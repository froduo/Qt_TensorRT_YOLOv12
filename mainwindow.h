#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include "cameracontroller.h"
#include "grabthread.h"
#include "inferthread.h"
#include "serialmanager.h"
#include "networkmanager.h"
#include "app_config.h"
#include "settingform.h"
#include"logger.h"

// 1. 声明 Ui 命名空间
namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onOpen();
    void onCloseCamera();
    void onOpenImage();
    void updateImage(QImage img, float inferTimeMs, float fps,  std::vector<Detection> results);
    void onCameraDisconnected();
    void onEngineLoadFailed(QString msg);
    void onSetParams();
private:
    // 2. 定义 ui 指针
    Ui::MainWindow *ui;

    CameraController camera;
    GrabThread* grabThread;
    InferThread* inferThread;
    void stopGrabbing();

    QLabel *statusLabel;    // 相机状态标签
    QLabel *timeLabel;      // 时间显示标签
    QTimer *sysTimer;       // 系统时钟定时器

    void updateCameraStatus(bool connected); // 封装状态更新逻辑
    // private 变量增加
    SerialManager *serialMgr;
    NetworkManager *networkMgr;

    QLabel *netLed;    // 网络状态灯
    QLabel *serialLed; // 串口状态灯

    // 辅助函数
    void setLedStatus(QLabel* led, bool online);

    AppConfig m_config;
    void applySettings(); // 应用配置到各个模块

private slots:
    void onUpdateSystemTime(); // 更新时间槽函数
    void showAbout();
    void on_actionversion_triggered();
};

#endif // MAINWINDOW_H
