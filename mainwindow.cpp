#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QTimer>
#include <QThread>
#include <QBoxLayout>
#include <QCoreApplication>
#include <QStorageInfo>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <opencv2/opencv.hpp>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    
    QString stylePath = QCoreApplication::applicationDirPath() + "/style.qss";
    QFile styleFile(stylePath);
    if (!styleFile.exists()) {
        stylePath = QCoreApplication::applicationDirPath() + "/../style.qss";
        styleFile.setFileName(stylePath);
    }
    if (styleFile.open(QFile::ReadOnly)) {
        QString styleSheet = QLatin1String(styleFile.readAll());
        this->setStyleSheet(styleSheet);
        styleFile.close();
        LOG_INFO("Stylesheet loaded: " + stylePath);
    } else {
        LOG_WARN("Stylesheet not found: " + stylePath);
    }
    
    LOG_INFO("Software starting...");
    m_config.load();
    LOG_INFO("Config loaded.");
    this->setWindowTitle(m_config.windowTitle);

    LOG_INFO("Software starting... Version: " + QString(APP_VERSION_STR));
    grabThread = nullptr;
    inferThread = new InferThread(this);
    serialMgr = new SerialManager(this);
    networkMgr = new NetworkManager(this);

    QGraphicsView* oldView = ui->imageView;
    if (oldView) {
        m_imageView = new ImageView(this);
        m_imageView->setObjectName("imageView");
        m_imageView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_imageView->setStyleSheet("background-color: #050812; border: 2px solid #4a6fa5; border-radius: 12px;");
        
        QBoxLayout* parentLayout = qobject_cast<QBoxLayout*>(oldView->parentWidget()->layout());
        if (parentLayout) {
            int idx = parentLayout->indexOf(oldView);
            if (idx >= 0) {
                parentLayout->removeWidget(oldView);
                parentLayout->insertWidget(idx, m_imageView);
            }
        }
        delete oldView;
    }

    QString titleStyle = "color: #7ad7ff; font-family: 'Consolas'; font-weight: bold; margin-left: 10px;";

    QLabel* netTitle = new QLabel("NET:", this);
    netTitle->setStyleSheet(titleStyle);
    netLed = new QLabel(this);
    setLedStatus(netLed, false);

    QLabel* serTitle = new QLabel("COM:", this);
    serTitle->setStyleSheet(titleStyle);
    serialLed = new QLabel(this);
    setLedStatus(serialLed, false);

    statusLabel = new QLabel("CAM: OFFLINE", this);
    statusLabel->setStyleSheet("color: #ff9fb1; font-family: 'Consolas'; font-weight: bold; padding: 0 15px;");

    // 运行时长标签（左下角）
    runTimeLabel = new QLabel("RUN: 00:00:00", this);
    runTimeLabel->setStyleSheet("color: #ffd54f; font-family: 'Consolas'; font-size: 13px; font-weight: bold; padding: 0 10px; border-right: 1px solid #2e3a51;");

    // 磁盘空间状态标签
    diskLabel = new QLabel("DISK: --", this);
    diskLabel->setStyleSheet("color: #7ad7ff; font-family: 'Consolas'; font-size: 13px; font-weight: bold; padding: 0 10px; border-right: 1px solid #2e3a51;");

    timeLabel = new QLabel(this);
    timeLabel->setStyleSheet("color: #7ad7ff; font-family: 'Consolas'; font-size: 14px; padding: 0 10px; border-left: 1px solid #2e3a51;");

    // 运行时长放在状态栏最左侧（先添加的靠左）
    ui->statusbar->addPermanentWidget(runTimeLabel);
    ui->statusbar->addPermanentWidget(diskLabel);

    ui->statusbar->addPermanentWidget(netTitle);
    ui->statusbar->addPermanentWidget(netLed);
    ui->statusbar->addPermanentWidget(new QLabel("  "));

    ui->statusbar->addPermanentWidget(serTitle);
    ui->statusbar->addPermanentWidget(serialLed);
    ui->statusbar->addPermanentWidget(new QLabel("  "));

    ui->statusbar->addPermanentWidget(statusLabel);
    ui->statusbar->addPermanentWidget(timeLabel);

    ui->statusbar->setStyleSheet("QStatusBar { background-color: #151d2e; border-top: 1px solid #2e4a6a; }");

    connect(inferThread, &InferThread::engineLoadFailed, this, &MainWindow::onEngineLoadFailed);
    connect(inferThread, &InferThread::sendResult, this, &MainWindow::updateImage);
    connect(&camera, &CameraController::deviceDisconnected, this, &MainWindow::onCameraDisconnected);

    connect(ui->btnOpen, &QPushButton::clicked, this, &MainWindow::onOpen);
    connect(ui->btnClose, &QPushButton::clicked, this, &MainWindow::onCloseCamera);
    connect(ui->actionOfflineInfer, &QAction::triggered, this, &MainWindow::onOpenImage);
    connect(ui->actionSysConfig, &QAction::triggered, this, &MainWindow::onSetParams);
    connect(ui->actionOfflineVerify, &QAction::triggered, this, &MainWindow::openOfflineVerify);

    connect(networkMgr, &NetworkManager::statusChanged, this, [=](bool connected){
        setLedStatus(netLed, connected);
    });
    connect(serialMgr, &SerialManager::statusChanged, this, [=](bool connected){
        setLedStatus(serialLed, connected);
    });
    connect(networkMgr, &NetworkManager::commandReceived, this, [=](QString cmd){
        qDebug() << "[Network Log] Received Command:" << cmd;
        ui->statusbar->showMessage("收到指令: " + cmd, 2000);
    });

    ui->btnClose->setEnabled(false);
    setLedStatus(netLed, false);
    setLedStatus(serialLed, false);

    LOG_INFO(QString("Loading TensorRT engine from config: %1").arg(m_config.enginePath));
    if(!inferThread->setEngine(m_config.enginePath, m_config.classesPath)) {
        LOG_WARN(QString("Config engine path failed, trying default: ./model/yolo12n_trt10_x86.engine"));
        if(!inferThread->setEngine("./model/yolo12n_trt10_x86.engine", "./model/coco.yaml")) {
            QTimer::singleShot(100, this, [=](){
                QMessageBox::critical(this,"Error","Model load failed");
            });
            ui->label_model->setText("MODEL: FAILED");
            ui->label_model->setStyleSheet("color: #FFCDD2; font-family: 'Consolas'; font-size: 11px; font-weight: 600;");
        } else {
            inferThread->start(QThread::HighPriority);
            ui->label_model->setText("MODEL: LOADED");
            ui->label_model->setStyleSheet("color: #81C784; font-family: 'Consolas'; font-size: 11px; font-weight: 600;");
        }
    } else {
        inferThread->start(QThread::HighPriority);
        ui->label_model->setText("MODEL: LOADED");
        ui->label_model->setStyleSheet("color: #81C784; font-family: 'Consolas'; font-size: 11px; font-weight: 600;");
    }

    // 启动运行计时器
    m_elapsedTimer.start();

    sysTimer = new QTimer(this);
    connect(sysTimer, &QTimer::timeout, this, &MainWindow::onUpdateSystemTime);
    sysTimer->start(1000);
    onUpdateSystemTime();

    // 磁盘空间监控定时器，每3秒更新一次
    m_diskTimer = new QTimer(this);
    connect(m_diskTimer, &QTimer::timeout, this, &MainWindow::updateDiskSpaceStatus);
    m_diskTimer->start(3000);
    updateDiskSpaceStatus();

    serialMgr->openPort(m_config.serialPort, m_config.baudRate);
    networkMgr->connectToServer(m_config.netIp, m_config.netPort);

    // 初始化后台图像保存线程
    initSaveWorker();
}

