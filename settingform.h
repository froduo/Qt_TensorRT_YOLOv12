#ifndef SETTINGFORM_H
#define SETTINGFORM_H

#include <QWidget>
#include <QDialog>
#include "app_config.h"
namespace Ui {
class settingForm;
}

class settingForm : public QDialog
{
    Q_OBJECT

public:
    explicit settingForm(const AppConfig &config,QWidget *parent = nullptr);
    ~settingForm();
    AppConfig getUpdatedConfig() const; // 获取界面修改后的配置
private:
    Ui::settingForm *ui;
    void scanSerialPorts(); // 扫描系统可用串口
private slots:
    void on_btnBrowsePath_clicked(); // 浏览按钮槽函数
};

#endif // SETTINGFORM_H
