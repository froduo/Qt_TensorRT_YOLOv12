#include "inferthread.h"
#include "logger.h"
#include <QFileInfo>
#include <QDebug>
#include <QElapsedTimer>
#include <QImage>

InferThread::InferThread(QObject *parent, int cameraId)
    : QThread(parent),
    m_running(true),
    m_cameraId(cameraId),
    yolo(nullptr)
{
}

InferThread::~InferThread()
{
    stop();
    cond.wakeAll();
    wait();

    if (yolo)
    {
        delete yolo;
        yolo = nullptr;
    }
}

bool InferThread::setEngine(const QString &enginePath, const QString &classesPath)
{
    QFileInfo fi(enginePath);
    if (!fi.exists())
    {
        // ⭐ 如果 .engine 文件不存在，尝试查找同名的 .onnx 文件自动转换
        QString onnxPath = enginePath;
        if (onnxPath.endsWith(".engine", Qt::CaseInsensitive)) {
            onnxPath.replace(onnxPath.length() - 7, 7, ".onnx");
        } else {
            onnxPath += ".onnx";
        }

        QFileInfo onnxFi(onnxPath);
        if (onnxFi.exists()) {
            LOG_INFO(QString("Engine file not found, but ONNX file exists: %1").arg(onnxPath));
            LOG_INFO(QString("Attempting to build engine from ONNX (this may take a while)..."));
            emit engineLoadFailed(QString("Engine file not found. Converting ONNX to engine...\n%1\n\nPlease wait, this may take several minutes.").arg(onnxPath));

            // 执行 ONNX → Engine 转换
            bool buildOk = TrtYolo::buildEngineFromOnnx(
                onnxPath.toStdString(),
                enginePath.toStdString(),
                logger);

            if (!buildOk) {
                QString errMsg = QString("Failed to build TensorRT engine from ONNX:\n%1").arg(onnxPath);
                LOG_ERR(errMsg);
                emit engineLoadFailed(errMsg);
                return false;
            }

            LOG_INFO(QString("ONNX conversion succeeded, engine saved to: %1").arg(enginePath));
        } else {
            LOG_ERR(QString("Engine file not found: %1").arg(enginePath));
            LOG_ERR(QString("ONNX file also not found: %1").arg(onnxPath));
            emit engineLoadFailed("Engine file not found:\n" + enginePath + "\n\n"
                                  "ONNX file also not found:\n" + onnxPath);
            return false;
        }
    }

    LOG_INFO(QString("Loading TensorRT engine: %1").arg(enginePath));
    if (!classesPath.isEmpty()) {
        LOG_INFO(QString("Loading classes from: %1").arg(classesPath));
    }

    try
    {
        if (yolo)
        {
            delete yolo;
            yolo = nullptr;
        }

        yolo = new TrtYolo(enginePath.toStdString(), logger, classesPath.toStdString());

        LOG_INFO("TensorRT engine loaded successfully.");
        qDebug() << "TensorRT engine loaded successfully.";
        return true;
    }
    catch (std::exception& e)
    {
        LOG_ERR(QString("TensorRT init failed: %1").arg(e.what()));
        emit engineLoadFailed(QString("TensorRT init failed: %1").arg(e.what()));
        return false;
    }
}

bool InferThread::setClasses(const QString &classesPath)
{
    if (yolo) {
        return yolo->loadClasses(classesPath.toStdString());
    }
    return false;
}

void InferThread::stop()
{
    QMutexLocker locker(&mutex);
    m_running = false;
    m_frameQueue.clear();  // 清空待处理帧队列
    cond.wakeAll();
}

void InferThread::setFrame(const cv::Mat &frame)
{
    QMutexLocker locker(&mutex);
    // ⭐ 如果队列已满，丢弃最旧的帧，防止内存暴涨
    if (m_frameQueue.size() >= MAX_QUEUE_SIZE) {
        m_frameQueue.dequeue();
    }
    m_frameQueue.enqueue(frame.clone());   // 必须 clone
    cond.wakeOne();
}

