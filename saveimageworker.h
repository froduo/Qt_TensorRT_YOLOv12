#ifndef SAVEIMAGEWORKER_H
#define SAVEIMAGEWORKER_H

#include <QObject>
#include <QImage>
#include <QString>
#include <QDir>
#include <QDateTime>
#include <QMutex>
#include <QAtomicInt>

/**
 * @brief 后台图像保存工作类
 * 通过信号/槽机制在独立线程中执行图像保存，不阻塞 UI 线程
 */
class SaveImageWorker : public QObject
{
    Q_OBJECT

public:
    explicit SaveImageWorker(QObject *parent = nullptr);
    ~SaveImageWorker();

    /// 设置保存参数（线程安全）
    void setSaveParams(const QString &basePath, const QString &format, int jpegQuality);

    /// 获取当前保存参数
    QString basePath() const;
    QString saveFormat() const;
    int jpegQuality() const;

    /// 磁盘满标志
    void setDiskFull(bool full);
    bool isDiskFull() const;

public slots:
    /// 接收保存任务（在后台线程中执行）
    /// @param img 图像
    /// @param hasDetection 是否有检测结果
    /// @param cameraId 相机标识（1 或 2）
    void onSaveImage(const QImage &img, bool hasDetection, int cameraId = 1);

signals:
    /// 保存完成信号（可选的，用于日志或状态更新）
    void imageSaved(const QString &filePath);

private:
    mutable QMutex m_mutex;
    QString m_basePath;
    QString m_saveFormat;
    int m_jpegQuality = 95;
    QAtomicInt m_diskFull{0}; // 0=false, 1=true
};

#endif // SAVEIMAGEWORKER_H