MainWindow::~MainWindow()
{
    stopGrabbing();
    if(inferThread) {
        inferThread->requestInterruption();
        inferThread->quit();
        inferThread->wait();
    }

    // 清理后台保存线程
    if (m_saveThread) {
        m_saveThread->quit();
        m_saveThread->wait(2000);
        delete m_saveWorker;
        m_saveWorker = nullptr;
        delete m_saveThread;
        m_saveThread = nullptr;
    }

    delete ui;
}

void MainWindow::stopGrabbing()
{
    if(grabThread)
    {
        grabThread->stop();
        // 使用超时等待，避免 UI 线程长时间阻塞
        if(!grabThread->wait(2000)) {
            LOG_WARN("GrabThread did not stop within 2s, forcing termination...");
            grabThread->terminate();
            grabThread->wait(500);
        }
        delete grabThread;
        grabThread = nullptr;
    }

    if(camera.isOpen())
    {
        camera.closeCamera();
    }
    updateCameraStatus(false);
}

void MainWindow::onCloseCamera()
{
    stopGrabbing();

    if (m_imageView) {
        m_imageView->setImage(QImage());
    }
    ui->btnOpen->setEnabled(true);
    // btnOpen 恢复为默认蓝色样式（移除禁用样式覆盖）
    ui->btnOpen->setStyleSheet("");

    ui->btnClose->setEnabled(false);
    // btnClose 禁用时显示为灰色，与激活状态的红色明显区分
    ui->btnClose->setStyleSheet(
        "QPushButton {"
        "   background-color: #3a3545;"
        "   border: 1px solid #5a4a6a;"
        "   border-radius: 8px;"
        "   color: #7a7a8a;"
        "   padding: 10px 16px;"
        "   font-weight: 600;"
        "   font-size: 12px;"
        "   min-height: 38px;"
        "}"
    );
}

