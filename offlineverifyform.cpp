#include "offlineverifyform.h"
#include "ui_offlineverifyform.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QScrollBar>
#include <QMessageBox>
#include <QSettings>
#include <QDebug>
#include <QMainWindow>
#include <opencv2/dnn.hpp>
#include <cuda_runtime_api.h>

static const std::vector<std::string> kOfflineClassNames = {
    "person","bicycle","car","motorcycle","airplane","bus","train","truck",
    "boat","traffic light","fire hydrant","stop sign","parking meter","bench",
    "bird","cat","dog","horse","sheep","cow","elephant","bear","zebra","giraffe",
    "backpack","umbrella","handbag","tie","suitcase","frisbee","skis","snowboard",
    "sports ball","kite","baseball bat","baseball glove","skateboard","surfboard",
    "tennis racket","bottle","wine glass","cup","fork","knife","spoon","bowl",
    "banana","apple","sandwich","orange","broccoli","carrot","hot dog","pizza",
    "donut","cake","chair","couch","potted plant","bed","dining table","toilet",
    "tv","laptop","mouse","remote","keyboard","cell phone","microwave","oven",
    "toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear",
    "hair drier","toothbrush"
};

static cv::Scalar offlineClassColor(int classId)
{
    static const cv::Scalar palette[] = {
        cv::Scalar(56, 56, 255), cv::Scalar(151, 157, 255), cv::Scalar(31, 112, 255),
        cv::Scalar(29, 178, 255), cv::Scalar(49, 210, 207), cv::Scalar(10, 249, 72),
        cv::Scalar(23, 204, 146), cv::Scalar(134, 219, 61), cv::Scalar(52, 147, 26),
        cv::Scalar(187, 212, 0), cv::Scalar(168, 153, 44), cv::Scalar(255, 194, 0),
        cv::Scalar(147, 69, 52), cv::Scalar(255, 115, 100), cv::Scalar(236, 24, 0),
        cv::Scalar(255, 56, 132), cv::Scalar(133, 0, 82), cv::Scalar(255, 56, 203),
        cv::Scalar(200, 149, 255), cv::Scalar(199, 55, 255)
    };
    constexpr int paletteSize = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    if (classId < 0) return cv::Scalar(200, 200, 200);
    return palette[classId % paletteSize];
}

OfflineVerifyForm::OfflineVerifyForm(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::OfflineVerifyForm)
{
    ui->setupUi(this);
    this->setWindowTitle("离线验证");

    connect(ui->btnSelectModel, &QPushButton::clicked, this, &OfflineVerifyForm::handleSelectModel);
    connect(ui->btnSelectImage, &QPushButton::clicked, this, &OfflineVerifyForm::handleSelectImage);
    connect(ui->btnStartInfer, &QPushButton::clicked, this, &OfflineVerifyForm::handleStartInfer);
    connect(ui->btnEnvCheck, &QPushButton::clicked, this, &OfflineVerifyForm::handleEnvCheck);
    connect(ui->btnOnnxToEngine, &QPushButton::clicked, this, &OfflineVerifyForm::handleOnnxToEngine);
    connect(ui->btnExportLog, &QPushButton::clicked, this, &OfflineVerifyForm::handleExportLog);
    connect(ui->cmbDevice, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &OfflineVerifyForm::handleDeviceChanged);

    ImageView* imageView = new ImageView(this);
    imageView->setObjectName("imageView");
    QLayoutItem* oldItem = ui->mainLayout->itemAt(0);
    if (oldItem) {
        QWidget* oldWidget = oldItem->widget();
        if (oldWidget) {
            ui->mainLayout->removeWidget(oldWidget);
            delete oldWidget;
        }
    }
    ui->mainLayout->insertWidget(0, imageView, 3);

    QString lastModel = restoreLastModelPath();
    if (!lastModel.isEmpty()) {
        ui->editModelPath->setText(lastModel);
        m_lastModelDir = QFileInfo(lastModel).absolutePath();
    } else {
        m_lastModelDir = QDir::currentPath();
    }

    QString lastImage = restoreLastImagePath();
    if (!lastImage.isEmpty()) {
        ui->editImagePath->setText(lastImage);
        m_offlineImagePath = lastImage;
        m_offlineImage = cv::imread(lastImage.toLocal8Bit().toStdString());
        m_lastImageDir = QFileInfo(lastImage).absolutePath();
    } else {
        m_lastImageDir = QDir::currentPath();
    }
}

