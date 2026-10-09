#pragma once
#pragma warning(disable: 4996)
#include<filesystem>
#include<fstream>
#include<string>
#include<vector>
#include<Qstring>
#include<QDir>
#include<QFile>
#include<chrono>
#include<functional>
#include"third_party/zip.h"
#include"third_party/unzip.h"
#include"Logger.h"
#include"third_party/Pugixml.hpp"
namespace fs = std::filesystem;
using namespace std;
class DocxProcessor
{
public:
	static bool process(const fs::path& srcPath, const fs::path dstPath, function<string(const string&)> redactFunc)
	{
		//获取公共目录作为临时目录（永远是英文目录）
		const char* pubDir = std::getenv("PUBLIC");
		std::string uniqueId = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
		fs::path tempDir = (pubDir ? fs::path(pubDir) : fs::path("C:/Users/Public")) / ("redact_docx_" + uniqueId);

		try
		{
			fs::remove_all(tempDir);
		}
		catch (...) {}
		fs::create_directories(tempDir);

		fs::path tempSrc = tempDir / "input.docx";
		fs::path tempDst = tempDir / "output.docx";
		fs::path unzipDir = tempDir / "unzipped";

		try
		{
			//复制源文件到临时目录
			fs::copy_file(srcPath, tempSrc, fs::copy_options::overwrite_existing);

			//解压.docx
			if (!extractZip(tempSrc, unzipDir)) return false;
			
			//处理XML(正文文字在word/document.xml中)
			fs::path docXml = unzipDir / "word" / "document.xml";
			if (fs::exists(docXml))
			{
				if (!processXml(docXml, redactFunc)) return false;
			}

			//重新打包为.docx
			if (!packZip(unzipDir, tempDst)) return false;

			//复制到目标路径
			fs::create_directories(dstPath.parent_path());
			fs::copy_file(tempDst, dstPath, fs::copy_options::overwrite_existing);

			//清理临时目录
			fs::remove_all(tempDir);
			return true;
		}
		catch (const exception& e)
		{
			Logger::logError(string("DocxProcessor异常: ") + string(e.what()));
			fs::remove_all(tempDir);
			return false;
		}
	}
private:
	//解压文件
	static bool extractZip(const fs::path& zipPath, const fs::path& dstPath)
	{
		unzFile uf = unzOpen(zipPath.string().c_str());
		if (!uf)
		{
			Logger::logError("无法打开 docx文件（可能加密或者损坏）：" + zipPath.string());
			return false;
		}

		unz_global_info gi;
		if (unzGetGlobalInfo(uf, &gi) != UNZ_OK)
		{
			unzClose(uf);
			return false;
		}

		char readBuffer[8192];
		for (uLong i = 0; i < gi.number_entry; i++)
		{
			char filenameInZip[256];
			unz_file_info fileInfo;
			if (unzGetCurrentFileInfo(uf, &fileInfo, filenameInZip, sizeof(filenameInZip), NULL, 0, NULL, 0) != UNZ_OK)
				break;

			fs::path fullPath = dstPath / filenameInZip;
			//处理文件夹
			if (filenameInZip[strlen(filenameInZip) - 1] == '/')
			{
				fs::create_directories(fullPath);
			}
			else
			{
				//确保目录存在
				fs::create_directories(fullPath.parent_path());
				if (unzOpenCurrentFile(uf) == UNZ_OK)
				{
					ofstream outFile(fullPath, ios::binary);
					int error = UNZ_OK;
					do {
						error = unzReadCurrentFile(uf, readBuffer, sizeof(readBuffer));
						if (error > 0)
							outFile.write(readBuffer, error);
					} while (error > 0);
					outFile.close();
					unzCloseCurrentFile(uf);
				}
			}
			if (i < gi.number_entry - 1)
			{
				unzGoToNextFile(uf);
			}
		}
		unzClose(uf);
		return true;
	}

	// 解析并处理单个 XML 文件
	static bool processXml(const fs::path& xmlPath, std::function<std::string(const std::string&)> redactFunc)
	{
		pugi::xml_document doc;
		if (!doc.load_file(xmlPath.string().c_str()))
		{
			return true; // 跳过空文件
		}
		for (pugi::xml_node node = doc.first_child(); node; node = node.next_sibling())
		{
			processNode(node, redactFunc);
		}
		return doc.save_file(xmlPath.string().c_str());
	}

	// 递归处理 XML 节点（逐节点安全替换）
	static void processNode(pugi::xml_node& node, std::function<std::string(const std::string&)> redactFunc) 
	{
		for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) 
		{
			if (child.type() == pugi::node_pcdata) 
			{
				std::string text = child.value();
				if (!text.empty()) 
				{
					// 每个文本节点独立调用一次脱敏，绝不跨节点！
					std::string redacted = redactFunc(text);
					if (redacted != text) 
					{
						child.set_value(redacted.c_str());
					}
				}
			}
			else 
			{
				processNode(child, redactFunc);
			}
		}
	}


	//打包文件
	static bool packZip(const fs::path& srcPath, const fs::path& zipPath)
	{
		zipFile zf = zipOpen(zipPath.string().c_str(), APPEND_STATUS_CREATE);
		if (!zf)
		{
			Logger::logError("无法创建 docx文件：" + zipPath.string());
			return false;
		}
		for (const auto& entry : fs::recursive_directory_iterator(srcPath))
		{
			if (entry.is_regular_file())
			{
				fs::path filePath = entry.path();
				fs::path relativePath = fs::relative(filePath, srcPath);
				string relativeStr = relativePath.generic_string();

				zip_fileinfo zi = { 0 };
				if (zipOpenNewFileInZip(zf, relativeStr.c_str(), &zi, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_BEST_SPEED) == ZIP_OK)
				{
					ifstream inFile(filePath, ios::binary);
					inFile.seekg(0, ios::end);
					streamsize fileSize = inFile.tellg();
					inFile.seekg(0, ios::beg);
					vector<char> buffer(fileSize);
					inFile.read(buffer.data(), fileSize);
					zipWriteInFileInZip(zf, buffer.data(), (unsigned int)buffer.size());
					zipCloseFileInZip(zf);
					inFile.close();
				}
			}
		}
		zipClose(zf, NULL);
		return true;
	}
};