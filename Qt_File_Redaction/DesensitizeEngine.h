#pragma once
#include"DocxProcessor.h"
#include"XlsxProcessor.h"
#include"third_party/sm3.h"
#include<string>
#include<vector>
#include<regex>
#include<thread>
#include<mutex>
#include<atomic>
#include<fstream>
#include<sstream>
#include<functional>
#include<algorithm>
#include<iterator>
#include<cctype>
#include<filesystem>
#include<unordered_map>
#include<unordered_set>
#include"Logger.h"



using namespace std;
namespace fs = filesystem;

struct DesensitizeRule
{
	string name;	//规则名
	regex pattern;
	function<string(const string&)> maskFunc;//替换函数
	bool enabled; 	//是否启用
	string replacement; 	//可选直接替换值
};

class DesensitizeEngine
{
public:
	DesensitizeEngine() 	//默认构造
	{
		loadDefaultRules();
		resetStatistics();
	}

	void loadDefaultRules()	//加载默认规则
	{
		m_rules.clear();
		m_rules.push_back({ "phone_cn", regex(R"(1[3-9]\d{9})"),
			[](const string& s) {return s.substr(0,3) + "****" + s.substr(7,4); }, true });
		// 优化后的身份证号正则（校验出生日期合法性）
		m_rules.push_back({
			"id_card_cn",
			// 前6位数字 + 8位日期（1900-2099年，01-12月，01-31日）+ 3位顺序码 + 校验码
			std::regex(R"([1-8]\d{5}(19|20)\d{2}(0[1-9]|1[0-2])(0[1-9]|[12]\d|3[01])\d{3}[\dXx])"),
			[](const string& s) {
				return s.substr(0, 6) + "********" + s.substr(14, 4);
			},
			true
			});
		m_rules.push_back({ "email", regex(R"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})"), 
			[](const string& s) 
			{
				size_t at = s.find('@'); 
				if (at <= 0) return s;
				return s.substr(0,1) + "****" + s.substr(at - 1,1) + s.substr(at); 
			}
			, true });
		m_rules.push_back({ "bank_card", regex(R"(\b([1-9]\d{15}|[1-9]\d{18})\b)"),
			[](const string& s) 
			{
				int len = s.length();
				if (len <= 10)return s;
				return s.substr(0, 6) + string(len - 10, '*') + s.substr(len - 4, 4);
			}
		,true });
		m_rules.push_back({ "car_plate_cn", regex(R"([京津沪渝冀豫云辽黑湘皖鲁新苏浙赣鄂桂甘晋蒙陕吉闽贵粤青藏川宁琼][A-Z][A-Z0-9]{5})"),
			[](const std::string& s) { return s.substr(0,2) + "***" + s.substr(s.length() - 1,1); }, true });

		// 中国香港
		m_rules.push_back({ "id_card_hk", std::regex(R"([A-Z]{1,2}\d{6}\([0-9A]\))", std::regex::icase),
			[](const string& s) { return s.substr(0, 1) + "******" + s.substr(s.length() - 3); }, false });
		m_rules.push_back({ "phone_hk", std::regex(R"(\b[569]\d{7}\b)"),
			[](const string& s) { return s.substr(0, 2) + "****" + s.substr(6, 2); }, false });
		m_rules.push_back({ "plate_hk", std::regex(R"([A-Z]{2}\s?\d{1,4})", std::regex::icase),
			[](const string& s) { return s.substr(0, 2) + "****"; }, false });

		// 中国澳门
		m_rules.push_back({ "id_card_mo", std::regex(R"([157]\d{6}\([0-9A-Za-z]\))"),
			[](const string& s) { return "******" + s.substr(s.length() - 3, 3); }, false });
		m_rules.push_back({ "phone_mo", std::regex(R"(\b6\d{7}\b)"),
			[](const string& s) { return s.substr(0, 2) + "****" + s.substr(6, 2); }, false });
		m_rules.push_back({ "plate_mo", std::regex(R"(M[A-Z]\s?\d{2}\s?-\s?\d{2}|M\d{2}\s?-\s?\d{2})", std::regex::icase),
			[](const string& s) { return s.substr(0, 2) + "******"; }, false });

		//中国台湾
		m_rules.push_back({ "id_card_tw", std::regex(R"([A-Z][12]\d{8})", std::regex::icase),
			[](const string& s) { return s.substr(0, 1) + "********"; }, false });
		m_rules.push_back({ "phone_tw", std::regex(R"(\b09\d{8}\b)"),
			[](const string& s) { return s.substr(0, 3) + "***" + s.substr(6, 4); }, false });
		m_rules.push_back({ "plate_tw", std::regex(R"([A-Z]{2,3}-\d{4})", std::regex::icase),
			[](const string& s) { return s.substr(0, 2) + "******"; }, false });
	}