OfflineVerifyForm::~OfflineVerifyForm()
{
    if (m_offlineTrtYolo) {
        delete m_offlineTrtYolo;
        m_offlineTrtYolo = nullptr;
    }
    if (m_offlineLogger) {
        delete m_offlineLogger;
        m_offlineLogger = nullptr;
    }
    if (m_trtexec) {
        m_trtexec->kill();
        m_trtexec->waitForFinished(3000);
        delete m_trtexec;
        m_trtexec = nullptr;
    }
    delete ui;
}

void OfflineVerifyForm::appendLog(const QString& text)
{
    ui->txtLog->appendPlainText(text);
    QScrollBar* sb = ui->txtLog->verticalScrollBar();
    if (sb) sb->setValue(sb->maximum());
}

void OfflineVerifyForm::setTaskState(const QString& state)
{
    appendLog("[状态] " + state);
}

QString OfflineVerifyForm::currentModelPath() const
{
    return ui->editModelPath->text().trimmed();
}

int OfflineVerifyForm::currentBackendIndex() const
{
    return ui->cmbDevice->currentIndex();
}

QString OfflineVerifyForm::backendName() const
{
    switch (currentBackendIndex()) {
    case 0: return "TensorRT GPU";
    case 1: return "OpenCV CPU";
    case 2: return "OpenCV CUDA";
    default: return "Unknown";
    }
}

QString OfflineVerifyForm::classNameById(int classId) const
{
    if (classId >= 0 && classId < (int)kOfflineClassNames.size())
        return QString::fromStdString(kOfflineClassNames[classId]);
    return QString("class_%1").arg(classId);
}

void OfflineVerifyForm::saveLastModelPath(const QString& path)
{
    QSettings settings("MyCompany", "Qt_TensorRT_YOLOv12");
    settings.setValue("offline/lastModelPath", path);
    m_lastModelDir = QFileInfo(path).absolutePath();
}

QString OfflineVerifyForm::restoreLastModelPath()
{
    QSettings settings("MyCompany", "Qt_TensorRT_YOLOv12");
    return settings.value("offline/lastModelPath", "").toString();
}

void OfflineVerifyForm::saveLastImagePath(const QString& path)
{
    QSettings settings("MyCompany", "Qt_TensorRT_YOLOv12");
    settings.setValue("offline/lastImagePath", path);
    m_lastImageDir = QFileInfo(path).absolutePath();
}

QString OfflineVerifyForm::restoreLastImagePath()
{
    QSettings settings("MyCompany", "Qt_TensorRT_YOLOv12");
    return settings.value("offline/lastImagePath", "").toString();
}

void OfflineVerifyForm::displayResultImage(const cv::Mat& resultImg)
{
    if (resultImg.empty()) return;

    cv::Mat rgb;
    if (resultImg.channels() == 3) {
        cv::cvtColor(resultImg, rgb, cv::COLOR_BGR2RGB);
    } else if (resultImg.channels() == 4) {
        cv::cvtColor(resultImg, rgb, cv::COLOR_BGRA2RGB);
    } else {
        cv::cvtColor(resultImg, rgb, cv::COLOR_GRAY2RGB);
    }

    QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
    QImage imgCopy = qimg.copy();

    ImageView* iv = findChild<ImageView*>("imageView");
    if (iv) {
        iv->setImage(imgCopy);
    }
}

void OfflineVerifyForm::handleSelectModel()
{
    QString file = QFileDialog::getOpenFileName(
        this,
        "选择模型文件",
        m_lastModelDir,
        "模型文件 (*.engine *.onnx);;TensorRT Engine (*.engine);;ONNX (*.onnx);;所有文件 (*)",
        nullptr,
        QFileDialog::DontResolveSymlinks
    );

    if (file.isEmpty()) return;

    ui->editModelPath->setText(file);
    saveLastModelPath(file);
    appendLog("[信息] 选择模型: " + file);
}

