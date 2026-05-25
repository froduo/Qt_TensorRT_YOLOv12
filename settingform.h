#ifndef SETTINGFORM_H
#define SETTINGFORM_H

#include <QDialog>
#include "app_config.h"

namespace Ui {
class settingForm;
}

class settingForm : public QDialog
{
    Q_OBJECT

public:
    explicit settingForm(const AppConfig &config, QWidget *parent = nullptr);
    ~settingForm();
    AppConfig getUpdatedConfig() const; // 获取界面修改后的配置

private:
    Ui::settingForm *ui;
    void scanSerialPorts(); // 扫描系统可用串口
    void ensureDefaultClassesFile(); // 确保默认类别文件存在

private slots:
    void handleBrowsePath(); // 浏览引擎文件按钮槽函数
    void handleBrowseClasses(); // 浏览类别文件按钮槽函数
    void handleBrowseSavePath(); // 浏览保存路径按钮槽函数
};

#endif // SETTINGFORM_H
