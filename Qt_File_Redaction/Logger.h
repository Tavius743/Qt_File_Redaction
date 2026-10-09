#pragma once
#pragma warning(disable: 4996)
#include<string>
#include<fstream>
#include<chrono>	//时间处理标准库头文件
#include<iomanip>
#include<sstream>
using namespace std;

class Logger
{
public:
	static void logError(const string& msg)
	{
		ofstream log("error.log", ios::app);
		if (log.is_open())
		{
			auto now = chrono::system_clock::now();
			auto now_c = chrono::system_clock::to_time_t(now);
			log << put_time(localtime(&now_c), "%Y-%m-%d %H:%M:%S") << " ";
			log << "[ERROR] " << msg << endl;
			log.close();
		}
	}
	static void logInfo(const string& msg)
	{
		ofstream log("info.log", ios::app);
		if (log.is_open())
		{
			auto now = chrono::system_clock::now();
			auto now_c = chrono::system_clock::to_time_t(now);
			log << put_time(localtime(&now_c), "%Y-%m-%d %H:%M:%S") << " ";
			log << "[INFO] " << msg << endl;
			log.close();
		}
	}
};