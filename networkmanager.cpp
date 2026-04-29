#include "networkmanager.h""
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
    // 处理连接错误信号 (重要：服务器没开时会触发这个)
    // 移除 QOverload，直接指向函数地址
    connect(socket, &QAbstractSocket::errorOccurred,
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
    // 如果已经在连接中或已连接，则不重复操作
    if (socket->state() == QAbstractSocket::UnconnectedState) {
        qDebug() << "Attempting to connect to server:" << m_host << ":" << m_port;
        socket->connectToHost(m_host, m_port);
    }
}
void NetworkManager::onErrorOccurred() {
    qDebug() << "Socket Error:" << socket->errorString();
    emit statusChanged(false);

    if (m_autoReconnect) {
        reconnectTimer->start(); // 出错时（如连接被拒绝）也开启重连
    }
}
void NetworkManager::disconnect() {
    m_autoReconnect = false; // 手动关闭连接时，禁用自动重连
    reconnectTimer->stop();
    socket->disconnectFromHost();
}
bool NetworkManager::isConnected() const {
    return socket->state() == QAbstractSocket::ConnectedState;
}
void NetworkManager::sendDetectionResults(const std::vector<Detection>& results) {
    if (socket->state() != QAbstractSocket::ConnectedState) return;

    QJsonArray rootArray;
    for (const auto& det : results) {
        QJsonObject obj;
        obj["class_id"] = det.class_id;
        obj["score"] = double(det.score);
        obj["x"] = det.x;
        obj["y"] = det.y;
        obj["w"] = det.w;
        obj["h"] = det.h;
        rootArray.append(obj);
    }

    QJsonDocument doc(rootArray);
    socket->write(doc.toJson(QJsonDocument::Compact) + "\n"); // 以换行符结尾方便接收端处理
}