	bool addCustomRule(const string& name, const string& target,
		const string& templateStr, bool useSm3 = false)
	{
		// 按逗号拆分 target（支持一次输入多个敏感词）
		std::vector<std::string> targets;
		std::stringstream ss(target);
		std::string item;
		while (std::getline(ss, item, ',')) 
		{
			size_t start = item.find_first_not_of(" \t");
			size_t end = item.find_last_not_of(" \t");
			if (start != std::string::npos) 
			{
				targets.push_back(item.substr(start, end - start + 1));
			}
		}

		for (const auto& t : targets) 
		{
			string escaped = escapeRegex(t);
			if (useSm3) 
			{
				// SM3 匿名化模式
				m_rules.push_back(
					{
					name,
					regex(escaped),
					[](const string& s) -> string 
					{
						return SM3::hashShort(s, 16);
					},
					true,
					""
					});
			}
			else 
			{
				// 原有的掩码模式
				m_rules.push_back(
					{
					name,
					regex(escaped),
					[templateStr](const string& s) -> string 
					{
						string result = s;
						for (size_t i = 0; i < templateStr.length() && i < s.length(); ++i) 
						{
							if (templateStr[i] == '*') result[i] = '*';
						}
						return result;
					},
					true,
					""
					});
			}
		}
		return true;
	}

	void resetStatistics()	//互斥锁
	{
		lock_guard<mutex> lock(m_statsMutex);
		m_hitCount.clear();
	}

	void setRuleEnabled(string rulename, bool Enabled)	//设置规则启用状态
	{
		for (auto& rule : m_rules)
		{
			if (rule.name == rulename)
			{
				rule.enabled = Enabled;
				break;
			}
		}
	}

	bool isRuleEnabled(const string rulename)	//检查规则是否启用
	{
		for (auto& rule : m_rules)
		{
			if (rule.name == rulename)
			{
				return rule.enabled;
			}
		}
		return false;
	}

	string processContent(const std::string& content, size_t offset = 0)	//处理文本内容
	{
		string result = content;
		for (const auto& rule : m_rules)
		{
			if (!rule.enabled) continue;
			//快速预判（跳过不匹配的规则）
			if (rule.name == "phone_cn" || rule.name == "id_card_cn" || rule.name == "bank_card"
				||rule.name=="phone_hk" || rule.name == "phone_mo" || rule.name == "phone_tw"
				|| rule.name == "id_card_hk" || rule.name == "id_card_mo" || rule.name == "id_card_tw")
			{
				// 这三条规则一定需要数字，没有数字就跳过
				bool hasDigit = false;
				for (char c : result) 
				{
					if (c >= '0' && c <= '9') { hasDigit = true; break; }
				}
				if (!hasDigit) continue;
			}
			if (rule.name == "email") 
			{
				// 邮箱必须有 @
				if (result.find('@') == std::string::npos) continue;
			}
			if (rule.name == "car_plate_cn") 
			{
				// 车牌必须有 A-Z 字母或中文
				bool hasAlpha = false;
				for (char c : result) 
				{
					if ((c >= 'A' && c <= 'Z') || (unsigned char)c >= 0x80) { hasAlpha = true; break; }
				}
				if (!hasAlpha) continue;
			}

			//收集所有匹配项
			vector<smatch> matches;
			auto begin = sregex_iterator(result.begin(), result.end(), rule.pattern);
			auto end = sregex_iterator();
			for (auto it = begin; it != end; ++it) matches.push_back(*it);
			int replacedCount = 0;
			if (matches.empty()) continue;
			for (auto rit = matches.rbegin(); rit != matches.rend(); ++rit)
			{
				const smatch& m = *rit;
				if (m.position() < offset) continue;
				//三大规则的细化校验
				string matched = m.str();
				bool valid = true;
				if (rule.name == "phone_cn") valid = isValidPhone(matched);
				else if (rule.name == "id_card_cn")valid = isValidIdCard(matched);
				else if (rule.name == "bank_card") valid = isValidBankCard(matched);
				if (!valid) continue;

				replacedCount++;
				string replacement;
				if (rule.maskFunc)
				{
					replacement = rule.maskFunc(m.str());
				}
				else
				{
					replacement = rule.replacement;
				}
				result.replace(m.position(), m.length(), replacement);
			}
				lock_guard<mutex> lock(m_statsMutex);
				m_hitCount[rule.name] += replacedCount;
			
		}
		return result;
	}

