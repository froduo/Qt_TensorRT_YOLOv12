#include "inferthread.h"
#include "logger.h"
#include <QFileInfo>
#include <QDebug>
#include <QElapsedTimer>
#include <QImage>

InferThread::InferThread(QObject *parent)
    : QThread(parent),
    m_running(true),
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
        LOG_ERR(QString("Engine file not found: %1").arg(enginePath));
        emit engineLoadFailed("Engine file not found:\n" + enginePath);
        return false;
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

// ⭐ 新增 slot：由信号触发，与 setFrame 功能相同但声明为 slot
void InferThread::onFrameReceived(const cv::Mat& frame)
{
    QMutexLocker locker(&mutex);

    // ⭐ 关键日志：记录帧接收情况
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 100 == 0) {
        LOG_INFO(QString("[InferThread] onFrameReceived #%1, frame size=%2x%3")
                 .arg(frameCount).arg(frame.cols).arg(frame.rows));
    }

    // ⭐ 如果队列已满，丢弃最旧的帧，防止内存暴涨
    if (m_frameQueue.size() >= MAX_QUEUE_SIZE) {
        m_frameQueue.dequeue();
    }
    m_frameQueue.enqueue(frame.clone());
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

    while (true)
    {
        cv::Mat img;

        {
            QMutexLocker locker(&mutex);
            if (!m_running) {
                LOG_INFO("[InferThread] Run loop: m_running=false, exiting");
                break;
            }

            // ⭐ 使用帧队列：等待直到队列非空
            bool waited = false;
            while (m_frameQueue.isEmpty() && m_running)
            {
                if (!cond.wait(&mutex, FRAME_WAIT_TIMEOUT_MS))
                {
                    // ⭐ 超时返回 false，说明长时间没有新帧
                    if (!m_running)
                        break;
                    // ⭐ 关键日志：记录超时等待
                    qint64 idleMs = idleTimer.elapsed();
                    if (!waited) {
                        LOG_WARN(QString("[InferThread] First frame wait timeout (%1ms), idle=%2ms")
                                 .arg(FRAME_WAIT_TIMEOUT_MS).arg(idleMs));
                        waited = true;
                    } else if (idleMs >= 5000) {
                        // ⭐ 每5秒记录一次长时间空闲警告
                        LOG_WARN(QString("[InferThread] No frame for %1 seconds, still waiting...")
                                 .arg(idleMs / 1000));
                        idleTimer.restart();
                    }
                }
            }

            if (!m_running) {
                LOG_INFO("[InferThread] Run loop: m_running=false after wait, exiting");
                break;
            }

            // ⭐ 安全检查：如果队列仍为空（理论上不会发生），跳过本轮
            if (m_frameQueue.isEmpty())
            {
                LOG_WARN("[InferThread] Skipping empty frame after wait (spurious wakeup)");
                continue;
            }

            // ⭐ 从队列头部取出最旧的帧
            img = m_frameQueue.dequeue();

            // ⭐ 重置空闲计时器
            idleTimer.restart();
        }

        if (!yolo) {
            LOG_WARN("[InferThread] yolo is null, skipping inference");
            continue;
        }

        // ⭐ 安全检查：跳过空图像
        if (img.empty())
        {
            LOG_WARN("[InferThread] Skipping inference on empty image");
            continue;
        }

        std::vector<Detection> results;

        // ===== 推理计时 =====
        QElapsedTimer inferTimer;
        inferTimer.start();

        try
        {
            yolo->preprocess(img);
            yolo->infer();
            yolo->postprocess(results);
        }
        catch (std::exception& e)
        {
            LOG_ERR(QString("[InferThread] Inference error: %1").arg(e.what()));
            continue;
        }

        float inferTimeMs = inferTimer.elapsed();
        int detCount = results.size();

        // 画检测框
        yolo->draw(img, results);

        // ===== FPS 统计 =====
        frameCount++;
        if (fpsTimer.elapsed() >= 1000)
        {
            fps = frameCount * 1000.0f / fpsTimer.elapsed();
            frameCount = 0;
            fpsTimer.restart();
        }

        // ===== 直接画文字到 img 上 =====
        char text1[128];
        char text2[128];
        char text3[128];

        sprintf(text1, "FPS: %.1f", fps);
        sprintf(text2, "Infer: %.2f ms", inferTimeMs);
        sprintf(text3, "Detections: %d", detCount);

        double fontScale = 0.8;
        int thickness = 2;
        int fontFace = cv::FONT_HERSHEY_SIMPLEX;

        cv::rectangle(img,
                      cv::Point(5, 5),
                      cv::Point(5 + 260, 5 + 110),
                      cv::Scalar(0, 0, 0),
                      -1);

        cv::putText(img, text1, cv::Point(15, 35),
                    fontFace, fontScale,
                    cv::Scalar(0, 255, 0), thickness);

        cv::putText(img, text2, cv::Point(15, 70),
                    fontFace, fontScale,
                    cv::Scalar(0, 255, 0), thickness);

        cv::putText(img, text3, cv::Point(15, 105),
                    fontFace, fontScale,
                    cv::Scalar(0, 255, 0), thickness);

        // 格式转换 OpenCV -> Qt
        cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
        QImage qimg(img.data,
                    img.cols,
                    img.rows,
                    img.step,
                    QImage::Format_RGB888);

        // ⭐ 关键日志：每100帧记录一次推理状态
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
    }

    LOG_INFO(QString("[InferThread] Run loop exited, total inferred frames: %1").arg(inferFrameCount));
}
void InferThread::setScoreThreshold(float val) {
    if (yolo) { // 必须判断引擎是否已创建
        yolo->confThreshold = val;
        qDebug() << "Detection threshold updated to:" << val;
    } else {
        qDebug() << "Warning: Cannot set threshold, YOLO engine not initialized.";
    }
}
