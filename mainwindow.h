#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QElapsedTimer>
#include <QStorageInfo>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include "cameracontroller.h"
#include "grabthread.h"
#include "inferthread.h"
#include "serialmanager.h"
#include "networkmanager.h"
#include "app_config.h"
#include "settingform.h"
#include "offlineverifyform.h"
#include "imageview.h"
#include "saveimageworker.h"
#include "logger.h"

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
    void openOfflineVerify();
private:
    // 2. 定义 ui 指针
    Ui::MainWindow *ui;

    CameraController camera;
    GrabThread* grabThread;
    InferThread* inferThread;
    void stopGrabbing();

    QLabel *statusLabel;    // 相机状态标签
    QLabel *timeLabel;      // 时间显示标签
    QLabel *runTimeLabel;   // 运行时长标签
    QLabel *diskLabel;      // 磁盘空间状态标签
    QTimer *sysTimer;       // 系统时钟定时器
    QElapsedTimer m_elapsedTimer; // 运行计时器
    QTimer *m_diskTimer;    // 磁盘空间监控定时器
    bool m_diskFull = false; // 磁盘空间不足标志（<1G时禁止保存）

    // 后台图像保存
    SaveImageWorker *m_saveWorker{nullptr};
    QThread *m_saveThread{nullptr};

    // ⭐ 自动重连相关
    QTimer *m_reconnectTimer{nullptr}; // 重连定时器
    int     m_reconnectAttempts{0};    // 重连尝试次数
    static const int MAX_RECONNECT_ATTEMPTS = 60; // 最多尝试60次（约5分钟）
    static const int RECONNECT_INTERVAL_MS = 5000; // 每5秒尝试一次

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

    // 主界面 ImageView（用于显示视频/推理结果，支持缩放拖拽）
    ImageView* m_imageView {nullptr};

private:
    void saveInferenceImage(const QImage &img, const std::vector<Detection> &results);
    void updateDiskSpaceStatus();
    void initSaveWorker(); // 初始化后台保存线程

private slots:
    void onUpdateSystemTime(); // 更新时间槽函数
    void showAbout();
    void on_actionversion_triggered();
    void onGrabThreadDeviceLost(); // ⭐ 抓图线程检测到掉线
    void onTryReconnect();         // ⭐ 尝试重连
};

#endif // MAINWINDOW_H
