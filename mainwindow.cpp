#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QFileDialog>
#include<qtimer.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // [0] 基础 UI 初始化
    ui->setupUi(this);
    // [1] 加载本地配置文件
    LOG_INFO("Software starting..."); // 记录启动
    m_config.load();
    LOG_INFO("Config loaded.");
    // 设置带日期版本号的窗口标题
    QString title = QString("YOLOv12 Sensor Core [%1]").arg(APP_VERSION_STR);
    this->setWindowTitle(title);

    LOG_INFO("Software starting... Version: " + QString(APP_VERSION_STR));
    // [2] 核心管理对象实例化 (仅分配内存)
    grabThread = nullptr;
    inferThread = new InferThread(this);
    serialMgr = new SerialManager(this);
    networkMgr = new NetworkManager(this);

    // [3] 状态栏 UI 组件创建与布局 (Permanent Widget)
    // --- [3] 状态栏 UI 增强设计 ---

    // 辅助样式定义
    QString titleStyle = "color: #00F0FF; font-family: 'Consolas'; font-weight: bold; margin-left: 10px;";
    QString valueStyle = "color: #FFFFFF; font-family: 'Consolas'; margin-left: 5px; margin-right: 5px;";

    // 3.1 网口区块 (NET)
    QLabel* netTitle = new QLabel("NET:", this);
    netTitle->setStyleSheet(titleStyle);
    netLed = new QLabel(this);
    setLedStatus(netLed, false); // 初始红色

    // 3.2 串口区块 (COM)
    QLabel* serTitle = new QLabel("COM:", this);
    serTitle->setStyleSheet(titleStyle);
    serialLed = new QLabel(this);
    setLedStatus(serialLed, false);

    // 3.3 相机区块 (CAM)
    statusLabel = new QLabel("CAM: OFFLINE", this);
    statusLabel->setStyleSheet("color: #FF3C00; font-family: 'Consolas'; font-weight: bold; padding: 0 15px;");

    // 3.4 时间区块 (TIME)
    timeLabel = new QLabel(this);
    timeLabel->setStyleSheet("color: #00F0FF; font-family: 'Consolas'; font-size: 14px; padding: 0 10px; border-left: 1px solid #2D3748;");

    // 3.5 按照逻辑顺序添加到状态栏
    ui->statusbar->addPermanentWidget(netTitle);
    ui->statusbar->addPermanentWidget(netLed);
    ui->statusbar->addPermanentWidget(new QLabel("  ")); // 间距

    ui->statusbar->addPermanentWidget(serTitle);
    ui->statusbar->addPermanentWidget(serialLed);
    ui->statusbar->addPermanentWidget(new QLabel("  ")); // 间距

    ui->statusbar->addPermanentWidget(statusLabel);
    ui->statusbar->addPermanentWidget(timeLabel);

    // 设置状态栏整体背景
    ui->statusbar->setStyleSheet("QStatusBar { background-color: #0D1117; border-top: 1px solid #00F0FF; }");

    // [4] 信号与槽连接 (必须放在 Open 操作之前，确保信号不丢失)

    // 4.1 核心推理与相机信号
    connect(inferThread, &InferThread::engineLoadFailed, this, &MainWindow::onEngineLoadFailed);
    connect(inferThread, &InferThread::sendResult, this, &MainWindow::updateImage);
    connect(&camera, &CameraController::deviceDisconnected, this, &MainWindow::onCameraDisconnected);

    // 4.2 按钮交互信号
    connect(ui->btnOpen, &QPushButton::clicked, this, &MainWindow::onOpen);
    connect(ui->btnClose, &QPushButton::clicked, this, &MainWindow::onCloseCamera);
    connect(ui->btnOpenImage, &QPushButton::clicked, this, &MainWindow::onOpenImage);
    connect(ui->btnSetParams, &QPushButton::clicked, this, &MainWindow::onSetParams);

    // 4.3 网络与串口通信信号
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

    // [5] 系统初始状态设置
    ui->btnClose->setEnabled(false);
    setLedStatus(netLed, false);
    setLedStatus(serialLed, false);

    // [6] 启动项 (加载、开启、连接)

    // 6.1 加载模型引擎
    if(!inferThread->setEngine("./model/yolo12n_trt10_x86.engine")) {
        QTimer::singleShot(100, this, [=](){
            QMessageBox::critical(this,"Error","Model load failed");
        });
    } else {
        inferThread->start(QThread::HighPriority);
    }

    // 6.2 启动系统时间定时器
    sysTimer = new QTimer(this);
    connect(sysTimer, &QTimer::timeout, this, &MainWindow::onUpdateSystemTime);
    sysTimer->start(1000);
    onUpdateSystemTime(); // 立即初始化时间

    // 6.3 尝试打开默认外设连接
    // 注意：这里的 openPort 会触发 statusChanged 信号，
    // 因为信号槽已经在第 [4] 步连好了，所以灯会正确变色
    serialMgr->openPort(m_config.serialPort, m_config.baudRate);
    networkMgr->connectToServer(m_config.netIp, m_config.netPort);
}

