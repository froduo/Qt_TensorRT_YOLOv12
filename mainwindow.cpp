#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QTimer>
#include <QBoxLayout>
#include <QCoreApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    LOG_INFO("Software starting...");
    m_config.load();
    LOG_INFO("Config loaded.");
    QString title = QString("YOLOv12 Sensor Core [%1]").arg(APP_VERSION_STR);
    this->setWindowTitle(title);

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

    timeLabel = new QLabel(this);
    timeLabel->setStyleSheet("color: #7ad7ff; font-family: 'Consolas'; font-size: 14px; padding: 0 10px; border-left: 1px solid #2e3a51;");

    ui->statusbar->addPermanentWidget(netTitle);
    ui->statusbar->addPermanentWidget(netLed);
    ui->statusbar->addPermanentWidget(new QLabel("  "));

    ui->statusbar->addPermanentWidget(serTitle);
    ui->statusbar->addPermanentWidget(serialLed);
    ui->statusbar->addPermanentWidget(new QLabel("  "));

    ui->statusbar->addPermanentWidget(statusLabel);
    ui->statusbar->addPermanentWidget(timeLabel);

    ui->statusbar->setStyleSheet("QStatusBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #151d2e, stop:1 #0f172a); border-top: 1px solid #2e4a6a; }");

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
    if(!inferThread->setEngine(m_config.enginePath)) {
        LOG_WARN(QString("Config engine path failed, trying default: ./model/yolo12n_trt10_x86.engine"));
        if(!inferThread->setEngine("./model/yolo12n_trt10_x86.engine")) {
            QTimer::singleShot(100, this, [=](){
                QMessageBox::critical(this,"Error","Model load failed");
            });
            ui->label_model->setText("MODEL: FAILED");
            ui->label_model->setStyleSheet("color: #FFCDD2; font-family: 'Consolas'; font-size: 11px; font-weight: 600; text-shadow: 0 0 3px #C62828, 0 0 6px #EF5350, 0 0 10px #E57373;");
        } else {
            inferThread->start(QThread::HighPriority);
            ui->label_model->setText("MODEL: LOADED");
            ui->label_model->setStyleSheet("color: #81C784; font-family: 'Consolas'; font-size: 11px; font-weight: 600; text-shadow: 0 0 3px #2E7D32, 0 0 6px #4CAF50, 0 0 10px #66BB6A;");
        }
    } else {
        inferThread->start(QThread::HighPriority);
        ui->label_model->setText("MODEL: LOADED");
        ui->label_model->setStyleSheet("color: #81C784; font-family: 'Consolas'; font-size: 11px; font-weight: 600; text-shadow: 0 0 3px #2E7D32, 0 0 6px #4CAF50, 0 0 10px #66BB6A;");
    }

    sysTimer = new QTimer(this);
    connect(sysTimer, &QTimer::timeout, this, &MainWindow::onUpdateSystemTime);
    sysTimer->start(1000);
    onUpdateSystemTime();

    serialMgr->openPort(m_config.serialPort, m_config.baudRate);
    networkMgr->connectToServer(m_config.netIp, m_config.netPort);
}

MainWindow::~MainWindow()
{
    stopGrabbing();
    if(inferThread) {
        inferThread->requestInterruption();
        inferThread->quit();
        inferThread->wait();
    }
    delete ui;
}

