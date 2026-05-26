#ifndef GRABTHREAD_H
#define GRABTHREAD_H

#include <QThread>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>
#include "cameracontroller.h"

class InferThread; // 前向声明

class GrabThread : public QThread
{
    Q_OBJECT
public:
    GrabThread(CameraController* cam);
    void stop();

    // ⭐ 设置推理线程指针，用于直接投递帧（避免 DirectConnection 阻塞）
    void setInferThread(InferThread* infer) { m_inferThread = infer; }

signals:
    void deviceLost();  // ⭐ 检测到相机掉线

protected:
    void run() override;

private:
    bool m_running;
    CameraController* m_camera;
    InferThread* m_inferThread{nullptr};
    int m_consecutiveFailCount{0};       // ⭐ 连续获取失败计数
    static const int MAX_FAIL_COUNT = 300; // ⭐ 连续失败阈值（约15秒，每次失败sleep 50ms）
};

#endif
