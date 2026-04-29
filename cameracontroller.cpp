#include "cameracontroller.h"
#include <QDebug>

CameraController::CameraController():handle(nullptr)
{
    MV_CC_Initialize();
}

CameraController::~CameraController()
{
    closeCamera();
    MV_CC_Finalize();
}

bool CameraController::enumDevices(std::vector<MV_CC_DEVICE_INFO*>& list)
{
    devices.clear();
    list.clear();

    MV_CC_DEVICE_INFO_LIST devList;
    memset(&devList,0,sizeof(devList));

    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devList);
    if(ret != MV_OK) return false;

    for(unsigned int i=0;i<devList.nDeviceNum;i++)
    {
        devices.push_back(*devList.pDeviceInfo[i]);
        list.push_back(&devices.back());
    }
    return true;
}

bool CameraController::openCamera(int index)
{
    if(index<0 || index>=devices.size()) return false;

    int ret = MV_CC_CreateHandle(&handle, &devices[index]);
    if(ret != MV_OK) return false;

    ret = MV_CC_OpenDevice(handle);
    if(ret != MV_OK) return false;

    MV_CC_SetEnumValue(handle,"TriggerMode",0);

    return true;
}

bool CameraController::startGrabbing()
{
    return MV_CC_StartGrabbing(handle)==MV_OK;
}

bool CameraController::stopGrabbing()
{
    return MV_CC_StopGrabbing(handle)==MV_OK;
}

bool CameraController::getFrame(MV_FRAME_OUT& frame)
{
    memset(&frame,0,sizeof(MV_FRAME_OUT));
    int ret = MV_CC_GetImageBuffer(handle,&frame,1000);
    if(ret != MV_OK) return false;
    return true;
}

void CameraController::freeFrame(MV_FRAME_OUT& frame)
{
    MV_CC_FreeImageBuffer(handle,&frame);
}

void CameraController::closeCamera()
{
    if(handle){
        MV_CC_StopGrabbing(handle);
        MV_CC_CloseDevice(handle);
        MV_CC_DestroyHandle(handle);
        handle=nullptr;
    }
}
bool CameraController::isOpen()
{
    return handle != nullptr;
}
// cameracontroller.cpp

// ⭐ 设置曝光时间 (传入参数单位通常是微秒 us)
bool CameraController::setExposureTime(float exposureTimeUs)
{
    if(!handle) return false;

    // 1. 关闭自动曝光 (ExposureAuto: 0=Off, 1=Once, 2=Continuous)
    // 不同型号相机枚举名可能不同，一般是 "ExposureAuto"
    int ret = MV_CC_SetEnumValue(handle, "ExposureAuto", 0);
    if(ret != MV_OK) {
        qDebug() << "Failed to turn off Auto Exposure:" << ret;
        // 注意：有些相机可能不支持关闭自动，或者 key 名字不一样，这里不强制返回 false，尝试继续设置
    }

    // 2. 设置具体数值
    ret = MV_CC_SetFloatValue(handle, "ExposureTime", exposureTimeUs);
    if(ret != MV_OK) {
        qDebug() << "Failed to set ExposureTime:" << ret;
        return false;
    }
    return true;
}

// ⭐ 设置增益 (传入参数单位通常是 dB)
bool CameraController::setGain(float gain)
{
    if(!handle) return false;

    // 1. 关闭自动增益 (GainAuto: 0=Off, 1=Once, 2=Continuous)
    int ret = MV_CC_SetEnumValue(handle, "GainAuto", 0);
    if(ret != MV_OK) {
        qDebug() << "Failed to turn off Auto Gain:" << ret;
    }

    // 2. 设置具体数值
    ret = MV_CC_SetFloatValue(handle, "Gain", gain);
    if(ret != MV_OK) {
        qDebug() << "Failed to set Gain:" << ret;
        return false;
    }
    return true;
}