void MainWindow::stopGrabbing()
{
    if(grabThread)
    {
        grabThread->stop();
        grabThread->wait();
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
    ui->btnClose->setEnabled(false);
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

    connect(grabThread, &GrabThread::sendFrame,
            inferThread, &InferThread::setFrame,
            Qt::QueuedConnection);

    grabThread->start(QThread::HighestPriority);

    ui->btnOpen->setEnabled(false);
    ui->btnClose->setEnabled(true);

    ui->label_exp->setText(QString("EXP: %1 us").arg(static_cast<int>(m_config.exposure)));
    ui->label_exp->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 11px; font-weight: 500; text-shadow: 0 0 3px #4CAF50, 0 0 6px #81C784, 0 0 10px #66BB6A;");
    ui->label_gain->setText(QString("GAIN: %1").arg(m_config.gain));
    ui->label_gain->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 11px; font-weight: 500; text-shadow: 0 0 3px #4CAF50, 0 0 6px #81C784, 0 0 10px #66BB6A;");
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
    if(img.isNull()) return;
    if(!m_imageView || m_imageView->size().isEmpty()) return;

    if(this->isMinimized()) return;

    m_imageView->setImage(img);

    ui->label_fps->setText(QString("FPS: %1").arg(QString::number(fps, 'f', 1)));
    ui->label_fps->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600; text-shadow: 0 0 3px #4CAF50, 0 0 6px #81C784, 0 0 10px #66BB6A;");
    ui->label_inferTime->setText(QString("TIME: %1ms").arg(QString::number(inferTimeMs, 'f', 1)));
    ui->label_inferTime->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600; text-shadow: 0 0 3px #4CAF50, 0 0 6px #81C784, 0 0 10px #66BB6A;");
    ui->label_detCount->setText(QString("DET: %1").arg(results.size()));
    ui->label_detCount->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600; text-shadow: 0 0 3px #4CAF50, 0 0 6px #81C784, 0 0 10px #66BB6A;");

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
    QMessageBox::critical(this,"Error","Camera disconnected!");

    ui->btnOpen->setEnabled(true);
    ui->btnClose->setEnabled(false);
}

void MainWindow::onEngineLoadFailed(QString msg)
{
    LOG_ERR("TensorRT Engine Load Failed: " + msg);
    QMessageBox::critical(this, "TensorRT Engine Error", msg);
    ui->label_model->setText("MODEL: FAILED");
    ui->label_model->setStyleSheet("color: #FFCDD2; font-family: 'Consolas'; font-size: 11px; font-weight: 600; text-shadow: 0 0 3px #C62828, 0 0 6px #EF5350, 0 0 10px #E57373;");
}

void MainWindow::onSetParams()
{
    settingForm dlg(m_config, this);
    int res = dlg.exec();
    if (res == QDialog::Accepted)
    {
        QString oldPath = m_config.enginePath;
        m_config = dlg.getUpdatedConfig();
        m_config.save();
        LOG_INFO("System parameters updated and saved to config.ini");

        if(camera.isOpen()){
            camera.setExposureTime(m_config.exposure);
            camera.setGain(m_config.gain);
            ui->label_exp->setText(QString("EXP: %1 us").arg(static_cast<int>(m_config.exposure)));
            ui->label_exp->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 11px; font-weight: 500; text-shadow: 0 0 3px #4CAF50, 0 0 6px #81C784, 0 0 10px #66BB6A;");
            ui->label_gain->setText(QString("GAIN: %1").arg(m_config.gain));
            ui->label_gain->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 11px; font-weight: 500; text-shadow: 0 0 3px #4CAF50, 0 0 6px #81C784, 0 0 10px #66BB6A;");
        }

        serialMgr->closePort();
        serialMgr->openPort(m_config.serialPort, m_config.baudRate);

        networkMgr->disconnect();
        networkMgr->connectToServer(m_config.netIp, m_config.netPort);

        if (oldPath != m_config.enginePath) {
            inferThread->stop();
            inferThread->wait();
            if(inferThread->setEngine(m_config.enginePath)) {
                inferThread->start();
                ui->statusbar->showMessage("推理引擎已重新加载", 3000);
                ui->label_model->setText("MODEL: LOADED");
                ui->label_model->setStyleSheet("color: #81C784; font-family: 'Consolas'; font-size: 11px; font-weight: 600; text-shadow: 0 0 3px #2E7D32, 0 0 6px #4CAF50, 0 0 10px #66BB6A;");
            } else {
                ui->label_model->setText("MODEL: FAILED");
                ui->label_model->setStyleSheet("color: #FFCDD2; font-family: 'Consolas'; font-size: 11px; font-weight: 600; text-shadow: 0 0 3px #C62828, 0 0 6px #EF5350, 0 0 10px #E57373;");
            }
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
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    timeLabel->setText(currentTime);
}

void MainWindow::updateCameraStatus(bool connected)
{
    if(connected)
    {
        statusLabel->setText("CAM: ONLINE");
        statusLabel->setStyleSheet("color: #8dffbf; font-family: 'Consolas'; font-weight: bold; padding: 0 15px;");
        ui->label_camStatus->setText("CAM: ONLINE");
        ui->label_camStatus->setStyleSheet("color: #81C784; font-family: 'Consolas'; font-size: 11px; font-weight: bold; text-shadow: 0 0 3px #2E7D32, 0 0 6px #4CAF50, 0 0 10px #66BB6A;");
    }
    else
    {
        statusLabel->setText("CAM: OFFLINE");
        statusLabel->setStyleSheet("color: #FFCDD2; font-family: 'Consolas'; font-weight: bold; padding: 0 15px;");
        ui->label_camStatus->setText("CAM: OFFLINE");
        ui->label_camStatus->setStyleSheet("color: #FFCDD2; font-family: 'Consolas'; font-size: 11px; font-weight: bold; text-shadow: 0 0 3px #C62828, 0 0 6px #EF5350, 0 0 10px #E57373;");
        ui->label_fps->setText("FPS: --");
        ui->label_fps->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600; text-shadow: 0 0 3px #4CAF50, 0 0 6px #81C784, 0 0 10px #66BB6A;");
        ui->label_inferTime->setText("TIME: --ms");
        ui->label_inferTime->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600; text-shadow: 0 0 3px #4CAF50, 0 0 6px #81C784, 0 0 10px #66BB6A;");
        ui->label_detCount->setText("DET: --");
        ui->label_detCount->setStyleSheet("color: #E8F5E9; font-family: 'Consolas'; font-size: 12px; font-weight: 600; text-shadow: 0 0 3px #4CAF50, 0 0 6px #81C784, 0 0 10px #66BB6A;");
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
