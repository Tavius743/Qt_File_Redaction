#pragma once
#include <filesystem>
#include <vector>
#include <string>
#include <thread>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <cstdlib>
#include<fstream>
#include<sstream>
#include<QCoreApplication>
#include<QProcess>
#include "Logger.h"

namespace fs = std::filesystem;
using namespace std;
namespace DocConverter {

    // 辅助函数：判断字符串是否以某后缀结尾
    inline bool hasSuffix(const string& str, const string& suffix) {
        if (str.size() < suffix.size()) return false;
        return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    inline fs::path getExeDirectory()
    {
        QString path = QCoreApplication::applicationDirPath();
        return fs::path(path.toStdWString());
    }

    //获取 xdoc2txt 转换 .doc
    inline fs::path getXdoc2txtPath()
    {
        return getExeDirectory() / "tools" / "xd2tx220_x64" / "command" / "xdoc2txt.exe";
    }

    //使用 xdoc2txt 转换 .doc和.xlsx
    inline bool convertWithXdoc2txt(const fs::path& srcPath, const fs::path& dstPath)
    {
        fs::path xdocPath = getXdoc2txtPath();
        if (!fs::exists(xdocPath))
        {
            Logger::logError("找不到 xdoc2txt.exe,期望路径" + xdocPath.string());
            return false;
        }

        //创建临时工作目录，避免源路径的特殊字符导致xdoc2txt解析失败
        fs::path tempDir = "C:/xdoc2txt_work";
        fs::create_directories(tempDir);
        fs::path tempInput = tempDir / "intput.tmp";

        //复制原文件到临时目录
        try
        {
            fs::copy_file(srcPath, tempInput, fs::copy_options::overwrite_existing);
        }
        catch (...)
        {
            Logger::logError("无法复制文件到临时目录：" + srcPath.string());
            return false;
        }

        //调用xdoc2txt，使用-8（输出UTF-8）
        QProcess process;
        QStringList args;
        //-u 强制输出UTF-8，解决中文乱码
        args << "-8" << QString::fromWCharArray(srcPath.wstring().c_str());

        process.start(QString::fromWCharArray(xdocPath.wstring().c_str()),args);
        process.waitForFinished();

        //从标准输出读取内容
        QByteArray output = process.readAllStandardOutput();
        QByteArray errorOutput = process.readAllStandardError();

        if (output.isEmpty())
        {
            Logger::logError("xdoc3txt 转换失败：" + srcPath.string()
                + "退出码：" + to_string(process.exitCode())
                + "错误信息：" + errorOutput.toStdString());
            fs::remove(tempInput);
            return false;
        }
        
        //写入目标文件
        ofstream outFile(dstPath, ios::binary);
        if (!outFile.is_open())
        {
            Logger::logError("无法创建输出文件：" + dstPath.string());
            fs::remove(tempInput);
            return false;
        }
        outFile.write(output.data(), output.size());
        outFile.close();

        //清理临时文件
        if (fs::exists(tempInput)) fs::remove(tempInput);
        return true;
    }

    //获取 pandoc 支持的文件格式对应的 -f 参数
    inline string getPandocFormat(const fs::path& file)
    {
        string ext = file.extension().string();
        if (ext == ".odt") return "odt";
        if (ext == ".html" || ext == ".htm") return "html";
        if (ext == ".epub") return "epub";
        return "";
    }

    //判断文件是否需要通过 pandoc 转换
    inline bool needPandocConvert(const fs::path& file)
    {
        string ext = file.extension().string();
        return ext == ".docx" || ext == ".doc" || ext == ".html"|| ext == ".odt" || ext == ".htm";
    }

    // 批量转换 Word 到 TXT
    inline void batchConvertToTxt(
        const vector<fs::path>& src_paths,// 输入文件列表
        const fs::path& base_src_dir, //源根目录
        const fs::path& dst_root, //输出根目录
        std::vector<fs::path>& out_txt_paths//输出参数
    ) 
    {
        fs::path exeDir = getExeDirectory();  // 先获取 exe 目录
        fs::path pandocPath = exeDir / "tools" / "pandoc.exe";  // 再拼接路径

        // 初始化输出列表，默认空路径
        out_txt_paths.assign(src_paths.size(), fs::path());

        //获取pandoc.exe路径
        if(!fs::exists(pandocPath))
        {
            Logger::logError("找不到pandoc.exe,期望路径：" + pandocPath.string());
            return; //所有转换失败，直接返回
        }

        // 过滤出需要转换的文档
        std::vector<fs::path> to_convert_pandoc;
        for (const auto& p : src_paths) 
        {
            if (!getPandocFormat(p).empty())
            {
                to_convert_pandoc.push_back(p);
            }
        }

        //使用xdoc2txt处理 .doc 和 .xls
        for (size_t i = 0; i < src_paths.size(); ++i)
        {
            string ext = src_paths[i].extension().string();
            if (ext == ".doc" || ext == ".xls")
            {
                fs::path relative_path = fs::relative(src_paths[i], base_src_dir);
                fs::path dst_path = dst_root / relative_path;
                fs::create_directories(dst_path.parent_path());

                bool success = convertWithXdoc2txt(src_paths[i], dst_path);
                if (success) 
                {
                    out_txt_paths[i] = dst_path;
                }
                else
                {
                    out_txt_paths[i] = fs::path();
                }
            }
        }

        //使用 pandoc
        int total = (int)to_convert_pandoc.size();
        if (total == 0) return;  

        // 2个线程
        int num_threads = min(6, total);
        if(num_threads == 0) num_threads = 1;
        int batch_size = (total + num_threads - 1) / num_threads;
        vector<thread> threads;

        // 启动线程分块处理
        for (int i = 0; i < num_threads; ++i) 
        {
            int start = i * batch_size;
            int end = min(start + batch_size, total);

            threads.emplace_back([&, start, end]() 
                {
                    for (int j = start; j < end; ++j)
                    {
                        try {
                            const fs::path& original = to_convert_pandoc[j];
                            // 计算相对路径（保留目录结构）
                            fs::path relative_path = fs::relative(original, base_src_dir);
                            fs::path dst_path = dst_root / relative_path;
                            // 自动创建父目录
                            fs::create_directories(dst_path.parent_path());

                            string format = getPandocFormat(original);  //获取转换类型

                            // ===== 改用 QProcess 调用 pandoc（替代 system） =====
                            QProcess process;
                            QStringList args;
                            args << "-f" << QString::fromStdString(format)
                                << "-t" << "plain"
                                << "-o" << QString::fromWCharArray(dst_path.wstring().c_str())
                                << QString::fromWCharArray(original.wstring().c_str());

                            process.start(QString::fromWCharArray(pandocPath.wstring().c_str()), args);
                            process.waitForFinished();

                            int ret = process.exitCode();

                            // 如果转换失败，可将错误信息写入日志
                            if (ret != 0) {
                                Logger::logError("pandoc 转换失败: " + original.string()+"返回值："+ to_string(ret));
                            }

                            // 在 src_paths 中找到对应的索引，填入结果
                            // （因为 to_convert 是子集，需要反向映射）
                            for (int k = 0; k < src_paths.size(); k++)
                            {
                                if (src_paths[k] == original)
                                {
                                    if (ret == 0)
                                    {
                                        out_txt_paths[k] = dst_path;
                                    }
                                    else
                                    {
                                        out_txt_paths[k] = fs::path();  // 转换失败留空
                                    }
                                    break;
                                }
                            }
                        }
                        catch (const exception& e)
                        {
                            Logger::logError("DocConverter 异常：" + string(e.what())
                                + "文件：" + to_convert_pandoc[j].string());
                        }
                        catch (...)
                        {
                            Logger::logError("DocConverter 未知异常，文件：" + to_convert_pandoc[j].string());
                        }
                    }
                });
        }
        // 等待所有线程完成
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    }

}