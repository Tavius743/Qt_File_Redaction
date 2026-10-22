#pragma warning(disable: 4996)
#include "Qt_File_Redaction.h"
#include <thread>
#include <QHeaderView>
#include <QCheckBox>
#include <QGroupBox>
#include <QDialog>
#include <QPushButton>
#include <QFile>
#include <QVBoxLayout>
#include <QTextBrowser>
#include <QHBoxLayout>
#include <QSet>
#include <map>
#include <algorithm>
#include <QTableWidgetItem>

Qt_File_Redaction::Qt_File_Redaction(QWidget *parent)   //构造函数
    : QMainWindow(parent)
{
    ui.setupUi(this);
	ui.groupBox->hide();  //隐藏自定义规则区域
    connect(ui.cbCustom, &QCheckBox::toggled, ui.groupBox, &QGroupBox::setVisible);
    //窗口标题
    setWindowTitle("隐盾 - 文件脱敏工具");

    // 初始化自定义规则表格
    ui.tableWidgetCustom->setColumnCount(5);
    ui.tableWidgetCustom->setHorizontalHeaderLabels({ "规则名称", "隐藏字段", "显示字段", "SM3匿名化", "启用" });

    // 设置列宽（根据你实际界面的宽度调整）
    ui.tableWidgetCustom->setColumnWidth(0, 100); // 规则名称
    ui.tableWidgetCustom->setColumnWidth(1, 200); // 隐藏字段
    ui.tableWidgetCustom->setColumnWidth(2, 200); // 显示字段
    ui.tableWidgetCustom->setColumnWidth(3, 50);  // SM3匿名化
    ui.tableWidgetCustom->setColumnWidth(4, 50);  // 启用

    // 让“隐藏字段”列自动拉伸填满空间
    ui.tableWidgetCustom->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    //初始显示第一页
	ui.stackedWidget->setCurrentIndex(0);
}

Qt_File_Redaction::~Qt_File_Redaction() //析构函数
{}

//================辅助函数实现==================
void Qt_File_Redaction::goToPage(int index)
{
	ui.stackedWidget->setCurrentIndex(index);
}

void Qt_File_Redaction::appendProgress(const QString& text)
{
	ui.textEditProgress->append(text);
    Logger::logInfo(text.toStdString());
}

void Qt_File_Redaction::appendReport(const QString& text)
{
	ui.textEditReport->append(text);	
}

void Qt_File_Redaction::setProgressStatus(int current, int total, const QString& filename, bool success)
{
	//单行动态更新，覆盖显示当前处理状态
	QString status = QString("正在脱敏 %1 / %2 个文件[%3] %4").arg(current).arg(total)
		.arg(success ? "OK" : "FAIL").arg(filename);
			ui.textEditProgress->append(status);
}

void Qt_File_Redaction::clearAllLogs()
{
	ui.textEditProgress->clear();
	ui.textEditReport->clear();
}

//================槽函数实现====================
//浏览按钮
void Qt_File_Redaction::on_btnBrowseSrc_clicked()   //浏览源文件夹按钮槽函数
{
	QString dir = QFileDialog::getExistingDirectory(this, "选择源文件夹");//弹出文件夹选择对话框
    /*各个参数意义
	* this: 父窗口指针，表示对话框的父窗口为当前窗口,确保对话框在当前窗口的上方显示
	* “选择源文件夹”: 对话框标题
    */
    if (!dir.isEmpty()) 
    {
		ui.lineEditSrc->setText(dir);   //将选择的文件夹路径显示在源文件夹输入框中
		m_srcPath = fs::path(dir.toStdWString()); // 使用宽字符串构造，避免窄->宽转换失败导致 std::system_error。
    }
}

void Qt_File_Redaction::on_btnBrowseDst_clicked()   //浏览输出文件夹按钮槽函数
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择输出文件夹");
    if (!dir.isEmpty())
    {
        ui.lineEditDst->setText(dir);
        m_dstPath = fs::path(dir.toStdWString()); // 使用宽字符串构造，避免窄->宽转换失败导致 std::system_error。
    }
}