void MainWindow::onOpen()
{
    LOG_INFO("Attempting to open camera...");
    if(grabThread && camera.isOpen()) {
        QMessageBox::information(this, "Info", "Camera is already running.");
        return;
    }
    if(grabThread || camera.isOpen()) {
        LOG_WARN("Inconsistent camera state detected, cleaning up...");
        stopGrabbing();
    }
    std::vector<MV_CC_DEVICE_INFO*> list;
    if(!camera.enumDevices(list) || list.empty())
    {
        QMessageBox::critical(this,"Error","No camera found");
        return;
    }

    int targetIdx = -1;
    for(size_t i = 0; i < list.size(); ++i) {
        QString currentSN = QString((char*)list[i]->SpecialInfo.stGigEInfo.chSerialNumber);
        if(currentSN == m_config.cameraSN) {
            targetIdx = static_cast<int>(i);
            break;
        }
        qDebug()<<"currentSN:"<<currentSN;
    }

    if(targetIdx == -1) {
        QMessageBox::warning(this, "错误", "未找到指定序列号的相机，尝试打开首选设备");
        targetIdx = 0;
    }

    if(!camera.openCamera(targetIdx))
    {
        updateCameraStatus(false);
        LOG_ERR("Failed to open camera index: " + QString::number(targetIdx));
        QMessageBox::critical(this,"Error","Open camera failed");
        return;
    }
    LOG_INFO(QString("Camera opened successfully. SN: %1").arg(m_config.cameraSN));
    updateCameraStatus(true);

    camera.setExposureTime(m_config.exposure);
    camera.setGain(m_config.gain);

    camera.startGrabbing();
    grabThread = new GrabThread(&camera);

    // ⭐ 设置目标序列号用于重连
    camera.setTargetSN(m_config.cameraSN);

    // ⭐ 连接抓图线程的掉线信号
    connect(grabThread, &GrabThread::deviceLost,
            this, &MainWindow::onGrabThreadDeviceLost);

    // ⭐ 使用 DirectConnection：InferThread 没有事件循环，QueuedConnection 无法工作
    // onFrameReceived 仅将帧入队（O(1)操作），不会阻塞 GrabThread 的帧捕获循环
    // InferThread 内部使用帧队列解耦，确保生产和消费互不阻塞
    connect(grabThread, &GrabThread::sendFrame,
            inferThread, &InferThread::onFrameReceived,
            Qt::DirectConnection);

    grabThread->start(QThread::HighestPriority);

    ui->btnOpen->setEnabled(false);
    // btnOpen 禁用时显示为灰色
    ui->btnOpen->setStyleSheet(
        "QPushButton {"
        "   background-color: #3a3545;"
        "   border: 1px solid #5a4a6a;"
        "   border-radius: 8px;"
        "   color: #7a7a8a;"
        "   padding: 10px 16px;"
        "   font-weight: 600;"
        "   font-size: 12px;"
        "   min-height: 38px;"
        "}"
    );

    ui->btnClose->setEnabled(true);
    // btnClose 激活时显示为醒目的红色，与禁用灰色明显区分
    ui->btnClose->setStyleSheet(
        "QPushButton {"
        "   background-color: #8b1a2b;"
        "   border: 2px solid #e04060;"
        "   border-radius: 8px;"
        "   color: #ffffff;"
        "   padding: 10px 16px;"
        "   font-weight: 700;"
        "   font-size: 13px;"
        "   min-height: 38px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #a02030;"
        "   border-color: #ff5070;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #5a0a1a;"
        "}"
    );

    ui->label_exp->setText(QString("EXP: %1 us").arg(static_cast<int>(m_config.exposure)));
    ui->label_exp->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 11px; font-weight: 500;");
    ui->label_gain->setText(QString("GAIN: %1").arg(m_config.gain));
    ui->label_gain->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 11px; font-weight: 500;");
}

