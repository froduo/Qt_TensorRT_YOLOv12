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

bool InferThread::setEngine(const QString &enginePath)
{
    QFileInfo fi(enginePath);
    if (!fi.exists())
    {
        LOG_ERR(QString("Engine file not found: %1").arg(enginePath));
        emit engineLoadFailed("Engine file not found:\n" + enginePath);
        return false;
    }

    // ⭐ 日志：记录加载的模型路径
    LOG_INFO(QString("Loading TensorRT engine: %1").arg(enginePath));

    try
    {
        if (yolo)
        {
            delete yolo;
            yolo = nullptr;
        }

        yolo = new TrtYolo(enginePath.toStdString(), logger);

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

void InferThread::stop()
{
    QMutexLocker locker(&mutex);
    m_running = false;
    m_frame.release();  // 释放待处理帧，确保 wait 不会卡住
    cond.wakeAll();
}

void InferThread::setFrame(const cv::Mat &frame)
{
    QMutexLocker locker(&mutex);
    m_frame = frame.clone();   // 必须 clone
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

    while (true)
    {
        cv::Mat img;

        {
            QMutexLocker locker(&mutex);
            if (!m_running)
                break;

            if (m_frame.empty())
                cond.wait(&mutex);

            if (!m_running)
                break;

            img = m_frame.clone();
            m_frame.release();
        }

        if (!yolo)
            continue;

        std::vector<Detection> results;

        // ===== 推理计时 =====
        QElapsedTimer inferTimer;
        inferTimer.start();

        yolo->preprocess(img);
        yolo->infer();
        yolo->postprocess(results);

        float inferTimeMs = inferTimer.elapsed();
        int detCount = results.size();

        // ⭐ 日志：记录每帧推理结果
        qDebug() << "[InferThread] Inference completed:"
                 << detCount << "detections,"
                 << QString::number(inferTimeMs, 'f', 1) << "ms";

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
        // 1. 准备文字内容
        char text1[128];
        char text2[128];
        char text3[128];

        sprintf(text1, "FPS: %.1f", fps);
        sprintf(text2, "Infer: %.2f ms", inferTimeMs);
        sprintf(text3, "Detections: %d", detCount);

        // 2. 设置字体参数（推荐使用 0.8 到 1.0 的大小，更清晰）
        double fontScale = 0.8;
        int thickness = 2;
        int fontFace = cv::FONT_HERSHEY_SIMPLEX;

        // 3. 绘制背景框
        // 这里将高度设为 120 (5+120)，宽度设为 250
        // 此时框的范围是 y=5 到 y=125
        // 如果文字较多，可以适当增大宽度和高度
        cv::rectangle(img,
                      cv::Point(5, 5),          // 左上角
                      cv::Point(5 + 260, 5 + 110), // 右下角 (宽度260，高度110)
                      cv::Scalar(0, 0, 0),      // 黑色背景
                      -1);                      // 实心填充

        // 4. 绘制文字 (调整了 Y 坐标，使其位于框内)
        // 第一行 Y=35
        cv::putText(img, text1, cv::Point(15, 35),
                    fontFace, fontScale,
                    cv::Scalar(0, 255, 0), thickness);

        // 第二行 Y=70 (间距 35)
        cv::putText(img, text2, cv::Point(15, 70),
                    fontFace, fontScale,
                    cv::Scalar(0, 255, 0), thickness);

        // 第三行 Y=105 (间距 35)
        // 105 < 115 (框底边)，所以能被包住
        cv::putText(img, text3, cv::Point(15, 105),
                    fontFace, fontScale,
                    cv::Scalar(0, 255, 0), thickness);

        // 5. 格式转换 OpenCV -> Qt
        cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
        QImage qimg(img.data,
                    img.cols,
                    img.rows,
                    img.step,
                    QImage::Format_RGB888);

        emit sendResult(qimg.copy(), inferTimeMs, fps, results);
    }
}
void InferThread::setScoreThreshold(float val) {
    if (yolo) { // 必须判断引擎是否已创建
        yolo->confThreshold = val;
        qDebug() << "Detection threshold updated to:" << val;
    } else {
        qDebug() << "Warning: Cannot set threshold, YOLO engine not initialized.";
    }
}