//页面导航
void Qt_File_Redaction::on_btnNext0_clicked()   //下一步按钮槽函数
{
	QString srcDir = ui.lineEditSrc->text();
	QString dstDir = ui.lineEditDst->text();
	if (srcDir.isEmpty() || dstDir.isEmpty())
	{
		QMessageBox::warning(this, "警告", "源文件夹和输出文件夹不能为空！");
		return;
	}
	m_srcPath = fs::path(srcDir.toStdWString()); // 使用宽字符串构造，避免窄->宽转换失败
	m_dstPath = fs::path(dstDir.toStdWString()); // 使用宽字符串构造，避免窄->宽转换失败
	if (!fs::exists(m_srcPath) || !fs::is_directory(m_srcPath))
	{
		QMessageBox::warning(this, "警告", "源文件夹不存在或不是有效的文件夹！");
		return;
	}
	if (!fs::exists(m_dstPath) || !fs::is_directory(m_dstPath))
	{
		QMessageBox::warning(this, "警告", "输出文件夹不存在或不是有效的文件夹！");
		return;
	}
	goToPage(1);  //跳转到下一页
}

void Qt_File_Redaction::on_btnPrev1_clicked()   //上一步按钮槽函数
{
	goToPage(0);  //跳转到上一页
}

//开始脱敏
void Qt_File_Redaction::on_btnStarted_clicked()
{
    if (m_isRunning) {
        QMessageBox::warning(this, "警告", "进程被占用！");
        return;
    }
    m_isRunning = true;

    // 读取自定义规则表格
    int rowCount = ui.tableWidgetCustom->rowCount();
    for (int i = 0; i < rowCount; ++i) {
        // 第4列：启用复选框
        QWidget* enableWidget = ui.tableWidgetCustom->cellWidget(i, 4);
        QCheckBox* enableBox = enableWidget ? enableWidget->findChild<QCheckBox*>() : nullptr;
        if (!enableBox || !enableBox->isChecked()) continue;

        // 第0、1、2列：文本
        QTableWidgetItem* nameItem = ui.tableWidgetCustom->item(i, 0);
        QTableWidgetItem* targetItem = ui.tableWidgetCustom->item(i, 1);
        QTableWidgetItem* templItem = ui.tableWidgetCustom->item(i, 2);
        if (!nameItem || !targetItem || !templItem) continue;

        QString nameQStr = nameItem->text();
        QString targetQStr = targetItem->text();
        QString templQStr = templItem->text();

        // 第3列：SM3匿名化复选框
        QWidget* sm3Widget = ui.tableWidgetCustom->cellWidget(i, 3);
        QCheckBox* sm3Box = sm3Widget ? sm3Widget->findChild<QCheckBox*>() : nullptr;
        bool useSm3 = sm3Box && sm3Box->isChecked();

        // 校验
        if (targetQStr.trimmed().isEmpty())
        {
            QMessageBox::warning(this, "提示",
                QString("第%1行的“隐藏字段”未填写，请填写完整后再开始脱敏。").arg(i + 1));
            m_isRunning = false;
            return;
        }
        if (!useSm3 && targetQStr.length() != templQStr.length()) 
        {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle("规则校验提示");
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setText(QString("第 %1 行的规则长度不一致。").arg(i + 1));
            msgBox.setInformativeText(
                QString("隐藏字段：%1（%2 个字符）\n显示字段：%3（%4 个字符）\n\n"
                    "请选择：跳过此行继续脱敏，或返回修改该行内容。")
                .arg(targetQStr).arg(targetQStr.length())
                .arg(templQStr).arg(templQStr.length()));

            QPushButton* skipBtn = msgBox.addButton("跳过此行", QMessageBox::AcceptRole);
            QPushButton* modifyBtn = msgBox.addButton("返回修改", QMessageBox::RejectRole);
            msgBox.setDefaultButton(modifyBtn);  // 默认选中"返回修改"
            msgBox.exec();

            if (msgBox.clickedButton() == skipBtn) 
            {
                continue;  // 跳过本行，继续处理其他规则
            }
            else 
            {
                // 用户选择返回修改，直接终止脱敏流程
                m_isRunning = false;
                return;
            }
        }
        std::string name = nameQStr.toUtf8().constData();
        std::string target = targetQStr.toUtf8().constData();
        std::string templ = templQStr.toUtf8().constData();

        m_engine.addCustomRule(name, target, templ, useSm3);
    }

    // 先关闭所有地区专属规则
    m_engine.setRuleEnabled("id_card_cn", false);
    m_engine.setRuleEnabled("phone_cn", false);
    m_engine.setRuleEnabled("plate_cn", false);
    m_engine.setRuleEnabled("id_card_hk", false);
    m_engine.setRuleEnabled("phone_hk", false);
    m_engine.setRuleEnabled("plate_hk", false);
    m_engine.setRuleEnabled("id_card_mo", false);
    m_engine.setRuleEnabled("phone_mo", false);
    m_engine.setRuleEnabled("plate_mo", false);
    m_engine.setRuleEnabled("id_card_tw", false);
    m_engine.setRuleEnabled("phone_tw", false);
    m_engine.setRuleEnabled("plate_tw", false);

    // 读取下拉框
    QString region = ui.cbRegion->currentText();

    // 根据地区精准映射四类数据
    if (region == "中国内地") {
        m_engine.setRuleEnabled("id_card_cn", ui.cbIDCard->isChecked());
        m_engine.setRuleEnabled("phone_cn", ui.cbPhone->isChecked());
        m_engine.setRuleEnabled("plate_cn", ui.cbCarPlate->isChecked());
    }
    else if (region == "中国香港") {
        m_engine.setRuleEnabled("id_card_hk", ui.cbIDCard->isChecked());
        m_engine.setRuleEnabled("phone_hk", ui.cbPhone->isChecked());
        m_engine.setRuleEnabled("plate_hk", ui.cbCarPlate->isChecked());
    }
    else if (region == "中国澳门") {
        m_engine.setRuleEnabled("id_card_mo", ui.cbIDCard->isChecked());
        m_engine.setRuleEnabled("phone_mo", ui.cbPhone->isChecked());
        m_engine.setRuleEnabled("plate_mo", ui.cbCarPlate->isChecked());
    }
    else if (region == "中国台湾") {
        m_engine.setRuleEnabled("id_card_tw", ui.cbIDCard->isChecked());
        m_engine.setRuleEnabled("phone_tw", ui.cbPhone->isChecked());
        m_engine.setRuleEnabled("plate_tw", ui.cbCarPlate->isChecked());
    }

    // 通用规则不受地区影响
    m_engine.setRuleEnabled("bank_card", ui.cbBankCard->isChecked());
    m_engine.setRuleEnabled("email", ui.cbEmail->isChecked());

    // ---- 切换到进度页（主线程） ----
    goToPage(2);
    ui.textEditProgress->clear();
    ui.progressBar->setValue(0);
    appendProgress("任务已启动，正在后台处理...");

    // ---- 捕获必要数据，启动工作线程 ----
    bool recursive = ui.Recursive->isChecked();
    fs::path srcPath = m_srcPath;
    fs::path dstPath = m_dstPath;
    // 注意：m_engine 是成员变量，工作线程中会使用，需确保线程安全（内部已使用互斥锁）

    std::thread([this, recursive, srcPath, dstPath]() {
        try {
            // ---- 1. 扫描文件 ----
            FileScanner scanner;
            auto allFiles = scanner.scan(srcPath, recursive);
            int allCount = (int)allFiles.size();
            QMetaObject::invokeMethod(this, [=]() {
                appendProgress(QString("扫描到 %1 个文件").arg(allCount));
                });

            if (allCount == 0) {
                QMetaObject::invokeMethod(this, [this]() {
                    appendProgress("未找到任何文件，任务终止。");
                    goToPage(3);
                    m_isRunning = false;
                    });
                return;
            }

            // ---- 2. 创建临时目录 ----
            fs::path tempPath = srcPath / ".temp_convert";
            fs::create_directories(tempPath);

            // ---- 3. 收集所有待脱敏的文件 ----
            std::vector<fs::path> txtFiles;

            // 转换 Word 到临时目录
            DocConverter::batchConvertToTxt(allFiles, srcPath, tempPath, txtFiles);
            int total = (int)txtFiles.size();
            QMetaObject::invokeMethod(this, [=]() {
                appendProgress(QString("将 %1 个 非TXT文件纯文本转化").arg(total));
                });

            // 复制原始 txt 到临时目录
            for (const auto& file : allFiles) 
            {
                if (file.extension() == ".txt"|| file.extension()==".csv" || file.extension() == ".docx" || file.extension() == ".xlsx") 
                {
                    ++total;
                    fs::path rel = fs::relative(file, srcPath);
                    fs::path dst = tempPath / rel;
                    fs::create_directories(dst.parent_path());
                    fs::copy(file, dst, fs::copy_options::overwrite_existing);
                    txtFiles.push_back(dst);
                }
            }
            QMetaObject::invokeMethod(this, [=]() {
                appendProgress(QString("共计 %1 个文件待脱敏").arg(total));
                });

            if (total == 0) {
                QMetaObject::invokeMethod(this, [this]() {
                    appendProgress("没有可脱敏的文件（无 .txt 且转换失败），任务终止。");
                    goToPage(3);
                    m_isRunning = false;
                    });
                return;
            }

            // ---- 4. 多线程脱敏 ----
            std::atomic<int> progress(0);
            // 注意：total 是 int，在 lambda 中按值捕获
            m_engine.batchProcess(txtFiles, dstPath, tempPath, progress,
                [this, total, &progress](const fs::path& file, bool success) {
                    int cur = progress.load();
                    QMetaObject::invokeMethod(this, [=]() {
                        ui.progressBar->setValue(total ? (cur * 100 / total) : 0);
                        setProgressStatus(cur, total,
                            QString::fromWCharArray(file.c_str()),
                            success);
                        });
                }
            );

            // ---- 5. 清理临时目录 ----
            fs::remove_all(tempPath);

            // ---- 6. 获取统计结果并切换到报告页 ----
            auto stats = m_engine.getStatistics();
            QMetaObject::invokeMethod(this, [this, stats]() 
                {
                ui.textEditReport->clear();
                ui.textEditReport->append("=== 脱敏统计报告 ===");
                ui.textEditReport->append("--------------------------------------");
                int totalHits = 0;
                // 内置规则英文名 → 中文显示
                // 内置规则英文名 → 中文显示（包含港澳台）
                std::map<std::string, QString> nameMap = {
                    // 大陆
                    {"phone_cn",     QStringLiteral("手机号")},
                    {"id_card_cn",   QStringLiteral("身份证号")},
                    {"car_plate_cn", QStringLiteral("车牌号")},
                    {"email",        QStringLiteral("邮箱号")},
                    {"bank_card",    QStringLiteral("银行卡号")},

                    // 香港
                    {"id_card_hk",        QStringLiteral("香港身份证")},
                    {"hk_phone",     QStringLiteral("香港手机号")},
                    {"plate_hk",     QStringLiteral("香港车牌号")},

                    // 澳门
                    {"id_card_hk",        QStringLiteral("澳门身份证")},
                    {"mo_phone",     QStringLiteral("澳门手机号")},
                    {"plate_mo",     QStringLiteral("澳门车牌号")},

                    // 台湾
                    {"id_card_tw",        QStringLiteral("台湾身份证")},
                    {"tw_phone",     QStringLiteral("台湾手机号")},
                    {"plate_tw",     QStringLiteral("台湾车牌号")}
                };
                for (const auto& kv : stats) 
                {
                    QString displayName;
                    auto it = nameMap.find(kv.first);
                    if (it != nameMap.end()) 
                    {
                        displayName = it->second;
                    }
                    else 
                    {
                        // 自定义规则：直接使用用户输入的中文名
                        displayName = QString::fromUtf8(kv.first.c_str());
                    }
                    ui.textEditReport->append(QString("%1 : %2 次").arg(displayName).arg(kv.second));
                    totalHits += kv.second;
                }
                ui.textEditReport->append("--------------------------------------");
                ui.textEditReport->append(QString("共替换 %1 处敏感信息").arg(totalHits));
                ui.textEditReport->append("本次脱敏结束！");
                // 切换页面并重置运行标志
                goToPage(3);
                m_isRunning = false;
                });

        }
        catch (const std::exception& e) {
            // 捕获异常，在 UI 线程显示错误信息
            QMetaObject::invokeMethod(this, [this, e]() {
                appendProgress(QString("发生错误: %1").arg(e.what()));
                goToPage(3);
                m_isRunning = false;
                });
        }
        catch (...) {
            QMetaObject::invokeMethod(this, [this]() {
                appendProgress("发生未知错误，任务终止。");
                goToPage(3);
                m_isRunning = false;
                });
        }
        }).detach();
}