void MainWindow::onOpenImage()
{
    QString file = QFileDialog::getOpenFileName(
        this,
        "选择图片文件",
        QCoreApplication::applicationDirPath(),
        "图像文件 (*.jpg *.png *.bmp);;所有文件 (*)",
        nullptr,
        QFileDialog::DontResolveSymlinks
    );
    
    if(file.isEmpty()) return;

    LOG_INFO(QString("Offline inference image: %1").arg(file));

    cv::Mat img = cv::imread(file.toLocal8Bit().toStdString());
    if(img.empty())
    {
        LOG_ERR(QString("Failed to load image: %1").arg(file));
        QMessageBox::critical(this,"Error","Failed to load image");
        return;
    }

    if(grabThread)
    {
        onCloseCamera();
    }

    inferThread->setFrame(img);

    ui->statusbar->showMessage("正在进行离线推理测试...");

    inferThread->setFrame(img);
    QCoreApplication::processEvents();
    QThread::msleep(33);

    LOG_INFO(QString("Offline inference completed, image: %1").arg(file));
    ui->statusbar->showMessage("离线推理完成", 3000);
}

void MainWindow::updateImage(QImage img, float inferTimeMs, float fps, std::vector<Detection> results)
{
    if(img.isNull()) {
        LOG_WARN("[MainWindow] updateImage received null image");
        return;
    }
    if(!m_imageView || m_imageView->size().isEmpty()) {
        LOG_WARN("[MainWindow] updateImage: imageView not ready, size empty");
        return;
    }

    if(this->isMinimized()) {
        // ⭐ 窗口最小化时不更新图像，但记录日志
        static bool minimizedLogged = false;
        if (!minimizedLogged) {
            LOG_INFO("[MainWindow] Window is minimized, skipping image update");
            minimizedLogged = true;
        }
        return;
    }

    // ⭐ 关键日志：每30秒记录一次图像更新状态
    static QElapsedTimer updateLogTimer;
    if (!updateLogTimer.isValid()) updateLogTimer.start();
    static int updateCount = 0;
    updateCount++;
    if (updateCount % 300 == 0 || updateLogTimer.elapsed() >= 30000) {
        LOG_INFO(QString("[MainWindow] updateImage #%1: img=%2x%3, fps=%4, infer=%5ms, det=%6")
                 .arg(updateCount)
                 .arg(img.width()).arg(img.height())
                 .arg(QString::number(fps, 'f', 1))
                 .arg(QString::number(inferTimeMs, 'f', 1))
                 .arg(results.size()));
        updateLogTimer.restart();
    }

    m_imageView->setImage(img);

    ui->label_fps->setText(QString("FPS: %1").arg(QString::number(fps, 'f', 1)));
    ui->label_fps->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600;");
    ui->label_inferTime->setText(QString("TIME: %1ms").arg(QString::number(inferTimeMs, 'f', 1)));
    ui->label_inferTime->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600;");
    ui->label_detCount->setText(QString("DET: %1").arg(results.size()));
    ui->label_detCount->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600;");

    // 保存推理图像
    saveInferenceImage(img, results);

    if(!results.empty()) {
        networkMgr->sendDetectionResults(results);
    }

    if(!results.empty() && serialMgr->isOpen()) {
        int frameCenterX = 640 / 2;
        int frameCenterY = 640 / 2;
        int detCenterX = results[0].x + results[0].w / 2;
        int detCenterY = results[0].y + results[0].h / 2;
        serialMgr->controlGimbal(detCenterX - frameCenterX, detCenterY - frameCenterY);
    }
}

