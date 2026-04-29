#ifndef GRABTHREAD_H
#define GRABTHREAD_H

#include <QThread>
#include <opencv2/opencv.hpp>
#include "cameracontroller.h"

class GrabThread : public QThread
{
    Q_OBJECT
public:
    GrabThread(CameraController* cam);
    void stop();

signals:
    void sendFrame(cv::Mat frame);

protected:
    void run() override;

private:
    bool m_running;
    CameraController* m_camera;
};

#endif
