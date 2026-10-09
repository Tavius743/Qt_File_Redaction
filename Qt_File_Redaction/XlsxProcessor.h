#pragma once
#pragma warning(disable: 4996)
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <functional>
#include <QString>
#include <QDir>
#include<chrono>
#include <QFile>
#include "third_party/pugixml.hpp"
#include "third_party/unzip.h"
#include "third_party/zip.h"
#include "Logger.h"

namespace fs = std::filesystem;

class XlsxProcessor {
private:
    // 解压
    static bool extractZip(const fs::path& zipPath, const fs::path& destDir) 
    {
        unzFile uf = unzOpen(zipPath.string().c_str());
        if (!uf) 
        {
            Logger::logError("无法打开 xlsx 文件（可能加密或损坏）: " + zipPath.string());
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
            if (unzGetCurrentFileInfo(uf, &fileInfo, filenameInZip, sizeof(filenameInZip), NULL, 0, NULL, 0) != UNZ_OK) break;

            fs::path fullPath = destDir / filenameInZip;
            if (filenameInZip[strlen(filenameInZip) - 1] == '/') 
            {
                fs::create_directories(fullPath);
            }
            else 
            {
                fs::create_directories(fullPath.parent_path());
                if (unzOpenCurrentFile(uf) == UNZ_OK) 
                {
                    std::ofstream outFile(fullPath, std::ios::binary);
                    int error = UNZ_OK;
                    do 
                    {
                        error = unzReadCurrentFile(uf, readBuffer, sizeof(readBuffer));
                        if (error > 0) outFile.write(readBuffer, error);
                    } while (error > 0);
                    outFile.close();
                    unzCloseCurrentFile(uf);
                }
            }
            if (i < gi.number_entry - 1) unzGoToNextFile(uf);
        }
        unzClose(uf);
        return true;
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


    // 重新打包
    static bool packZip(const fs::path& srcDir, const fs::path& zipPath) 
    {
        zipFile zf = zipOpen(zipPath.string().c_str(), APPEND_STATUS_CREATE);
        if (!zf) 
        {
            Logger::logError("无法创建 xlsx 输出文件");
            return false;
        }

        for (const auto& entry : fs::recursive_directory_iterator(srcDir)) 
        {
            if (entry.is_regular_file()) 
            {
                fs::path filePath = entry.path();
                fs::path relativePath = fs::relative(filePath, srcDir);
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

public:
    static bool process(const fs::path& srcPath, const fs::path& dstPath, std::function<std::string(const std::string&)> redactFunc) 
    {
       
        // 在系统公共目录建工作区（避开中文路径）
        const char* pubDir = std::getenv("PUBLIC");
        std::string uniqueId = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        fs::path tempDir = (pubDir ? fs::path(pubDir) : fs::path("C:/Users/Public")) / ("redact_xlsx_" + uniqueId);

        try
        {
            fs::remove_all(tempDir);
		}
        catch (...){}
        fs::create_directories(tempDir);

        fs::path tempSrc = tempDir / "input.xlsx";
        fs::path tempDst = tempDir / "output.xlsx";
        fs::path unzipDir = tempDir / "unzipped";

        try 
        {
            // 1. 复制到纯英文路径
            fs::copy_file(srcPath, tempSrc, fs::copy_options::overwrite_existing);

            // 2. 解压
            if (!extractZip(tempSrc, unzipDir)) return false;


            // 3. 处理 XML 文件
            // Excel 的文字主要存储在 xl/sharedStrings.xml
            fs::path sharedStrings = unzipDir / "xl" / "sharedStrings.xml";
            if (fs::exists(sharedStrings)) 
            {
                processXml(sharedStrings, redactFunc);
            }

            
            // 有些 Excel 文字直接内联在 sheet 里，扫描所有 worksheets
            fs::path worksheetsDir = unzipDir / "xl" / "worksheets";
            if (fs::exists(worksheetsDir) && fs::is_directory(worksheetsDir)) 
            {
                for (const auto& entry : fs::directory_iterator(worksheetsDir)) 
                {
                    if (entry.path().extension() == ".xml") 
                    {
                        processXml(entry.path(), redactFunc);
                    }
                }
            }

            // 4. 重新打包
            if (!packZip(unzipDir, tempDst)) return false;
       
            // 5. 复制回目标路径
            fs::create_directories(dstPath.parent_path());
            fs::copy_file(tempDst, dstPath, fs::copy_options::overwrite_existing);

            // 6. 清理
            fs::remove_all(tempDir);
            return true;
        }
        catch (const std::exception& e) 
        {
            Logger::logError("XlsxProcessor 异常: " + std::string(e.what()));
            fs::remove_all(tempDir);
            return false;
        }
    }
};