	bool processFile(const fs::path& srcPath, const fs::path& dstPath)	//处理单个文件
	{
		try 
		{
			//预检查
			if (!fs::exists(srcPath))
			{
				Logger::logError("源文件不存在：" + srcPath.string());
				return false;
			}
			if (!fs::is_regular_file(srcPath))
			{
				Logger::logError("路径不是普通文件：" + srcPath.string());
				return false;
			}

			if (srcPath.extension() == ".csv")
			{
				return processCSVFile(srcPath, dstPath);
			}
			// 如果是 docx，走直接格式保留处理
			if (srcPath.extension() == ".docx") 
			{
				return DocxProcessor::process(srcPath, dstPath,
					[this](const std::string& text) 
					{
						return this->processContent(text, 0);
					});
			}
			if (srcPath.extension() == ".xlsx") 
			{
				return XlsxProcessor::process(srcPath, dstPath,
					[this](const std::string& text) 
					{
						return this->processContent(text, 0);
					});
			}

			uintmax_t fileSize = fs::file_size(srcPath);
			const uintmax_t THRESHOLD = 50 * 1024 * 1024;	//50MB

			if (fileSize < THRESHOLD)
			{
				return processFileSmall(srcPath, dstPath);
			}
			else
			{
				return processFileLarge(srcPath, dstPath);
			}
		}
		catch (const fs::filesystem_error& e)
		{
			Logger::logError("文件系统错误：" + string(e.what()));
			return false;
		}
		catch (const exception& e)
		{
			Logger::logError("未预料的异常：" + string(e.what()));
			return false;
		}
		catch (...)
		{
			Logger::logError("未知异常");
			return false;
		}
	}