void OfflineVerifyForm::handleSelectImage()
{
    QString file = QFileDialog::getOpenFileName(
        this,
        "选择推理图片",
        m_lastImageDir,
        "图片文件 (*.jpg *.jpeg *.png *.bmp *.tiff);;所有文件 (*)",
        nullptr,
        QFileDialog::DontResolveSymlinks
    );

    if (file.isEmpty()) return;

    ui->editImagePath->setText(file);
    m_offlineImagePath = file;
    m_offlineImage = cv::imread(file.toLocal8Bit().toStdString());

    if (m_offlineImage.empty()) {
        QMessageBox::warning(this, "警告", "无法加载图片文件，请选择有效的图片");
        ui->editImagePath->clear();
        return;
    }

    saveLastImagePath(file);
    appendLog("[信息] 选择图片: " + file);

    displayResultImage(m_offlineImage);
}

void OfflineVerifyForm::handleStartInfer()
{
    QString modelPath = currentModelPath();
    if (modelPath.isEmpty()) {
        QMessageBox::warning(this, "警告", "请先选择模型文件");
        return;
    }

    QFileInfo modelFile(modelPath);
    if (!modelFile.exists()) {
        QMessageBox::critical(this, "错误", "模型文件不存在: " + modelPath);
        return;
    }

    if (!modelFile.isFile()) {
        QMessageBox::critical(this, "错误", "指定的路径不是有效的文件: " + modelPath);
        return;
    }

    if (m_offlineImage.empty()) {
        QMessageBox::warning(this, "警告", "请先选择推理图片");
        return;
    }

    QString backend = backendName();
    if (backend == "TensorRT GPU" && !modelPath.endsWith(".engine", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, "警告", "TensorRT 后端需要 .engine 文件，请选择正确的模型或切换后端");
        return;
    }

    if ((backend == "OpenCV CPU" || backend == "OpenCV CUDA") && 
        !modelPath.endsWith(".onnx", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, "警告", "OpenCV DNN 后端需要 .onnx 文件，请选择正确的模型或切换后端");
        return;
    }

    setTaskState(QString("开始推理... 后端: %1").arg(backend));
    appendLog(QString("[模型] %1").arg(modelPath));
    appendLog(QString("[图片] %1").arg(m_offlineImagePath));
    appendLog(QString("[阈值] %1").arg(ui->valOfflineScore->value()));

    if (!loadOfflineModel()) {
        appendLog("[错误] 模型加载失败");
        QMessageBox::critical(this, "错误", "模型加载失败，请检查日志");
        return;
    }

    runOfflineInfer();
}

void OfflineVerifyForm::handleEnvCheck()
{
    appendLog("========== 环境自检 ==========");

    int driverVersion = 0;
    cudaError_t err = cudaDriverGetVersion(&driverVersion);
    if (err == cudaSuccess) {
        appendLog(QString("[CUDA Driver] %1").arg(driverVersion));
    } else {
        appendLog("[CUDA Driver] 获取失败: " + QString::fromUtf8(cudaGetErrorString(err)));
    }

    int runtimeVersion = 0;
    err = cudaRuntimeGetVersion(&runtimeVersion);
    if (err == cudaSuccess) {
        appendLog(QString("[CUDA Runtime] %1").arg(runtimeVersion));
    } else {
        appendLog("[CUDA Runtime] 获取失败: " + QString::fromUtf8(cudaGetErrorString(err)));
    }

    int deviceCount = 0;
    err = cudaGetDeviceCount(&deviceCount);
    if (err == cudaSuccess) {
        appendLog(QString("[CUDA Devices] %1").arg(deviceCount));
        for (int i = 0; i < deviceCount; ++i) {
            cudaDeviceProp prop;
            cudaGetDeviceProperties(&prop, i);
            appendLog(QString("  Device %1: %2 (VRAM: %3 MB, Compute: %4.%5)")
                          .arg(i)
                          .arg(prop.name)
                          .arg(prop.totalGlobalMem / 1024 / 1024)
                          .arg(prop.major)
                          .arg(prop.minor));
        }
    } else {
        appendLog("[CUDA Devices] 获取失败");
    }

    int trtVersion = NV_TENSORRT_VERSION;
    appendLog(QString("[TensorRT] %1").arg(trtVersion));

    appendLog(QString("[OpenCV] %1").arg(QString::fromStdString(cv::getBuildInformation().substr(0, 100).c_str())));

    bool hasCuda = cv::cuda::getCudaEnabledDeviceCount() > 0;
    appendLog(QString("[OpenCV CUDA] %1").arg(hasCuda ? "支持" : "不支持"));

    appendLog("========== 自检完成 ==========");
}

