#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include <QObject>
#include <QTimer>
#include <opencv2/opencv.hpp>
#include "MvCameraControl.h"

class CameraController : public QObject
{
    Q_OBJECT
public:
    CameraController();
    ~CameraController();

    bool enumDevices(std::vector<MV_CC_DEVICE_INFO*>& list);
    bool openCamera(int index);
    bool startGrabbing();
    bool stopGrabbing();
    bool getFrame(MV_FRAME_OUT& frame);
    void freeFrame(MV_FRAME_OUT& frame);
    void closeCamera();
    bool isOpen();
    // ⭐ 新增参数设置函数
    bool setExposureTime(float exposureTimeUs); // 单位：微秒
    bool setGain(float gain);                   // 单位：dB

    // ⭐ 自动重连相关
    void setTargetSN(const QString& sn);        // 设置目标相机序列号
    bool reconnect();                           // 尝试重连（枚举+打开+开始抓取）
    int  getTargetIndex() const { return m_targetIndex; }

signals:
    void deviceDisconnected();

private:
    void* handle;
    std::vector<MV_CC_DEVICE_INFO> devices;
    QString m_targetSN;       // 目标相机序列号
    int     m_targetIndex;    // 目标相机索引
};

#endif