void MainWindow::onCameraDisconnected()
{
    LOG_WARN("Camera device disconnected unexpectedly!");
    stopGrabbing();
    updateCameraStatus(false);

    // ⭐ 启动自动重连
    ui->statusbar->showMessage("相机掉线，正在尝试自动重连...");
    m_reconnectAttempts = 0;
    if (!m_reconnectTimer) {
        m_reconnectTimer = new QTimer(this);
        connect(m_reconnectTimer, &QTimer::timeout, this, &MainWindow::onTryReconnect);
    }
    m_reconnectTimer->start(RECONNECT_INTERVAL_MS);
    // 立即尝试一次
    onTryReconnect();
}

// ⭐ 抓图线程检测到掉线（与 deviceDisconnected 信号等效）
void MainWindow::onGrabThreadDeviceLost()
{
    LOG_WARN("GrabThread detected device lost, initiating reconnect...");
    // 清理抓图线程
    if (grabThread) {
        grabThread->stop();
        grabThread->wait();
        delete grabThread;
        grabThread = nullptr;
    }
    if (camera.isOpen()) {
        camera.closeCamera();
    }
    updateCameraStatus(false);

    // 启动自动重连
    ui->statusbar->showMessage("相机掉线，正在尝试自动重连...");
    m_reconnectAttempts = 0;
    if (!m_reconnectTimer) {
        m_reconnectTimer = new QTimer(this);
        connect(m_reconnectTimer, &QTimer::timeout, this, &MainWindow::onTryReconnect);
    }
    m_reconnectTimer->start(RECONNECT_INTERVAL_MS);
    onTryReconnect();
}

