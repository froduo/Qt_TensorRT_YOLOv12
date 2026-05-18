#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <QString>
#include <QSettings>
#include <QCoreApplication>
#include <QDebug>

struct AppConfig {
    // 相机参数
    QString cameraSN = "SN12345678";
    double exposure = 5000.0;
    double gain = 0.0;

    // 串口参数
    QString serialPort = "ttyUSB0";
    int baudRate = 115200;

    // 网口参数
    QString netIp = "192.168.200.172";
    int netPort = 8080;

    // 推理参数
    double scoreThreshold = 0.25; // 默认得分阈值
    QString enginePath = "./model/yolo12n_trt10_x86.engine"; // 新增推理路径
    QString classesPath = "./model/coco.yaml"; // 检测类别文件路径
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

        s.beginGroup("Serial");
        s.setValue("Port", serialPort);
        s.setValue("Baud", baudRate);
        s.endGroup();

        s.beginGroup("Network");
        s.setValue("IP", netIp);
        s.setValue("Port", netPort);
        s.endGroup();

        s.beginGroup("Inference");
        s.setValue("ScoreThreshold", scoreThreshold);
        s.setValue("EnginePath", enginePath); // 保存路径
        s.setValue("ClassesPath", classesPath); // 保存类别文件路径
        s.endGroup();
        s.sync(); // 强制将内存中的设置立即写入磁盘文件
    }

    // 从文件读取
    void load() {
        QSettings s(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
        cameraSN = s.value("Camera/SN", "SN12345678").toString();
        exposure = s.value("Camera/Exposure", 5000.0).toDouble();
        gain = s.value("Camera/Gain", 0.0).toDouble();

        serialPort = s.value("Serial/Port", "ttyUSB0").toString();
        baudRate = s.value("Serial/Baud", 115200).toInt();

        netIp = s.value("Network/IP", "192.168.200.172").toString();
        netPort = s.value("Network/Port", 8080).toInt();

        enginePath = s.value("Inference/EnginePath", "./model/yolo12n_trt10_x86.engine").toString();
        scoreThreshold = s.value("Inference/ScoreThreshold", 0.25).toDouble();
        classesPath = s.value("Inference/ClassesPath", "./model/coco.yaml").toString();
    }
};

#endif
