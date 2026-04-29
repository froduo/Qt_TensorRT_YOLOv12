#ifndef SERIALMANAGER_H
#define SERIALMANAGER_H

#include <QObject>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>

class SerialManager : public QObject {
    Q_OBJECT
public:
    explicit SerialManager(QObject *parent = nullptr);
    bool openPort(const QString &portName, int baudRate);
    void closePort();
    bool isOpen() const { return serial.isOpen(); }

    // 发送控制指令给云台（例如：根据检测到的中心点偏差控制旋转）
    void controlGimbal(int offsetX, int offsetY);
signals:
    void statusChanged(bool connected);

    // 在 openPort 成功后 emit statusChanged(true);
    // 在 closePort 后 emit statusChanged(false);
private:
    QSerialPort serial;
};

#endif