// ⭐ 尝试重连
void MainWindow::onTryReconnect()
{
    m_reconnectAttempts++;
    LOG_INFO(QString("Reconnect attempt %1/%2...").arg(m_reconnectAttempts).arg(MAX_RECONNECT_ATTEMPTS));
    ui->statusbar->showMessage(QString("正在重连相机... (%1/%2)")
                                   .arg(m_reconnectAttempts).arg(MAX_RECONNECT_ATTEMPTS));

    if (camera.reconnect()) {
        // 重连成功
        LOG_INFO("Camera reconnected successfully!");
        m_reconnectTimer->stop();
        m_reconnectAttempts = 0;

        // 恢复参数设置
        camera.setExposureTime(m_config.exposure);
        camera.setGain(m_config.gain);

        // 重新创建 GrabThread
        grabThread = new GrabThread(&camera);
        connect(grabThread, &GrabThread::deviceLost,
                this, &MainWindow::onGrabThreadDeviceLost);
        // ⭐ 使用 DirectConnection：onFrameReceived 仅入队，不阻塞 GrabThread
        connect(grabThread, &GrabThread::sendFrame,
                inferThread, &InferThread::onFrameReceived,
                Qt::DirectConnection);
        grabThread->start(QThread::HighestPriority);

        updateCameraStatus(true);
        ui->statusbar->showMessage("相机重连成功", 3000);
        ui->btnOpen->setEnabled(false);
        ui->btnClose->setEnabled(true);
        return;
    }

    // 重连失败
    if (m_reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
        // 超过最大尝试次数，放弃
        m_reconnectTimer->stop();
        m_reconnectAttempts = 0;
        LOG_ERR("Max reconnect attempts reached, giving up.");
        ui->statusbar->showMessage("相机重连失败，请手动点击 START CAMERA", 5000);
        ui->btnOpen->setEnabled(true);
        ui->btnClose->setEnabled(false);
        QMessageBox::warning(this, "重连失败",
                             QString("已尝试 %1 次重连均失败，请检查相机连接后手动启动。").arg(MAX_RECONNECT_ATTEMPTS));
    }
}

void MainWindow::onEngineLoadFailed(QString msg)
{
    LOG_ERR("TensorRT Engine Load Failed: " + msg);
    QMessageBox::critical(this, "TensorRT Engine Error", msg);
    ui->label_model->setText("MODEL: FAILED");
    ui->label_model->setStyleSheet("color: #FFCDD2; font-family: 'Consolas'; font-size: 11px; font-weight: 600;");
}

void MainWindow::onSetParams()
{
    settingForm dlg(m_config, this);
    int res = dlg.exec();
    if (res == QDialog::Accepted)
    {
        QString oldEnginePath = m_config.enginePath;
        QString oldClassesPath = m_config.classesPath;
        m_config = dlg.getUpdatedConfig();
        m_config.save();
        LOG_INFO("System parameters updated and saved to config.ini");

        // 更新主界面标题
        this->setWindowTitle(m_config.windowTitle);

        if(camera.isOpen()){
            camera.setExposureTime(m_config.exposure);
            camera.setGain(m_config.gain);
            ui->label_exp->setText(QString("EXP: %1 us").arg(static_cast<int>(m_config.exposure)));
        ui->label_exp->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 11px; font-weight: 500;");
        ui->label_gain->setText(QString("GAIN: %1").arg(m_config.gain));
        ui->label_gain->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 11px; font-weight: 500;");
        }

        serialMgr->closePort();
        serialMgr->openPort(m_config.serialPort, m_config.baudRate);

        networkMgr->disconnect();
        networkMgr->connectToServer(m_config.netIp, m_config.netPort);

        if (oldEnginePath != m_config.enginePath) {
            inferThread->stop();
            inferThread->wait();
            if(inferThread->setEngine(m_config.enginePath, m_config.classesPath)) {
                inferThread->start();
                ui->statusbar->showMessage("推理引擎已重新加载", 3000);
                ui->label_model->setText("MODEL: LOADED");
                ui->label_model->setStyleSheet("color: #81C784; font-family: 'Consolas'; font-size: 11px; font-weight: 600;");
            } else {
                ui->label_model->setText("MODEL: FAILED");
                ui->label_model->setStyleSheet("color: #FFCDD2; font-family: 'Consolas'; font-size: 11px; font-weight: 600;");
            }
        } else if (oldClassesPath != m_config.classesPath) {
            inferThread->setClasses(m_config.classesPath);
            ui->statusbar->showMessage("类别配置已更新", 3000);
        }
        inferThread->setScoreThreshold(m_config.scoreThreshold);
        QMessageBox::information(this, "成功", "参数已保存并实时应用");
    }
}

