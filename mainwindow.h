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
    void updateImage2(QImage img, float inferTimeMs, float fps, std::vector<Detection> results); // 相机2
    // ⭐ 原始帧显示（推理禁用时使用）
    void onRawFrame(QImage img, int cameraId);
    void onRawFrame2(QImage img, int cameraId);
    void onCameraDisconnected();
    void onCameraDisconnected2(); // 相机2掉线
    void onEngineLoadFailed(QString msg);
    void onSetParams();
    void openOfflineVerify();
private:
    // 2. 定义 ui 指针
    Ui::MainWindow *ui;

    // 相机1
    CameraController camera;
    GrabThread* grabThread;
    // 相机2
    CameraController camera2;
    GrabThread* grabThread2{nullptr};

    InferThread* inferThread;
    InferThread* inferThread2{nullptr}; // ⭐ 相机2独立推理线程
    void stopGrabbing();
    void stopGrabbing2(); // 停止相机2抓取

    QLabel *statusLabel;    // 相机状态标签
    QLabel *statusLabel2;   // 相机2状态标签
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

    // ⭐ 自动重连相关 - 相机1
    QTimer *m_reconnectTimer{nullptr}; // 重连定时器
    int     m_reconnectAttempts{0};    // 重连尝试次数
    static const int MAX_RECONNECT_ATTEMPTS = 60; // 最多尝试60次（约5分钟）
    static const int RECONNECT_INTERVAL_MS = 5000; // 每5秒尝试一次

    // ⭐ 自动重连相关 - 相机2
    QTimer *m_reconnectTimer2{nullptr};
    int     m_reconnectAttempts2{0};

    void updateCameraStatus(bool connected); // 封装状态更新逻辑
    void updateCameraStatus2(bool connected); // 相机2状态更新

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
    ImageView* m_imageView2 {nullptr}; // 相机2显示

private:
    void saveInferenceImage(const QImage &img, const std::vector<Detection> &results, int cameraId = 1);
    void updateDiskSpaceStatus();
    void initSaveWorker(); // 初始化后台保存线程

private slots:
    void onUpdateSystemTime(); // 更新时间槽函数
    void showAbout();
    void on_actionversion_triggered();
    void onGrabThreadDeviceLost(); // ⭐ 抓图线程检测到掉线
    void onGrabThreadDeviceLost2(); // 相机2掉线
    void onTryReconnect();         // ⭐ 尝试重连
    void onTryReconnect2();        // 相机2重连
};

#endif // MAINWINDOW_H