	void batchProcess(
		const vector<fs::path>& srcFiles,	// 源文件列表
		const fs::path& dstRoot,			// 目标根目录
		const fs::path& srcRoot,			// 源根目录，用于计算相对路径
		atomic<int>& progress,				// 进度计数器
		function<void(const fs::path&, bool)> onFileDone = NULL	// 每个文件处理完成后的回调函数
	)
	{
		if (!fs::exists(dstRoot))
		{
			fs::create_directories(dstRoot);
		}
		// 过滤掉空路径，使用有效文件列表进行处理
		vector<fs::path> validFiles;
		for (const auto& p : srcFiles)
		{
			if (!p.empty()) validFiles.push_back(p);
		}
		int total = (int)validFiles.size();
		if (total == 0) return;

		resetStatistics();

		int numThreads = thread::hardware_concurrency();
		if (numThreads == 0) numThreads = 4;
		int batchSize = (total + numThreads - 1) / numThreads;    // 每个线程处理的数量
		vector<thread> threads;
		for (int i = 0; i < numThreads; i++)
		{
			int start = i * batchSize;
			int end = min(start + batchSize, total);
			threads.emplace_back([&, start, end]()
				{
					for (int j = start; j < end; j++)
					{
						try {
							// 使用 validFiles ，确保索引与总数一致
							const fs::path& current = validFiles[j];
							// 计算相对路径并构建目标路径
							fs::path relativePath = fs::relative(current, srcRoot);
							fs::path dstPath = dstRoot / relativePath;
							// 创建目标目录
							create_directories(dstPath.parent_path());
							bool success = processFile(current, dstPath);    // 处理单个文件
							progress.fetch_add(1);    // 原子地增加 progress

							if (onFileDone)
							{
								onFileDone(current, success);
							}
						}
						catch (const exception& e)
						{
							Logger::logError("batchProcess 线程异常" + string(e.what())
								+ "文件：" + validFiles[j].string());
							progress.fetch_add(1);
							if (onFileDone)
							{
								onFileDone(validFiles[j], false);
							}
						}
						catch (...)
						{
							Logger::logError("batchProcess 线程未知异常，文件：" + validFiles[j].string());
							progress.fetch_add(1);
							if (onFileDone)
							{
								onFileDone(validFiles[j], false);
							}
						}
					}
				});
		}
		for (auto& t : threads)
		{
			if (t.joinable()) t.join();
		}
	}
	unordered_map<string, int> getStatistics()const
	{
		lock_guard<mutex> lock(m_statsMutex);
		return m_hitCount;
	}

private:
	vector<DesensitizeRule> m_rules;
	mutable mutex m_statsMutex;
	unordered_map<string, int> m_hitCount;
	string escapeRegex(const string& str)
	{
		string result;
		const string specials = ".+*?^${}()|[]\\";
		for (char c:str)
		{
			if (specials.find(c) != string::npos)
			{
				result += "\\";
			}
			result += c;
		}
		return result;
	}
	bool processFileLarge(const fs::path& srcPath, const fs::path& dstPath)
	{
		try
		{
			//打开文件
			ifstream inFile(srcPath, ios::binary);
			if (!inFile.is_open())
			{
				Logger::logError("无法打开文件：" + srcPath.string());
				return false;
			}

			//原子写入
			fs::path tmpPath = dstPath;
			tmpPath += ".tmp";
			ofstream outFile(tmpPath,ios::binary);
			if (!outFile.is_open())
			{
				Logger::logError("无法创建临时文件：" + tmpPath.string());
				return false;
			}

			//分块处理
			const size_t CHUNK_SIZE = 64 * 1024; // 64KB
			const size_t TAIL_SIZE = 25; // 处理尾部可能被截断的敏感信息
			string buffer(CHUNK_SIZE, '\0');
			string tail;

			while (inFile.read(buffer.data(), CHUNK_SIZE) || inFile.gcount() > 0)
			{
				size_t bytesRead = inFile.gcount();
				string chunk(buffer.data(), bytesRead);
				//将上一个块的剩余部分加到当前块的开头
				string combined = tail + chunk;
				size_t tailLen = tail.size();
				tail.clear();
				//脱敏处理
				string processed = processContent(combined, tailLen);
				//提取当前块的处理结果
				size_t chunkStart = tailLen;
				if (processed.size() > chunkStart)
				{
					string chunkResult = processed.substr(chunkStart);
					outFile.write(chunkResult.data(), chunkResult.size());
				}
				//更新尾部
				tail = chunk.size() >= TAIL_SIZE ? chunk.substr(chunk.size() - TAIL_SIZE) : chunk;
			}
			inFile.close();
			outFile.close();
			if (outFile.fail())
			{
				Logger::logError("写入临时文件失败：" + tmpPath.string());
				fs::remove(tmpPath);
				return false;
			}
			fs::rename(tmpPath, dstPath);	//原子替换
			return true;
		}
		catch (const exception& e)
		{
			Logger::logError("大文件处理异常：" + string(e.what()));
			return false;
		}
		catch (...)
		{
			Logger::logError("大文件处理未知异常");
			return false;
		}
	}
	bool processFileSmall(const fs::path& srcPath, const fs::path& dstPath)
	{
		try
		{
			//打开文件
			ifstream inFile(srcPath);
			if (!inFile.is_open())
			{
				Logger::logError("无法打开文件：" + srcPath.string());
				return false;
			}

			stringstream buffer;
			buffer << inFile.rdbuf();
			string content = buffer.str();
			inFile.close();

			string processed = processContent(content, 0);

			//原子写入
			fs::path tmpPath = dstPath;
			tmpPath += ".tmp";
			ofstream outFile(tmpPath);
			if (!outFile.is_open())
			{
				Logger::logError("无法创建临时文件：" + tmpPath.string());
				return false;
			}
			outFile << processed;
			outFile.close();

			if (outFile.fail())
			{
				Logger::logError("写入临时文件失败：" + tmpPath.string());
				fs::remove(tmpPath);
				return false;
			}
			fs::rename(tmpPath, dstPath);	//原子替换
			return true;
		}
		catch (const exception& e)
		{
			Logger::logError("文件处理异常：" + string(e.what()));
			return false;
		}
		catch (...)
		{
			Logger::logError("文件处理未知异常");
			return false;
		}
	}
	bool processCSVFile(const fs::path& srcPath, const fs::path& dstPath)
	{
		try 
		{
			if (!fs::exists(srcPath))
			{
				Logger::logError("CSV文件不存在:" + srcPath.string());
				return false;
			}
			ifstream inFile(srcPath);
			if (!inFile.is_open())
			{
				Logger::logError("无法打开 CSV 文件：" + srcPath.string());
				return false;
			}
			fs::path tmpPath = dstPath;
			tmpPath += ".tmp";
			ofstream outFile(tmpPath);
			if (!outFile.is_open())
			{
				Logger::logError("无法创建 CSV 临时文件：" + tmpPath.string());
				return false;
			}

			string line;
			while (getline(inFile, line))
				//按列拆分，并以逗号为分隔
			{
				vector<string> cols;
				stringstream ss(line);
				string col;
				while (getline(ss, col, ','))
					//每列单独脱敏
				{
					string processed = processContent(col, 0);
					cols.push_back(processed);
				}
				//重组输出
				for (size_t i = 0; i < cols.size(); ++i)
				{
					if (i > 0) outFile << ',';
					outFile << cols[i];
				}
				outFile << "\n";
			}
			inFile.close();
			outFile.close();
			fs::rename(tmpPath, dstPath);
			return true;
		}
		catch (const exception& e)
		{
			Logger::logError("CSV 文件处理异常：" + string(e.what()));
			return false;
		}
		catch (...)
		{
			Logger::logError("CSV 文件处理未知异常");
			return false;
		}
	}
	