void MainWindow::openOfflineVerify()
{
    OfflineVerifyForm dlg(this);
    dlg.exec();
}

void MainWindow::onUpdateSystemTime()
{
    // 更新系统时间
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    timeLabel->setText(currentTime);

    // 更新运行时长
    qint64 elapsedMs = m_elapsedTimer.elapsed();
    int hours   = static_cast<int>(elapsedMs / 3600000);
    int minutes = static_cast<int>((elapsedMs % 3600000) / 60000);
    int seconds = static_cast<int>((elapsedMs % 60000) / 1000);
    runTimeLabel->setText(QString("RUN: %1:%2:%3")
                              .arg(hours, 2, 10, QChar('0'))
                              .arg(minutes, 2, 10, QChar('0'))
                              .arg(seconds, 2, 10, QChar('0')));
}

void MainWindow::updateCameraStatus(bool connected)
{
    if(connected)
    {
        statusLabel->setText("CAM: ONLINE");
        statusLabel->setStyleSheet("color: #8dffbf; font-family: 'Consolas'; font-weight: bold; padding: 0 15px;");
        ui->label_camStatus->setText("CAM: ONLINE");
        ui->label_camStatus->setStyleSheet("color: #81C784; font-family: 'Consolas'; font-size: 11px; font-weight: bold;");
    }
    else
    {
        statusLabel->setText("CAM: OFFLINE");
        statusLabel->setStyleSheet("color: #FFCDD2; font-family: 'Consolas'; font-weight: bold; padding: 0 15px;");
        ui->label_camStatus->setText("CAM: OFFLINE");
        ui->label_camStatus->setStyleSheet("color: #FFCDD2; font-family: 'Consolas'; font-size: 11px; font-weight: bold;");
        ui->label_fps->setText("FPS: --");
        ui->label_fps->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600;");
        ui->label_inferTime->setText("TIME: --ms");
        ui->label_inferTime->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600;");
        ui->label_detCount->setText("DET: --");
        ui->label_detCount->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600;");
    }
}

void MainWindow::setLedStatus(QLabel* led, bool online)
{
    if(online)
    {
        led->setText("●");
        led->setStyleSheet("color: #8dffbf; font-weight: bold; font-size: 16px;");
    }
    else
    {
        led->setText("●");
        led->setStyleSheet("color: #ff9fb1; font-weight: bold; font-size: 16px;");
    }
}

void MainWindow::showAbout()
{
    QString info = QString("智能视觉监控系统\n\n"
                           "版本: %1\n"
                           "框架: YOLOv12 + TensorRT\n"
                           "Qt: %2\n"
                           "OpenCV: %3")
                       .arg(APP_VERSION_STR)
                       .arg(qVersion())
                       .arg(CV_VERSION);
    QMessageBox::about(this, "关于", info);
}

void MainWindow::on_actionversion_triggered()
{
    showAbout();
}

void MainWindow::saveInferenceImage(const QImage &img, const std::vector<Detection> &results)
{
    // 如果磁盘已满（<1G），不保存
    if (m_diskFull) {
        return;
    }

    bool hasDetection = !results.empty();
    bool shouldSave = false;

    if (hasDetection && m_config.saveNG) {
        shouldSave = true;
    } else if (!hasDetection && m_config.saveOK) {
        shouldSave = true;
    }

    if (!shouldSave) return;

    // 通过信号发送到后台线程保存，不阻塞 UI 线程
    if (m_saveWorker) {
        // 同步保存参数到后台线程
        m_saveWorker->setSaveParams(m_config.savePath, m_config.saveFormat, m_config.jpegQuality);
        m_saveWorker->setDiskFull(m_diskFull);
        // 发射信号，后台线程执行保存
        emit m_saveWorker->onSaveImage(img, hasDetection);
    }
}

