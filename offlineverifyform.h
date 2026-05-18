#ifndef OFFLINEVERIFYFORM_H
#define OFFLINEVERIFYFORM_H

#include <QDialog>
#include <QProcess>
#include <QPlainTextEdit>
#include <QElapsedTimer>
#include <QDir>
#include <opencv2/opencv.hpp>
#include "trt_yolo.h"
#include "inferthread.h"
#include "imageview.h"

namespace Ui {
class OfflineVerifyForm;
}

class OfflineVerifyForm : public QDialog
{
    Q_OBJECT

public:
    explicit OfflineVerifyForm(QWidget *parent = nullptr);
    ~OfflineVerifyForm();

private:
    Ui::OfflineVerifyForm *ui;

    void appendLog(const QString& text);
    void setTaskState(const QString& state);
    QString currentModelPath() const;
    int currentBackendIndex() const;
    QString backendName() const;
    bool loadOfflineModel();
    void runOfflineInfer();
    void drawDetections(cv::Mat& img, const std::vector<Detection>& results);
    QString classNameById(int classId) const;
    static QString resolveTrtexecPath();
    void augmentTrtexecLibraryPath(QProcessEnvironment& env) const;

    cv::Mat m_offlineImage;
    QString m_offlineImagePath;
    TrtYolo* m_offlineTrtYolo = nullptr;
    TrtLogger* m_offlineLogger = nullptr;
    cv::dnn::Net m_offlineCpuNet;
    cv::dnn::Net m_offlineCudaNet;
    bool m_offlineCpuReady = false;
    bool m_offlineCudaReady = false;
    std::vector<std::string> m_offlineClassNames;
    QString m_offlineLoadedModelPath;
    QProcess* m_trtexec = nullptr;
    QString m_trtexecEnginePath;

    QString m_lastModelDir;
    QString m_lastImageDir;

    void saveLastModelPath(const QString& path);
    QString restoreLastModelPath();
    void saveLastImagePath(const QString& path);
    QString restoreLastImagePath();

    void displayResultImage(const cv::Mat& resultImg);

private slots:
    void handleSelectModel();
    void handleSelectImage();
    void handleStartInfer();
    void handleEnvCheck();
    void handleOnnxToEngine();
    void handleExportLog();
    void handleDeviceChanged(int index);
    void onTrtexecFinished(int exitCode, QProcess::ExitStatus status);
    void onTrtexecReadyRead();
    void onTrtexecError(QProcess::ProcessError err);
};

#endif // OFFLINEVERIFYFORM_H
