#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include <QObject>
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
signals:
    void deviceDisconnected();

private:
    void* handle;
    std::vector<MV_CC_DEVICE_INFO> devices;
};

#endif
