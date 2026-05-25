#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <QDir>
#include <QDebug>

class Logger {
public:
    enum LogLevel { INFO, WARN, ERR };

    static Logger* getInstance() {
        static Logger instance;
        return &instance;
    }

    void log(const QString &message, LogLevel level = INFO) {
        QMutexLocker locker(&mutex); // 保证多线程安全

        QString levelStr;
        switch (level) {
        case INFO: levelStr = "[INFO]"; break;
        case WARN: levelStr = "[WARN]"; break;
        case ERR:  levelStr = "[ERROR]"; break;
        }

        QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        QString logLine = QString("%1 %2 %3").arg(timeStr, levelStr, message);

        // 1. 输出到控制台
        if (level == ERR) qCritical() << logLine;
        else qDebug() << logLine;

        // 2. 写入文件
        if (logFile.isOpen()) {
            QTextStream out(&logFile);
            out << logLine << "\n";
            out.flush();
        }
    }
    // ⭐ 清理超过30天的旧日志文件
    void cleanOldLogs() {
        QDir dir("logs");
        if (!dir.exists()) return;

        dir.setFilter(QDir::Files | QDir::NoSymLinks);
        dir.setNameFilters(QStringList() << "run_log_*.txt");
        QFileInfoList list = dir.entryInfoList();
        QDateTime limit = QDateTime::currentDateTime().addDays(-14); // 保留14天
        int removedCount = 0;

        for (int i = 0; i < list.size(); ++i) {
            if (list.at(i).lastModified() < limit) {
                if (QFile::remove(list.at(i).absoluteFilePath())) {
                    removedCount++;
                }
            }
        }

        if (removedCount > 0) {
            qDebug() << "[Logger] Cleaned up" << removedCount << "old log files (older than 30 days)";
        }
    }

private:
    Logger() {
        QDir logDir;
        if (!logDir.exists("logs")) {
            logDir.mkdir("logs");
        }
        // ⭐ 启动时自动清理超过30天的旧日志
        cleanOldLogs();

        QString fileName = QString("logs/run_log_%1.txt")
                               .arg(QDateTime::currentDateTime().toString("yyyyMMdd"));
        logFile.setFileName(fileName);
        logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }

    ~Logger() {
        if (logFile.isOpen()) logFile.close();
    }

    QFile logFile;
    QMutex mutex;
};

// 定义宏方便使用
#define LOG_INFO(msg) Logger::getInstance()->log(msg, Logger::INFO)
#define LOG_WARN(msg) Logger::getInstance()->log(msg, Logger::WARN)
#define LOG_ERR(msg)  Logger::getInstance()->log(msg, Logger::ERR)

#endif