void OfflineVerifyForm::handleOnnxToEngine()
{
    QString onnxPath = QFileDialog::getOpenFileName(
        this,
        "选择 ONNX 文件",
        m_lastModelDir,
        "ONNX 文件 (*.onnx)",
        nullptr,
        QFileDialog::DontResolveSymlinks
    );
    if (onnxPath.isEmpty()) return;

    QString defaultEnginePath = QFileInfo(onnxPath).absolutePath() + "/" +
                                QFileInfo(onnxPath).baseName() + ".engine";
    QString enginePath = QFileDialog::getSaveFileName(
        this,
        "保存 Engine 文件",
        defaultEnginePath,
        "Engine 文件 (*.engine)",
        nullptr,
        QFileDialog::DontResolveSymlinks
    );
    if (enginePath.isEmpty()) return;

    bool useFP16 = false;
    QMessageBox::StandardButton reply = QMessageBox::question(this, "FP16 精度",
        "是否使用 FP16 精度？\n\n选择 Yes → 使用 FP16（更快，精度略低）\n选择 No  → 使用 FP32（更精确，速度较慢）",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    useFP16 = (reply == QMessageBox::Yes);

    QString trtexec = resolveTrtexecPath();
    if (trtexec.isEmpty()) {
        QMessageBox::critical(this, "错误", "未找到 trtexec，请确保 TensorRT 已安装且 trtexec 在 PATH 中");
        return;
    }

    QStringList args;
    args << "--onnx=" + onnxPath
         << "--saveEngine=" + enginePath
         << "--verbose";
    if (useFP16) {
        args << "--fp16";
    }

    appendLog("[信息] 开始转换 ONNX → Engine");
    appendLog(QString("  ONNX: %1").arg(onnxPath));
    appendLog(QString("  Engine: %1").arg(enginePath));
    appendLog(QString("  精度: %1").arg(useFP16 ? "FP16" : "FP32"));
    appendLog(QString("  trtexec: %1").arg(trtexec));

    if (m_trtexec) {
        m_trtexec->kill();
        m_trtexec->waitForFinished(3000);
        delete m_trtexec;
    }
    m_trtexec = new QProcess(this);
    m_trtexecEnginePath = enginePath;

    connect(m_trtexec, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &OfflineVerifyForm::onTrtexecFinished);
    connect(m_trtexec, &QProcess::readyReadStandardOutput,
            this, &OfflineVerifyForm::onTrtexecReadyRead);
    connect(m_trtexec, &QProcess::readyReadStandardError,
            this, &OfflineVerifyForm::onTrtexecReadyRead);
    connect(m_trtexec, qOverload<QProcess::ProcessError>(&QProcess::errorOccurred),
            this, &OfflineVerifyForm::onTrtexecError);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    augmentTrtexecLibraryPath(env);
    m_trtexec->setProcessEnvironment(env);

    m_trtexec->start(trtexec, args);
}

void OfflineVerifyForm::handleExportLog()
{
    QString defaultName = QString("offline_log_%1.txt")
                              .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString filePath = QFileDialog::getSaveFileName(
        this,
        "导出日志",
        QDir::currentPath() + "/" + defaultName,
        "文本文件 (*.txt)",
        nullptr,
        QFileDialog::DontResolveSymlinks
    );
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "错误", "无法写入文件: " + filePath);
        return;
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#else
    out.setEncoding(QStringConverter::Utf8);
#endif
    out << ui->txtLog->toPlainText();
    file.close();

    appendLog("[信息] 日志已导出到: " + filePath);
}

void OfflineVerifyForm::handleDeviceChanged(int index)
{
    Q_UNUSED(index);
    if (m_offlineTrtYolo) {
        delete m_offlineTrtYolo;
        m_offlineTrtYolo = nullptr;
    }
    if (m_offlineLogger) {
        delete m_offlineLogger;
        m_offlineLogger = nullptr;
    }
    m_offlineCpuNet = cv::dnn::Net();
    m_offlineCudaNet = cv::dnn::Net();
    m_offlineCpuReady = false;
    m_offlineCudaReady = false;
    appendLog("[信息] 切换后端至: " + backendName());
}

void OfflineVerifyForm::onTrtexecFinished(int exitCode, QProcess::ExitStatus status)
{
    if (status == QProcess::NormalExit && exitCode == 0) {
        appendLog("[成功] ONNX → Engine 转换完成");
        appendLog(QString("  输出: %1").arg(m_trtexecEnginePath));
        ui->editModelPath->setText(m_trtexecEnginePath);
        saveLastModelPath(m_trtexecEnginePath);
    } else {
        appendLog(QString("[失败] trtexec 退出码: %1").arg(exitCode));
    }
}

