#ifndef INFERTHREAD_H
#define INFERTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <opencv2/opencv.hpp>
#include "trt_yolo.h"
#include <NvInfer.h>
#include<QDebug>
#include <QImage>

class TrtLogger : public nvinfer1::ILogger
{
public:
    void log(Severity severity, const char* msg) noexcept override
    {
        if (severity <= Severity::kWARNING)
            qDebug() << "[TensorRT]" << msg;
    }
};

class InferThread : public QThread
{
    Q_OBJECT
public:
    explicit InferThread(QObject *parent = nullptr);
    ~InferThread();

    bool setEngine(const QString& enginePath, const QString& classesPath = "");
    bool setClasses(const QString& classesPath);
    void stop();
    void setFrame(const cv::Mat& frame);

    void setScoreThreshold(float val);

public slots:
    // ⭐ 声明为 slot，确保 QueuedConnection 能正确投递
    void onFrameReceived(const cv::Mat& frame);

signals:
    void sendResult(QImage img, float inferTimeMs, float fps, std::vector<Detection> results);
    void engineLoadFailed(const QString& msg);

protected:
    void run() override;

private:
    bool m_running;
    
    // ⭐ 帧队列：解耦 GrabThread 生产者和 InferThread 消费者
    QQueue<cv::Mat> m_frameQueue;
    QMutex mutex;
    QWaitCondition cond;

    TrtYolo* yolo;

    TrtLogger logger;   // ✅ 成员 logger（不是引用）

    // ⭐ 超时保护：防止 cond_wait 永久阻塞
    static const int FRAME_WAIT_TIMEOUT_MS = 1000; // 1秒超时
    static const int MAX_QUEUE_SIZE = 5;            // ⭐ 最大队列深度，防止内存暴涨
};

#endif // INFERTHREAD_H
