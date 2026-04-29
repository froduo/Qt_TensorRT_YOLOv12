#include "mainwindow.h"

#include <QApplication>
// --- 必须包含以下头文件 ---
#include <QMetaType>         // 用于 qRegisterMetaType
#include <vector>            // 用于 std::vector
#include <opencv2/opencv.hpp> // 用于 cv::Mat
#include "trt_yolo.h"        // 必须包含定义 Detection 结构体的头文件
// -----------------------
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // --- 关键修改：注册自定义类型以支持跨线程信号槽 ---
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<std::vector<Detection>>("std::vector<Detection>");
    // ------------------------------------------
    MainWindow w;
    w.show();
    return a.exec();
}
