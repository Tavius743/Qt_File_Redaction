#pragma once

#include <QtWidgets/QMainWindow>    //主窗口基类头文件
#include <QtWidgets/QFileDialog>    //文件对话框头文件
#include <QtWidgets/QMessageBox>    //消息对话框头文件
#include <QtWidgets/QApplication>   //应用程序头文件
#include <QMetaObject>              //元对象头文件
#include "ui_Qt_File_Redaction.h"
#include<filesystem>
#include<QDesktopServices>
#include<atomic>
#include"Logger.h"
#include"FileScanner.h"
#include"DocConverter.h"
#include"DesensitizeEngine.h"

using namespace std;
namespace fs = filesystem;

class Qt_File_Redaction : public QMainWindow
{
    Q_OBJECT

public:
    Qt_File_Redaction(QWidget *parent = nullptr);
    ~Qt_File_Redaction();

private slots:  //私有槽函数
    //浏览按钮
	void on_btnBrowseSrc_clicked();
	void on_btnBrowseDst_clicked();
    //页面导航
	void on_btnNext0_clicked();
	void on_btnPrev1_clicked();
	//开始脱敏
    void on_btnStarted_clicked();
    //完成页面
    void on_btnContinue_clicked();
    void on_btnFinish_clicked();
	//查看错误日志
	void on_btnOpenErrorLog_clicked();
	//查看使用说明
	void on_btnManual_clicked();
	//自定义规则表格操作
	void on_btnAddRule_clicked();
	void on_btnRemoveRule_clicked();

private:
	Ui::Qt_File_RedactionClass ui;  //UI界面对象
	DesensitizeEngine m_engine;     //脱敏引擎对象
	fs::path m_srcPath;				//源文件路径
	fs::path m_dstPath;				//目标文件根路径
	atomic<bool> m_isRunning{false};//脱敏任务是否正在运行

	//辅助函数
	void goToPage(int index);
	void appendProgress(const QString& text);
	void appendReport(const QString& text);
	void setProgressStatus(int current, int total, const QString& filename, bool success);	//更新进度状态
	void clearAllLogs();	//清空所有日志
};