void OfflineVerifyForm::onTrtexecReadyRead()
{
    QString output = QString::fromUtf8(m_trtexec->readAllStandardOutput());
    if (!output.isEmpty()) {
        QStringList lines = output.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            if (line.contains("error", Qt::CaseInsensitive) ||
                line.contains("warning", Qt::CaseInsensitive) ||
                line.contains("success", Qt::CaseInsensitive) ||
                line.contains("finished", Qt::CaseInsensitive) ||
                line.contains("layer", Qt::CaseInsensitive)) {
                appendLog("[trtexec] " + line.trimmed());
            }
        }
    }
    QString errOutput = QString::fromUtf8(m_trtexec->readAllStandardError());
    if (!errOutput.isEmpty()) {
        appendLog("[trtexec-err] " + errOutput.trimmed());
    }
}

void OfflineVerifyForm::onTrtexecError(QProcess::ProcessError err)
{
    Q_UNUSED(err);
    appendLog("[错误] trtexec 进程出错: " + m_trtexec->errorString());
}

bool OfflineVerifyForm::loadOfflineModel()
{
    QString modelPath = currentModelPath();
    if (modelPath.isEmpty()) return false;

    int backend = currentBackendIndex();

    if (backend == 0) {
        if (m_offlineTrtYolo) {
            if (m_offlineLoadedModelPath == modelPath) {
                return true;
            }
            delete m_offlineTrtYolo;
            m_offlineTrtYolo = nullptr;
        }
        if (m_offlineLogger) {
            delete m_offlineLogger;
            m_offlineLogger = nullptr;
        }

        appendLog("[信息] 加载 TensorRT Engine...");
        try {
            m_offlineLogger = new TrtLogger();
            m_offlineTrtYolo = new TrtYolo(modelPath.toStdString(), *m_offlineLogger);
            m_offlineLoadedModelPath = modelPath;
            appendLog("[信息] TensorRT Engine 加载成功");
            return true;
        } catch (const std::exception& e) {
            appendLog("[错误] TensorRT Engine 加载失败: " + QString::fromUtf8(e.what()));
            if (m_offlineTrtYolo) {
                delete m_offlineTrtYolo;
                m_offlineTrtYolo = nullptr;
            }
            if (m_offlineLogger) {
                delete m_offlineLogger;
                m_offlineLogger = nullptr;
            }
            return false;
        }
    }
    else if (backend == 1) {
        if (m_offlineCpuReady &&
            m_offlineCpuNet.getUnconnectedOutLayersNames().size() > 0 &&
            m_offlineLoadedModelPath == modelPath) {
            return true;
        }
        appendLog("[信息] 加载 ONNX (CPU)...");
        try {
            m_offlineCpuNet = cv::dnn::readNetFromONNX(modelPath.toStdString());
            m_offlineCpuNet.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            m_offlineCpuNet.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            m_offlineCpuReady = true;
            m_offlineLoadedModelPath = modelPath;
            appendLog("[信息] ONNX CPU 加载成功");
            return true;
        } catch (const std::exception& e) {
            appendLog("[错误] ONNX CPU 加载失败: " + QString::fromUtf8(e.what()));
            m_offlineCpuReady = false;
            return false;
        }
    }
    else if (backend == 2) {
        if (m_offlineCudaReady &&
            m_offlineCudaNet.getUnconnectedOutLayersNames().size() > 0 &&
            m_offlineLoadedModelPath == modelPath) {
            return true;
        }
        appendLog("[信息] 加载 ONNX (CUDA)...");
        try {
            m_offlineCudaNet = cv::dnn::readNetFromONNX(modelPath.toStdString());
            m_offlineCudaNet.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
            m_offlineCudaNet.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
            m_offlineCudaReady = true;
            m_offlineLoadedModelPath = modelPath;
            appendLog("[信息] ONNX CUDA 加载成功");
            return true;
        } catch (const std::exception& e) {
            appendLog("[错误] ONNX CUDA 加载失败: " + QString::fromUtf8(e.what()));
            m_offlineCudaReady = false;
            return false;
        }
    }

    return false;
}

