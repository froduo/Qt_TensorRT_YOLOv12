#include "grabthread.h"
#include "inferthread.h"
#include "logger.h"

GrabThread::GrabThread(CameraController* cam)
{
    m_camera=cam;
    m_running=true;
    m_consecutiveFailCount=0;
}

void GrabThread::stop(){ m_running=false; }

void GrabThread::run()
{
    MV_FRAME_OUT frame;
    int frameIndex = 0;
    QElapsedTimer frameTimer;
    frameTimer.start();

    LOG_INFO("[GrabThread] Started grabbing frames");

    while(m_running)
    {
        if(!m_camera->getFrame(frame)) {
            // ⭐ 获取帧失败，累计计数
            m_consecutiveFailCount++;
            if (m_consecutiveFailCount == 1) {
                LOG_WARN(QString("[GrabThread] First frame grab failed, consecutive failures: %1/%2")
                         .arg(m_consecutiveFailCount).arg(MAX_FAIL_COUNT));
            } else if (m_consecutiveFailCount % 50 == 0) {
                // ⭐ 每50次失败记录一次日志，避免刷屏
                LOG_WARN(QString("[GrabThread] Frame grab consecutive failures: %1/%2")
                         .arg(m_consecutiveFailCount).arg(MAX_FAIL_COUNT));
            }
            if (m_consecutiveFailCount >= MAX_FAIL_COUNT) {
                LOG_ERR(QString("[GrabThread] MAX_FAIL_COUNT (%1) reached, emitting deviceLost").arg(MAX_FAIL_COUNT));
                emit deviceLost();
                // 发出掉线信号后退出线程，等待重连后重新创建
                m_running = false;
                break;
            }
            QThread::msleep(50); // 失败时休眠50ms，避免空转
            continue;
        }

        // 成功获取帧，重置失败计数
        if (m_consecutiveFailCount > 0) {
            LOG_INFO(QString("[GrabThread] Frame grab recovered after %1 consecutive failures").arg(m_consecutiveFailCount));
            m_consecutiveFailCount = 0;
        }

        cv::Mat img(frame.stFrameInfo.nHeight,
                    frame.stFrameInfo.nWidth,
                    CV_8UC1,
                    frame.pBufAddr);

        cv::Mat bgr;
        cv::cvtColor(img,bgr,cv::COLOR_GRAY2BGR);

        // ⭐ 必须 clone 后再释放，否则跨线程传递时数据已失效
        cv::Mat frameClone = bgr.clone();
        m_camera->freeFrame(frame);

        // ⭐ 关键日志：每100帧或每5秒记录一次抓帧状态
        frameIndex++;
        qint64 elapsed = frameTimer.elapsed();
        if (frameIndex % 100 == 0 || elapsed >= 5000) {
            LOG_INFO(QString("[GrabThread] Frame #%1 captured, size=%2x%3, interval=%4ms")
                     .arg(frameIndex)
                     .arg(frameClone.cols).arg(frameClone.rows)
                     .arg(elapsed));
            frameTimer.restart();
        }

        // ⭐ 直接投递帧到 InferThread 的队列，避免信号跨线程阻塞
        if (m_inferThread) {
            m_inferThread->setFrame(frameClone);
        }
    }

    LOG_INFO(QString("[GrabThread] Exited, total frames captured: %1").arg(frameIndex));
}

