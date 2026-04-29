#include "grabthread.h"

GrabThread::GrabThread(CameraController* cam)
{
    m_camera=cam;
    m_running=true;
}

void GrabThread::stop(){ m_running=false; }

void GrabThread::run()
{
    MV_FRAME_OUT frame;

    while(m_running)
    {
        if(!m_camera->getFrame(frame)) continue;

        cv::Mat img(frame.stFrameInfo.nHeight,
                    frame.stFrameInfo.nWidth,
                    CV_8UC1,
                    frame.pBufAddr);

        cv::Mat bgr;
        cv::cvtColor(img,bgr,cv::COLOR_GRAY2BGR);

        emit sendFrame(bgr); // ❗不 clone
        m_camera->freeFrame(frame);
    }
}