MainWindow::~MainWindow()
{
    stopGrabbing(); // 析构时确保线程停止
    if(inferThread) {
        inferThread->requestInterruption();
        inferThread->quit();
        inferThread->wait();
    }
    delete ui;
}

// ⭐ 封装停止逻辑，防止代码重复
void MainWindow::stopGrabbing()
{
    if(grabThread)
    {
        grabThread->stop(); // 设置标志位
        grabThread->wait(); // 等待线程安全退出
        delete grabThread;
        grabThread = nullptr;
    }

    if(camera.isOpen())
    {
        camera.closeCamera();
    }
    updateCameraStatus(false);
}

// ⭐ 实现关闭相机槽函数
void MainWindow::onCloseCamera()
{
    stopGrabbing();

    // UI 反馈
    ui->imageLabel->clear();
    ui->imageLabel->setText("Camera Closed");
    ui->btnOpen->setEnabled(true);
    ui->btnClose->setEnabled(false);
}

void MainWindow::onOpen()
{
    LOG_INFO("Attempting to open camera...");
    // 防止重复打开
    if(grabThread || camera.isOpen()) {
        QMessageBox::information(this, "Info", "Camera is already running.");
        return;
    }
    // 枚举设备
    std::vector<MV_CC_DEVICE_INFO*> list;
    if(!camera.enumDevices(list) || list.empty())
    {
        QMessageBox::critical(this,"Error","No camera found");
        return;
    }

    int targetIdx = -1;
    for(int i = 0; i < list.size(); ++i) {
        // 海康 SDK 中的序列号字段比较
        QString currentSN = QString((char*)list[i]->SpecialInfo.stGigEInfo.chSerialNumber);
        if(currentSN == m_config.cameraSN) {
            targetIdx = i;
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
        updateCameraStatus(false); // 确保状态正确
        LOG_ERR("Failed to open camera index: " + QString::number(targetIdx)); // 记录错误
        QMessageBox::critical(this,"Error","Open camera failed");
        return;
    }
    LOG_INFO(QString("Camera opened successfully. SN: %1").arg(m_config.cameraSN));
    updateCameraStatus(true);
    // ⭐ 相机打开成功后，应用一次界面上当前的数值
    // 否则相机会用默认参数跑
    camera.setExposureTime(m_config.exposure);
    camera.setGain(m_config.gain);

    camera.startGrabbing();
    grabThread = new GrabThread(&camera);

    connect(grabThread, &GrabThread::sendFrame,
            inferThread, &InferThread::setFrame,
            Qt::QueuedConnection);

    grabThread->start(QThread::HighestPriority);

    // 更新按钮状态
    ui->btnOpen->setEnabled(false);
    ui->btnClose->setEnabled(true);
}

void MainWindow::onOpenImage()
{
    QString file = QFileDialog::getOpenFileName(this, "Open Image", "", "Images (*.jpg *.png *.bmp)");
    if(file.isEmpty()) return;

    // 处理中文路径
    cv::Mat img = cv::imread(file.toLocal8Bit().toStdString());
    if(img.empty())
    {
        QMessageBox::critical(this,"Error","Failed to load image");
        return;
    }

    // ⭐ 如果相机正在运行，先停止
    if(grabThread)
    {
        onCloseCamera(); // 复用关闭逻辑
    }

    inferThread->setFrame(img);

    // 2. 开启 10 秒压力测试
    // 假设你的 inferThread 有一个模式可以持续处理同一张图
    // 或者我们直接在主循环里模拟发送

    QElapsedTimer timer;
    timer.start();

    // 提示用户正在测试
    ui->statusbar->showMessage("正在进行10秒静态稳定性测试...");

    while(timer.elapsed() < 10000) // 10000 毫秒 = 10 秒
    {
        // 将图片传给推理线程处理
        inferThread->setFrame(img);
        // 给 UI 刷新的机会，否则界面会卡死
        QCoreApplication::processEvents();
        // 控制一下速度，模拟 30FPS，避免把 CPU/GPU 跑满到发热降频
        QThread::msleep(33);
    }
    ui->statusbar->showMessage("测试完成", 3000);
}

void MainWindow::updateImage(QImage img, float inferTimeMs, float fps,  std::vector<Detection> results)
{
    // 1. UI 更新代码 ...
    if(img.isNull()) return; // 防止空图导致闪烁
    if(ui->imageLabel->size().isEmpty()) return;

    /// 如果界面最小化，停止渲染以节省资源
    if(this->isMinimized()) return;

    // 性能优化：根据图像大小动态选择缩放算法
    Qt::TransformationMode mode = Qt::FastTransformation;
    if (img.width() < 1000) {
        mode = Qt::SmoothTransformation; // 小图用平滑，大图用快速
    }

    ui->imageLabel->setPixmap(QPixmap::fromImage(img).scaled(
        ui->imageLabel->size(), Qt::KeepAspectRatio, mode));

    // 在状态栏临时显示推理信息
    QString telemetry = QString(">> INF: %1ms | FPS: %2 | DET: %3")
                            .arg(QString::number(inferTimeMs, 'f', 1))
                            .arg(QString::number(fps, 'f', 1))
                            .arg(results.size());

    ui->statusbar->showMessage(telemetry);

    // 2. 网口发送：传输所有检测到的目标数据
    if(!results.empty()) {
        networkMgr->sendDetectionResults(results);
    }

    // 3. 串口发送：云台追踪逻辑 (示例：追踪第一个检测到的目标)
    if(!results.empty()) {
        // 计算目标中心点相对于画面中心的偏移
        int frameCenterX = 640 / 2; // 假设输入 640
        int frameCenterY = 640 / 2;
        int detCenterX = results[0].x + results[0].w / 2;
        int detCenterY = results[0].y + results[0].h / 2;
        if (serialMgr->isOpen()) {
            serialMgr->controlGimbal(detCenterX - frameCenterX, detCenterY - frameCenterY);
        }
    }
}
void MainWindow::onCameraDisconnected()
{
    LOG_WARN("Camera device disconnected unexpectedly!"); // 记录告警
    // 相机断开处理
    stopGrabbing();
    updateCameraStatus(false);
    QMessageBox::critical(this,"Error","Camera disconnected!");

    ui->btnOpen->setEnabled(true);
    ui->btnClose->setEnabled(false);
}

void MainWindow::onEngineLoadFailed(QString msg)
{
    LOG_ERR("TensorRT Engine Load Failed: " + msg); // 记录核心故障
    QMessageBox::critical(this, "TensorRT Engine Error", msg);
}
// ⭐ 实现参数设置槽函数
void MainWindow::onSetParams()
{
    // 弹出配置对话框，传入当前配置
    settingForm dlg(m_config, this);
    int res = dlg.exec();
    if (res== QDialog::Accepted)
    {
        QString oldPath = m_config.enginePath;
        // 1. 获取界面上的新配置
        m_config = dlg.getUpdatedConfig();
        // 2. 保存到本地 config.ini
        m_config.save();
        LOG_INFO("System parameters updated and saved to config.ini"); // 记录操作
        // 3. 立即应用到运行中的模块

        // 更新相机参数（如果相机已打开）
        if(camera.isOpen()){
            camera.setExposureTime(m_config.exposure);
            camera.setGain(m_config.gain);
        }

        // 重新连接串口
        serialMgr->closePort();
        serialMgr->openPort(m_config.serialPort, m_config.baudRate);

        // 重新连接网口
        networkMgr->disconnect();
        networkMgr->connectToServer(m_config.netIp, m_config.netPort);

        if (oldPath != m_config.enginePath) {
            // 执行热重载
            inferThread->stop(); // 先停止旧线程
            inferThread->wait();
            if(inferThread->setEngine(m_config.enginePath)) {
                inferThread->start();
                ui->statusbar->showMessage("推理引擎已重新加载", 3000);
            }
        }
        // --- 应用新阈值到推理线程 ---
        // 假设你的 inferThread 有一个接口或者可以直接访问 yolo 引擎
        inferThread->setScoreThreshold(m_config.scoreThreshold);
        QMessageBox::information(this, "成功", "参数已保存并实时应用");
    }
    return;
}
// 更新系统时间
void MainWindow::onUpdateSystemTime()
{
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    timeLabel->setText(currentTime);
}

// 统一更新相机连接状态显示
void MainWindow::updateCameraStatus(bool connected)
{
    if(connected)
    {
        statusLabel->setText("CAM: ● ACTIVE");
        statusLabel->setStyleSheet("color: #00FFC2; font-family: 'Consolas'; font-weight: bold; padding: 0 15px;");
    }
    else
    {
        statusLabel->setText("CAM: ○ OFFLINE");
        statusLabel->setStyleSheet("color: #FF3C00; font-family: 'Consolas'; font-weight: bold; padding: 0 15px;");
    }
}
// 指示灯颜色切换函数
void MainWindow::setLedStatus(QLabel* led, bool online) {
    // 荧光绿 vs 亮橙红
    QString color = online ? "#00FFC2" : "#FF3C00";
    // 阴影颜色
    QString shadow = online ? "rgba(0, 255, 194, 0.4)" : "rgba(255, 60, 0, 0.4)";

    led->setFixedSize(12, 12);
    led->setStyleSheet(QString(
                           "background-color: %1; "
                           "border-radius: 6px; "
                           "border: 1px solid rgba(255, 255, 255, 0.3); "
                           ).arg(color));

    // 给父级或自身添加简单的提示，增加交互感
    led->setToolTip(online ? "LINK ACTIVE" : "LINK LOST");
}
void MainWindow::showAbout() {
    QString info = QString("智能视觉监控系统\n\n"
                           "软件版本: %1\n"
                           "编译日期: %2\n"
                           "技术支持: froduo")
                       .arg(APP_VERSION_STR)
                       .arg(__DATE__); // __DATE__ 是 C++ 内置宏，显示 Mmm dd yyyy 格式
    QMessageBox::about(this, "关于", info);
}

void MainWindow::on_actionversion_triggered()
{
    showAbout();
}

