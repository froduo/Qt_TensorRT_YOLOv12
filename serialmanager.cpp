#include "serialmanager.h"
#include<QDebug>

SerialManager::SerialManager(QObject *parent) : QObject(parent) {}

bool SerialManager::openPort(const QString &portName, int baudRate)
{
    QString actualPort = portName;

    // 1. 自动寻找逻辑
        if (actualPort.isEmpty()) {
        const auto infos = QSerialPortInfo::availablePorts();
        for (const QSerialPortInfo &info : infos) {
            // 注意：CH340 可能在 description 里，也可能在 manufacturer 里
            if (info.description().contains("CH340") || info.portName().contains("ttyUSB")) {
                actualPort = info.portName(); // Qt setPortName 推荐只传 "ttyUSB0" 而不是 "/dev/ttyUSB0"
                break;
            }
        }
    }

    if (serial.isOpen()) serial.close();
    // ⭐ 修复点：使用 actualPort 而不是 portName
    // 且如果 actualPort 是 "/dev/ttyUSB0"，Qt 有时更喜欢直接用 "ttyUSB0"
    if(actualPort.startsWith("/dev/")) {
        actualPort = actualPort.mid(5);
    }
    serial.setPortName(portName);
    serial.setBaudRate(baudRate);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    bool ok = serial.open(QIODevice::ReadWrite);
    if(ok) {
        emit statusChanged(true);
        qDebug() << "Successfully opened port:" << actualPort;
    } else {
        emit statusChanged(false);
        qDebug() << "Failed to open port:" << actualPort << "Reason:" << serial.errorString();
    }
    return ok;
}

void SerialManager::closePort() { serial.close(); }

void SerialManager::controlGimbal(int offsetX, int offsetY) {
    if (!serial.isOpen()) return;
    static int i=0;
    i=i>255?0:i+1;

    // 这里编写你的云台控制协议逻辑
    // 示例：发送文本指令 "MOVE X:10 Y:-5\n"
    // QString cmd   QString("MOVE X:%1 Y:%2\n").arg(offsetX).arg(offsetY);
    QString cmd ="SA0"+ QString("%1").arg(i, 3, 10, QChar('0'))+"#";
    serial.write(cmd.toUtf8());
}
