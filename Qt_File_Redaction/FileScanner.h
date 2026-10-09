#pragma once
#include <filesystem>
#include <vector>
#include <string>
using namespace std;
namespace fs = std::filesystem;

class FileScanner {
public:
    // 扫描文件夹，返回所有普通文件的完整路径列表
    // isRecursive = true 时递归子目录，否则只扫顶层
    vector<fs::path> scan(const fs::path& folderPath, bool isRecursive) 
    {
        vector<fs::path> result;
        if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) 
        {
            //Qt部分实现返回错误提醒
            return result; 
        }

        if (isRecursive) 
        {
            for (const auto& entry : fs::recursive_directory_iterator(folderPath)) 
            {
                if (entry.is_regular_file()) 
                {
                    result.push_back(entry.path());
                }
            }
        }
        else 
        {
            for (const auto& entry : fs::directory_iterator(folderPath)) 
            {
                if (entry.is_regular_file()) 
                {
                    result.push_back(entry.path());
                }
            }
        }
        return result;
    }
};