void OfflineVerifyForm::runOfflineInfer()
{
    int backend = currentBackendIndex();
    float confThreshold = (float)ui->valOfflineScore->value();
    cv::Mat resultImg = m_offlineImage.clone();

    QElapsedTimer timer;
    timer.start();

    if (backend == 0) {
        if (!m_offlineTrtYolo) {
            appendLog("[错误] TensorRT 引擎未加载");
            QMessageBox::critical(this, "错误", "TensorRT 引擎未加载");
            return;
        }

        m_offlineTrtYolo->confThreshold = confThreshold;
        m_offlineTrtYolo->preprocess(m_offlineImage);

        try {
            m_offlineTrtYolo->infer();
        } catch (const std::exception& e) {
            appendLog("[错误] TensorRT 推理失败: " + QString::fromUtf8(e.what()));
            QMessageBox::critical(this, "错误", "TensorRT 推理失败: " + QString::fromUtf8(e.what()));
            return;
        }

        std::vector<Detection> results;
        m_offlineTrtYolo->postprocess(results);

        drawDetections(resultImg, results);

        double inferTimeMs = timer.elapsed();
        appendLog(QString("[结果] TensorRT 检测到 %1 个目标").arg(results.size()));
        appendLog(QString("[耗时] 推理时长: %1 ms").arg(inferTimeMs, 0, 'f', 2));
        appendLog(QString("[性能] 约 %1 FPS").arg(1000.0 / inferTimeMs, 0, 'f', 1));

        for (const auto& det : results) {
            appendLog(QString("  %1: %.3f [%2, %3, %4, %5]")
                          .arg(classNameById(det.class_id))
                          .arg(det.score, 0, 'f', 3)
                          .arg(det.x).arg(det.y).arg(det.w).arg(det.h));
        }
    }
    else if (backend == 1 || backend == 2) {
        cv::dnn::Net& net = (backend == 1) ? m_offlineCpuNet : m_offlineCudaNet;
        if (net.empty()) {
            appendLog("[错误] OpenCV 网络未加载");
            QMessageBox::critical(this, "错误", "OpenCV 网络未加载");
            return;
        }

        int inputSize = 640;
        cv::Mat blob = cv::dnn::blobFromImage(m_offlineImage, 1.0/255.0,
            cv::Size(inputSize, inputSize), cv::Scalar(), true, false);

        net.setInput(blob);
        std::vector<cv::Mat> outputs;

        try {
            net.forward(outputs, net.getUnconnectedOutLayersNames());
        } catch (const std::exception& e) {
            appendLog("[错误] OpenCV DNN 推理失败: " + QString::fromUtf8(e.what()));
            QMessageBox::critical(this, "错误", "OpenCV DNN 推理失败: " + QString::fromUtf8(e.what()));
            return;
        }

        if (outputs.empty()) {
            appendLog("[错误] 推理无输出");
            QMessageBox::critical(this, "错误", "推理无输出");
            return;
        }

        cv::Mat& predMat = outputs[0];
        int numAttr = predMat.size[1];
        int numPred = predMat.size[2];
        bool transposed = (numAttr == 84 || numAttr == 85);

        if (!transposed && numPred > numAttr) {
            int tmp = numAttr;
            numAttr = numPred;
            numPred = tmp;
            transposed = true;
        }

        std::vector<Detection> results;
        float* data = (float*)predMat.data;

        for (int i = 0; i < numPred; ++i) {
            int offset = transposed ? (i * numAttr) : i;
            float* ptr;
            if (transposed) {
                ptr = data + offset;
            } else {
                ptr = data + offset;
            }

            float score;
            int classId;
            float cx, cy, w, h;

            if (transposed) {
                cx = ptr[0];
                cy = ptr[1];
                w  = ptr[2];
                h  = ptr[3];
                score = 0;
                classId = -1;
                for (int c = 4; c < numAttr; ++c) {
                    if (ptr[c] > score) {
                        score = ptr[c];
                        classId = c - 4;
                    }
                }
            } else {
                int numClasses = numAttr - 4;
                score = 0;
                classId = -1;
                for (int c = 0; c < numClasses; ++c) {
                    if (ptr[c] > score) {
                        score = ptr[c];
                        classId = c;
                    }
                }
                cx = ptr[numClasses];
                cy = ptr[numClasses + 1];
                w  = ptr[numClasses + 2];
                h  = ptr[numClasses + 3];
            }

            if (score < confThreshold || classId < 0) continue;

            float scale = (float)inputSize / std::max(m_offlineImage.cols, m_offlineImage.rows);
            float padX = (inputSize - m_offlineImage.cols * scale) / 2.0f;
            float padY = (inputSize - m_offlineImage.rows * scale) / 2.0f;

            float x1 = (cx - w/2 - padX) / scale;
            float y1 = (cy - h/2 - padY) / scale;
            float bw = w / scale;
            float bh = h / scale;

            Detection det;
            det.class_id = classId;
            det.score = score;
            det.x = (int)x1;
            det.y = (int)y1;
            det.w = (int)bw;
            det.h = (int)bh;
            results.push_back(det);
        }

        std::vector<int> keep;
        std::vector<cv::Rect> boxes;
        std::vector<float> scores;
        for (const auto& d : results) {
            boxes.push_back(cv::Rect(d.x, d.y, d.w, d.h));
            scores.push_back(d.score);
        }
        cv::dnn::NMSBoxes(boxes, scores, confThreshold, 0.5f, keep);

        std::vector<Detection> finalResults;
        for (int idx : keep) {
            finalResults.push_back(results[idx]);
        }

        drawDetections(resultImg, finalResults);

        double inferTimeMs = timer.elapsed();
        appendLog(QString("[结果] %1 检测到 %2 个目标")
                      .arg(backendName())
                      .arg(finalResults.size()));
        appendLog(QString("[耗时] 推理时长: %1 ms").arg(inferTimeMs, 0, 'f', 2));
        appendLog(QString("[性能] 约 %1 FPS").arg(1000.0 / inferTimeMs, 0, 'f', 1));

        for (const auto& det : finalResults) {
            appendLog(QString("  %1: %.3f [%2, %3, %4, %5]")
                          .arg(classNameById(det.class_id))
                          .arg(det.score, 0, 'f', 3)
                          .arg(det.x).arg(det.y).arg(det.w).arg(det.h));
        }
    }

    displayResultImage(resultImg);
    setTaskState("推理完成");
}

