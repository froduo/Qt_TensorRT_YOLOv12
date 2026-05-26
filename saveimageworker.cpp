#include "saveimageworker.h"
#include "logger.h"
#include <QImageWriter>

SaveImageWorker::SaveImageWorker(QObject *parent)
    : QObject(parent)
{
}

SaveImageWorker::~SaveImageWorker()
{
}

void SaveImageWorker::setSaveParams(const QString &basePath, const QString &format, int jpegQuality)
{
    QMutexLocker locker(&m_mutex);
    m_basePath = basePath;
    m_saveFormat = format;
    m_jpegQuality = qBound(1, jpegQuality, 100);
}

QString SaveImageWorker::basePath() const
{
    QMutexLocker locker(&m_mutex);
    return m_basePath;
}

QString SaveImageWorker::saveFormat() const
{
    QMutexLocker locker(&m_mutex);
    return m_saveFormat;
}

int SaveImageWorker::jpegQuality() const
{
    QMutexLocker locker(&m_mutex);
    return m_jpegQuality;
}

void SaveImageWorker::setDiskFull(bool full)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_diskFull.storeRelaxed(full ? 1 : 0);
#else
    m_diskFull.store(full ? 1 : 0);
#endif
}

bool SaveImageWorker::isDiskFull() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return m_diskFull.loadRelaxed() != 0;
#else
    return m_diskFull.load() != 0;
#endif
}

void SaveImageWorker::onSaveImage(const QImage &img, bool hasDetection, int cameraId)
{
    // 磁盘满时不保存
    if (isDiskFull()) {
        return;
    }

    // 获取保存参数（线程安全）
    QString basePath, saveFormat;
    int quality;
    {
        QMutexLocker locker(&m_mutex);
        basePath = m_basePath;
        saveFormat = m_saveFormat;
        quality = m_jpegQuality;
    }

    if (basePath.isEmpty()) {
        return;
    }

    // 确定子目录
    QString subDir = hasDetection ? "NG" : "OK";

    // 相机标识目录
    QString camDir = (cameraId == 2) ? "相机2" : "相机1";

    // 构建路径：basePath/yyyyMMdd/相机1/NG/ 或 basePath/yyyyMMdd/相机2/OK/
    QString dateDir = QDateTime::currentDateTime().toString("yyyyMMdd");
    QString fullDir = basePath + "/" + dateDir + "/" + camDir + "/" + subDir;

    if (!QDir().mkpath(fullDir)) {
        LOG_WARN("[SaveImageWorker] Failed to create directory: " + fullDir);
        return;
    }

    // 生成文件名：时间戳_序号.格式
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    QString fileName = QString("%1.%2").arg(timestamp).arg(saveFormat);
    QString fullPath = fullDir + "/" + fileName;

    // 保存图像
    bool saved = false;
    if (saveFormat == "jpg") {
        saved = img.save(fullPath, "JPEG", quality);
    } else if (saveFormat == "bmp") {
        saved = img.save(fullPath, "BMP");
    } else if (saveFormat == "tif") {
        saved = img.save(fullPath, "TIFF");
    } else {
        saved = img.save(fullPath);
    }

    if (saved) {
        emit imageSaved(fullPath);
    } else {
        LOG_WARN("[SaveImageWorker] Failed to save image: " + fullPath);
    }
}