void MainWindow::updateDiskSpaceStatus()
{
    QString savePath = m_config.savePath;
    if (savePath.isEmpty()) {
        savePath = QCoreApplication::applicationDirPath();
    }

    QStorageInfo storage(savePath);
    if (!storage.isValid() || !storage.isReady()) {
        diskLabel->setText("DISK: --");
        diskLabel->setStyleSheet("color: #7ad7ff; font-family: 'Consolas'; font-size: 13px; font-weight: bold; padding: 0 10px; border-right: 1px solid #2e3a51;");
        return;
    }

    // 计算剩余空间（字节）
    qint64 bytesFree = storage.bytesAvailable();
    qint64 bytesTotal = storage.bytesTotal();

    // 计算已用空间（当前保存目录占用的空间）
    qint64 bytesUsed = 0;
    QDir dir(m_config.savePath);
    if (dir.exists()) {
        QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &fi : files) {
            bytesUsed += fi.size();
        }
        // 递归统计子目录
        QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &di : dirs) {
            QDir subDir(di.absoluteFilePath());
            QFileInfoList subFiles = subDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &sfi : subFiles) {
                bytesUsed += sfi.size();
            }
        }
    }

    // 格式化显示
    double freeGB = bytesFree / (1024.0 * 1024.0 * 1024.0);
    double usedMB = bytesUsed / (1024.0 * 1024.0);
    double totalGB = bytesTotal / (1024.0 * 1024.0 * 1024.0);

    QString diskText = QString("DISK: %1G/%2G | USED: %3M")
                           .arg(QString::number(freeGB, 'f', 1))
                           .arg(QString::number(totalGB, 'f', 1))
                           .arg(QString::number(usedMB, 'f', 0));

    // 判断空间状态
    const qint64 WARN_THRESHOLD = 1LL * 1024 * 1024 * 1024; // 1GB
    const qint64 FULL_THRESHOLD = 100LL * 1024 * 1024;      // 100MB（几乎满）

    if (bytesFree < FULL_THRESHOLD) {
        // 红色 - 空间不足，禁止保存
        m_diskFull = true;
        diskLabel->setText(diskText + " | DISK FULL!");
        diskLabel->setStyleSheet("color: #FF1744; font-family: 'Consolas'; font-size: 13px; font-weight: bold; padding: 0 10px; border-right: 1px solid #2e3a51; background-color: #4a0000;");
    } else if (bytesFree < WARN_THRESHOLD) {
        // 黄色 - 空间不足1G，警告
        m_diskFull = false;
        diskLabel->setText(diskText + " | LOW SPACE!");
        diskLabel->setStyleSheet("color: #FFD600; font-family: 'Consolas'; font-size: 13px; font-weight: bold; padding: 0 10px; border-right: 1px solid #2e3a51; background-color: #3a3a00;");
    } else {
        // 正常 - 绿色
        m_diskFull = false;
        diskLabel->setText(diskText);
        diskLabel->setStyleSheet("color: #8dffbf; font-family: 'Consolas'; font-size: 13px; font-weight: bold; padding: 0 10px; border-right: 1px solid #2e3a51;");
    }

    // 同步磁盘满标志到后台保存线程
    if (m_saveWorker) {
        m_saveWorker->setDiskFull(m_diskFull);
    }
}

void MainWindow::initSaveWorker()
{
    m_saveWorker = new SaveImageWorker(); // 无 parent，将移入后台线程
    m_saveThread = new QThread(this);

    // 初始化保存参数
    m_saveWorker->setSaveParams(m_config.savePath, m_config.saveFormat, m_config.jpegQuality);
    m_saveWorker->setDiskFull(m_diskFull);

    // 将 worker 移入后台线程
    m_saveWorker->moveToThread(m_saveThread);

    // 线程结束时清理 worker
    connect(m_saveThread, &QThread::finished, m_saveWorker, &QObject::deleteLater);

    // 启动线程
    m_saveThread->start();

    LOG_INFO("[MainWindow] SaveImageWorker initialized in background thread");
}