//退出
void Qt_File_Redaction::on_btnFinish_clicked()
{
	QApplication::quit();
}

//继续脱敏
void Qt_File_Redaction::on_btnContinue_clicked()
{
	Qt_File_Redaction::clearAllLogs();
	ui.lineEditSrc->clear();
	ui.lineEditDst->clear();
	m_srcPath.clear();
	m_dstPath.clear();
	Qt_File_Redaction::goToPage(0);
}

//查看错误日志
void Qt_File_Redaction::on_btnOpenErrorLog_clicked()
{
	QString logPath = QDir::currentPath() + "/error.log";
    if (QFileInfo::exists(logPath))
    {
		QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));

	}
    else
    {
        QMessageBox::information(this, "提示", "暂无错误日志");
    }
}
void Qt_File_Redaction::on_btnAddRule_clicked()
{
    //修改表格前，先屏蔽所有信号
    ui.tableWidgetCustom->blockSignals(true);

    int row = ui.tableWidgetCustom->rowCount();
    ui.tableWidgetCustom->insertRow(row);

    // 第0列：规则名称
    ui.tableWidgetCustom->setItem(row, 0, new QTableWidgetItem("新规则"));
    // 第1列：隐藏字段
    ui.tableWidgetCustom->setItem(row, 1, new QTableWidgetItem(""));
    // 第2列：显示字段
    ui.tableWidgetCustom->setItem(row, 2, new QTableWidgetItem(""));

    // 第3列：SM3匿名化（复选框居中）
    QCheckBox* sm3Box = new QCheckBox();
    QWidget* sm3Widget = new QWidget();
    QHBoxLayout* sm3Layout = new QHBoxLayout(sm3Widget);
    sm3Layout->addWidget(sm3Box);
    sm3Layout->setAlignment(Qt::AlignCenter);
    sm3Layout->setContentsMargins(0, 0, 0, 0);
    ui.tableWidgetCustom->setCellWidget(row, 3, sm3Widget);

    // 第4列：启用（默认勾选，居中）
    QCheckBox* enableBox = new QCheckBox();
    enableBox->setChecked(true);
    QWidget* enableWidget = new QWidget();
    QHBoxLayout* enableLayout = new QHBoxLayout(enableWidget);
    enableLayout->addWidget(enableBox);
    enableLayout->setAlignment(Qt::AlignCenter);
    enableLayout->setContentsMargins(0, 0, 0, 0);
    ui.tableWidgetCustom->setCellWidget(row, 4, enableWidget);

    //修改完毕后，恢复信号
    ui.tableWidgetCustom->blockSignals(false);
}

