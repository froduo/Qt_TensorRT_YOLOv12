#ifndef INFERTHREAD_H
#define INFERTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
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

    bool setEngine(const QString& enginePath);
    void stop();
    void setFrame(const cv::Mat& frame);

    void setScoreThreshold(float val) ;
signals:
    void sendResult(QImage img, float inferTimeMs, float fps, std::vector<Detection> results);
    void engineLoadFailed(const QString& msg);

protected:
    void run() override;

private:
    bool m_running;
    cv::Mat m_frame;
    QMutex mutex;
    QWaitCondition cond;

    TrtYolo* yolo;

    TrtLogger logger;   // ✅ 成员 logger（不是引用）
};

#endif // INFERTHREAD_H