	bool isValidPhone(const string& phone_cn) const	//手机号白名单校验
	{
		if (phone_cn.length() != 11) return false;
		string prefix = phone_cn.substr(0, 3);
		static const unordered_set<string> validPrefixes =
		{
			"130","131","132","133","134","135","136","137","138","139",
			"150","151","152","153","155","156","157","158","159",
			"170","171","173","175","176","177","178",
			"180","181","182","183","184","185","186","187","188","189",
			"191","198","199"
		};
		return validPrefixes.count(prefix) > 0;
	}

	bool isValidIdCard(const string& id)const	//身份证校验
	{
		if (id.length() != 18) return false;
		for (int i = 0; i < 17; i++)
		{
			if (id[i]<'0'||id[i]>'9') return false;
		}
		const int weights[] = { 7,9,10,5,8,4,2,1,6,3,7,9,10,5,8,4,2 };
		const char checkCodes[] = "10X98765432";
		int sum = 0;
		for (int i = 0; i < 17; i++)
		{
			sum += (id[i] - '0') * weights[i];
		}
		char expected = checkCodes[sum % 11];
		char actual = id[17];
		if (actual >= 'a' && actual <= 'z')
			actual = actual - 'a' + 'A';
		return actual == expected;
	}

	bool isValidBankCard(const string& card) const
	{
		if (card.length() < 16 || card.length() > 19) return false;
		int sum = 0;
		bool alternate = false;
		for (int i = (int)card.length() - 1; i >= 0; i--)
		{
			if (!isdigit((unsigned char)card[i])) return false;
			int n = card[i] - '0';
			if (alternate)
			{
				n *= 2;
				if (n > 9) n -= 9;
			}
			sum += n;
			alternate = !alternate;
		}
		return (sum % 10 == 0);
	}
	
};