void Qt_File_Redaction::on_btnRemoveRule_clicked()
{
    ui.tableWidgetCustom->blockSignals(true);   // 屏蔽信号

    QList<QTableWidgetItem*> selected = ui.tableWidgetCustom->selectedItems();
    QSet<int> rowsToDelete;
    for (auto* item : selected) rowsToDelete.insert(item->row());

    QList<int> sortedRows = rowsToDelete.values();
    std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());
    for (int row : sortedRows) 
    {
        ui.tableWidgetCustom->removeRow(row);
    }

    ui.tableWidgetCustom->blockSignals(false);  // 恢复信号
}

void Qt_File_Redaction::on_btnManual_clicked()
{
    //创建对话框
    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle("使用说明");
    dialog->resize(700, 600);
    dialog->setAttribute(Qt::WA_DeleteOnClose); //关闭时自动释放

    //垂直布局
    QVBoxLayout* layout = new QVBoxLayout(dialog);
    
    //富文本浏览器
    QTextBrowser* browser = new QTextBrowser(dialog);
    browser->setOpenExternalLinks(true);//支持点击超链接

    //从资源文件中加载html
    QFile file(":docs/manual.html");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        browser->setHtml(QString::fromUtf8(file.readAll()));
        file.close();
    }
    else
    {
        browser->setPlainText("使用说明文件加载失败，请检查资源文件");
    }

    layout->addWidget(browser);

    //关闭按钮
    QPushButton* closeBtn = new QPushButton("关闭", dialog);
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);

    //模态显示
    dialog->exec();
}