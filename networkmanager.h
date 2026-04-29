#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <QObject>
#include <QTcpSocket>
#include "trt_yolo.h" // 引用 Detection 结构体
#include<QTimer>
class NetworkManager : public QObject {
    Q_OBJECT
public:
    explicit NetworkManager(QObject *parent = nullptr);
    void connectToServer(const QString &ip, uint16_t port);
    void disconnect();

    bool isConnected() const;
    // 发送检测结果
    void sendDetectionResults(const std::vector<Detection>& results);
signals:
    void commandReceived(QString cmd); // 收到指令信号
    void statusChanged(bool connected); // 连接状态改变信号

private slots:
    void onReadyRead(); // 处理接收数据
    void onConnected();
    void onDisconnected();
    void onErrorOccurred(); // 处理连接错误（如服务器没开）
    void attemptReconnect(); // 执行重连逻辑

private:
    QTcpSocket *socket;
    QTimer *reconnectTimer; // 重连定时器

    QString m_host;   // 记录IP
    quint16 m_port;   // 记录端口
    bool m_autoReconnect = true; // 是否开启自动重连开关
};

#endif
