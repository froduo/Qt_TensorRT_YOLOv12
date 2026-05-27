#include "networkmanager.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

NetworkManager::NetworkManager(QObject *parent) : QObject(parent) {
    socket = new QTcpSocket(this);
    // 初始化重连定时器 (5秒尝试一次)
    reconnectTimer = new QTimer(this);
    reconnectTimer->setInterval(5000);
    // 连接套接字信号
    connect(socket, &QTcpSocket::readyRead, this, &NetworkManager::onReadyRead);
    connect(socket, &QTcpSocket::connected, this, &NetworkManager::onConnected);
    connect(socket, &QTcpSocket::disconnected, this, &NetworkManager::onDisconnected);
    // 处理连接错误信号 (Qt 5.12 兼容：使用旧的 error 信号)
    connect(socket, static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
            this, &NetworkManager::onErrorOccurred);

    // 定时器触发重连
    connect(reconnectTimer, &QTimer::timeout, this, &NetworkManager::attemptReconnect);
}
void NetworkManager::onReadyRead() {
    QByteArray data = socket->readAll();
    QString cmd = QString::fromUtf8(data).trimmed();
    emit commandReceived(cmd); // 转发指令
}

void NetworkManager::onConnected() {
    qDebug() << "Successfully connected to server.";
    reconnectTimer->stop(); // 连接成功，停止重连定时器
    emit statusChanged(true);
}

void NetworkManager::onDisconnected() {
    qDebug() << "Server disconnected.";
    emit statusChanged(false);

    if (m_autoReconnect) {
        reconnectTimer->start(); // 启动定时器，准备下次重连
    }
}
void NetworkManager::connectToServer(const QString &ip, quint16 port) {
    m_host = ip;
    m_port = port;
    m_autoReconnect = true; // 开启重连机制

    attemptReconnect();
}
void NetworkManager::attemptReconnect() {
    if (socket->state() == QAbstractSocket::ConnectedState) {
        return;
    }
    qDebug() << "Attempting to connect to" << m_host << ":" << m_port;
    socket->connectToHost(m_host, m_port);
}
void NetworkManager::onErrorOccurred(QAbstractSocket::SocketError error) {
    Q_UNUSED(error);
    qDebug() << "Socket error:" << socket->errorString();
}
void NetworkManager::sendDetectionResults(const std::vector<Detection>& results) {
    QJsonArray detections;
    for (const auto& det : results) {
        QJsonObject obj;
        obj["class_id"] = det.class_id;
        obj["score"] = det.score;
        obj["x"] = det.x;
        obj["y"] = det.y;
        obj["w"] = det.w;
        obj["h"] = det.h;
        detections.append(obj);
    }
    QJsonObject root;
    root["type"] = "detection";
    root["count"] = static_cast<int>(results.size());
    root["detections"] = detections;
    QJsonDocument doc(root);
    sendResult(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void NetworkManager::disconnect() {
    m_autoReconnect = false;
    reconnectTimer->stop();
    if (socket->state() == QAbstractSocket::ConnectedState ||
        socket->state() == QAbstractSocket::ConnectingState) {
        socket->disconnectFromHost();
    }
}

void NetworkManager::sendResult(const QString &jsonResult) {
    if (socket->state() == QAbstractSocket::ConnectedState) {
        socket->write(jsonResult.toUtf8());
        socket->flush();
    }
}