void OfflineVerifyForm::drawDetections(cv::Mat& img, const std::vector<Detection>& results)
{
    for (const auto& det : results) {
        cv::Scalar color = offlineClassColor(det.class_id);
        cv::rectangle(img, cv::Rect(det.x, det.y, det.w, det.h), color, 2);

        QString label = QString("%1 %.2f")
                            .arg(classNameById(det.class_id))
                            .arg(det.score);
        cv::putText(img, label.toStdString(),
                    cv::Point(det.x, det.y - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
    }
}

QString OfflineVerifyForm::resolveTrtexecPath()
{
    QString pathEnv = qEnvironmentVariable("PATH");
    QStringList dirs = pathEnv.split(':', Qt::SkipEmptyParts);
    for (const QString& dir : dirs) {
        QFileInfo fi(dir + "/trtexec");
        if (fi.exists() && fi.isExecutable()) {
            return fi.absoluteFilePath();
        }
    }

    QStringList candidates = {
        "/usr/local/TensorRT-10.3/bin/trtexec",
        "/usr/local/TensorRT-10.0/bin/trtexec",
        "/usr/local/TensorRT-8.6/bin/trtexec",
        "/usr/local/TensorRT-8.5/bin/trtexec",
        "/usr/local/TensorRT-8.4/bin/trtexec",
        "/usr/local/TensorRT/bin/trtexec",
        "/opt/TensorRT/bin/trtexec"
    };
    for (const QString& path : candidates) {
        QFileInfo fi(path);
        if (fi.exists() && fi.isExecutable()) {
            return fi.absoluteFilePath();
        }
    }

    return QString();
}

void OfflineVerifyForm::augmentTrtexecLibraryPath(QProcessEnvironment& env) const
{
    QStringList libCandidates = {
        "/usr/local/TensorRT-10.3/lib",
        "/usr/local/TensorRT-10.3/targets/x86_64-linux-gnu/lib",
        "/usr/local/TensorRT/lib",
        "/usr/local/cuda/lib64"
    };
    QString existing = env.value("LD_LIBRARY_PATH", "");
    QStringList paths = existing.split(':', Qt::SkipEmptyParts);
    for (const QString& libPath : libCandidates) {
        if (QDir(libPath).exists() && !paths.contains(libPath)) {
            paths.prepend(libPath);
        }
    }
    env.insert("LD_LIBRARY_PATH", paths.join(':'));
}