void InferThread::run()
{
    // ⭐ 重置运行标志，因为 stop() 将其设为 false，而 start() 不会自动重置
    {
        QMutexLocker locker(&mutex);
        m_running = true;
    }

    QElapsedTimer fpsTimer;
    fpsTimer.start();

    int frameCount = 0;
    float fps = 0.0f;

    // ⭐ 用于检测是否长时间没有新帧
    QElapsedTimer idleTimer;
    idleTimer.start();

    // ⭐ 推理帧计数器
    int inferFrameCount = 0;
    QElapsedTimer inferLogTimer;
    inferLogTimer.start();

    LOG_INFO("[InferThread] Run loop started");

    // ⭐ 帧计数器，用于调试
    int processedCount = 0;
    QElapsedTimer logTimer;
    logTimer.start();

    while (true)
    {
        // ===== 检查运行状态 =====
        {
            QMutexLocker locker(&mutex);
            if (!m_running) {
                LOG_INFO("[InferThread] Run loop: m_running=false, exiting");
                break;
            }
        }

        // ===== 取出待处理帧 =====
        bool hasFrame = false;
        cv::Mat img;

        {
            QMutexLocker locker(&mutex);
            if (!m_frameQueue.isEmpty()) {
                img = m_frameQueue.dequeue();
                hasFrame = true;
                idleTimer.restart();
            }
        }

        // 如果没有帧，等待一下
        if (!hasFrame) {
            QThread::msleep(5);
            continue;
        }

        // ===== 处理帧（推理或原始显示） =====
        if (hasFrame && !img.empty()) {
            try {
                if (m_enableInference && yolo) {
                    // ---- 推理模式 ----
                    std::vector<Detection> results;

                    QElapsedTimer inferTimer;
                    inferTimer.start();

                    yolo->preprocess(img);
                    yolo->infer();
                    yolo->postprocess(results);

                    float inferTimeMs = inferTimer.elapsed();
                    int detCount = results.size();

                    yolo->draw(img, results);

                    frameCount++;
                    if (fpsTimer.elapsed() >= 1000)
                    {
                        fps = frameCount * 1000.0f / fpsTimer.elapsed();
                        frameCount = 0;
                        fpsTimer.restart();
                    }

                    // 画文字
                    char text1[128], text2[128], text3[128];
                    sprintf(text1, "FPS: %.1f", fps);
                    sprintf(text2, "Infer: %.2f ms", inferTimeMs);
                    sprintf(text3, "Detections: %d", detCount);

                    cv::rectangle(img, cv::Point(5, 5), cv::Point(5 + 260, 5 + 110), cv::Scalar(0, 0, 0), -1);
                    cv::putText(img, text1, cv::Point(15, 35), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
                    cv::putText(img, text2, cv::Point(15, 70), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
                    cv::putText(img, text3, cv::Point(15, 105), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);

                    cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
                    QImage qimg(img.data, img.cols, img.rows, img.step, QImage::Format_RGB888);

                    inferFrameCount++;
                    if (inferFrameCount % 100 == 0 || inferLogTimer.elapsed() >= 5000) {
                        LOG_INFO(QString("[InferThread] Inferred frame #%1, det=%2, time=%3ms, fps=%4, img=%5x%6")
                                 .arg(inferFrameCount).arg(detCount)
                                 .arg(QString::number(inferTimeMs, 'f', 1))
                                 .arg(QString::number(fps, 'f', 1))
                                 .arg(img.cols).arg(img.rows));
                        inferLogTimer.restart();
                    }

                    emit sendResult(qimg.copy(), inferTimeMs, fps, results);
                } else {
                    // ---- 非推理模式：直接发送原始帧用于显示和保存 ----
                    cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
                    QImage qimg(img.data, img.cols, img.rows, img.step, QImage::Format_RGB888);
                    emit sendRawFrame(qimg.copy(), m_cameraId);
                }
            } catch (std::exception& e) {
                LOG_ERR(QString("[InferThread] Frame processing error: %1").arg(e.what()));
            }

            // ⭐ 日志：记录帧处理情况
            processedCount++;
            if (processedCount % 100 == 0 || logTimer.elapsed() >= 5000) {
                LOG_INFO(QString("[InferThread] Processed #%1 frames, queue=%2")
                         .arg(processedCount)
                         .arg(m_frameQueue.size()));
                logTimer.restart();
            }
        }
    }

    LOG_INFO(QString("[InferThread] Run loop exited, total inferred frames: %1")
             .arg(inferFrameCount));
}

void InferThread::setScoreThreshold(float val) {
    if (yolo) {
        yolo->confThreshold = val;
        qDebug() << "Detection threshold updated to:" << val;
    } else {
        qDebug() << "Warning: Cannot set threshold, YOLO engine not initialized.";
    }
}

void InferThread::setEnableInference(bool en) {
    m_enableInference = en;
    qDebug() << "[InferThread] Inference" << (en ? "ENABLED" : "DISABLED");
}
