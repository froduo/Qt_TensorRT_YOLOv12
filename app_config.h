#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <QString>
#include <QSettings>
#include <QCoreApplication>
#include <QDebug>

struct AppConfig {
    // 相机1参数
    QString cameraSN = "SN12345678";
    double exposure = 5000.0;
    double gain = 0.0;

    // 相机2参数
    QString cameraSN2 = "SN87654321";
    double exposure2 = 5000.0;
    double gain2 = 0.0;

    // 串口参数
    QString serialPort = "ttyUSB0";
    int baudRate = 115200;

    // 网口参数
    QString netIp = "192.168.200.172";
    int netPort = 8080;

    // 主界面标题
    QString windowTitle = "AI智能检测系统";

    // 图像保存参数
    bool saveNG = false;          // 检测到物体时保存到NG文件夹
    bool saveOK = false;          // 未检测到物体时保存到OK文件夹
    QString savePath = "./save_images"; // 保存根路径
    QString saveFormat = "jpg";   // 保存格式: jpg/bmp/tif
    int jpegQuality = 95;         // JPEG压缩质量 1-100

    // 推理参数 - 相机1
    bool enableInference = true;   // 是否启用推理
    double scoreThreshold = 0.25; // 默认得分阈值
    QString enginePath = "./model/yolo12n_trt8.5.2_arm64.engine"; // 推理引擎路径 (ARM64)
    QString classesPath = "./model/coco.yaml"; // 检测类别文件路径

    // 推理参数 - 相机2
    bool enableInference2 = true;  // 是否启用推理
    double scoreThreshold2 = 0.25;
    QString enginePath2 = "./model/yolo12n_trt8.5.2_arm64.engine";
    QString classesPath2 = "./model/coco.yaml";

    // 保存到文件
    void save() {
        QString path = QCoreApplication::applicationDirPath() + "/config.ini";
        qDebug() << "Saving to:" << path;
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup("Camera");
        s.setValue("SN", cameraSN);
        s.setValue("Exposure", exposure);
        s.setValue("Gain", gain);
        s.endGroup();

        s.beginGroup("Camera2");
        s.setValue("SN", cameraSN2);
        s.setValue("Exposure", exposure2);
        s.setValue("Gain", gain2);
        s.endGroup();

        s.beginGroup("Serial");
        s.setValue("Port", serialPort);
        s.setValue("Baud", baudRate);
        s.endGroup();

        s.beginGroup("Network");
        s.setValue("IP", netIp);
        s.setValue("Port", netPort);
        s.endGroup();

        s.beginGroup("Display");
        s.setValue("WindowTitle", windowTitle);
        s.endGroup();

        s.beginGroup("ImageSave");
        s.setValue("SaveNG", saveNG);
        s.setValue("SaveOK", saveOK);
        s.setValue("SavePath", savePath);
        s.setValue("SaveFormat", saveFormat);
        s.setValue("JpegQuality", jpegQuality);
        s.endGroup();

        s.beginGroup("Inference");
        s.setValue("EnableInference", enableInference);
        s.setValue("ScoreThreshold", scoreThreshold);
        s.setValue("EnginePath", enginePath);
        s.setValue("ClassesPath", classesPath);
        s.endGroup();

        s.beginGroup("Inference2");
        s.setValue("EnableInference", enableInference2);
        s.setValue("ScoreThreshold", scoreThreshold2);
        s.setValue("EnginePath", enginePath2);
        s.setValue("ClassesPath", classesPath2);
        s.endGroup();
        s.sync();
    }

    // 从文件读取
    void load() {
        QSettings s(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
        cameraSN = s.value("Camera/SN", "SN12345678").toString();
        exposure = s.value("Camera/Exposure", 5000.0).toDouble();
        gain = s.value("Camera/Gain", 0.0).toDouble();

        cameraSN2 = s.value("Camera2/SN", "SN87654321").toString();
        exposure2 = s.value("Camera2/Exposure", 5000.0).toDouble();
        gain2 = s.value("Camera2/Gain", 0.0).toDouble();

        serialPort = s.value("Serial/Port", "ttyUSB0").toString();
        baudRate = s.value("Serial/Baud", 115200).toInt();

        netIp = s.value("Network/IP", "192.168.200.172").toString();
        netPort = s.value("Network/Port", 8080).toInt();

        windowTitle = s.value("Display/WindowTitle", "AI智能检测系统").toString();

        saveNG = s.value("ImageSave/SaveNG", false).toBool();
        saveOK = s.value("ImageSave/SaveOK", false).toBool();
        savePath = s.value("ImageSave/SavePath", "./save_images").toString();
        saveFormat = s.value("ImageSave/SaveFormat", "jpg").toString();
        jpegQuality = s.value("ImageSave/JpegQuality", 95).toInt();

        enableInference = s.value("Inference/EnableInference", true).toBool();
        enginePath = s.value("Inference/EnginePath", "./model/yolo12n_trt8.5.2_arm64.engine").toString();
        scoreThreshold = s.value("Inference/ScoreThreshold", 0.25).toDouble();
        classesPath = s.value("Inference/ClassesPath", "./model/coco.yaml").toString();

        enableInference2 = s.value("Inference2/EnableInference", true).toBool();
        enginePath2 = s.value("Inference2/EnginePath", "./model/yolo12n_trt8.5.2_arm64.engine").toString();
        scoreThreshold2 = s.value("Inference2/ScoreThreshold", 0.25).toDouble();
        classesPath2 = s.value("Inference2/ClassesPath", "./model/coco.yaml").toString();
    }
};

#endif
