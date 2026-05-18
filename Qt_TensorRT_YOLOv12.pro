QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    cameracontroller.cpp \
    grabthread.cpp \
    imageview.cpp \
    inferthread.cpp \
    main.cpp \
    mainwindow.cpp \
    networkmanager.cpp \
    offlineverifyform.cpp \
    serialmanager.cpp \
    settingform.cpp \
    trt_yolo.cpp

HEADERS += \
    app_config.h \
    cameracontroller.h \
    grabthread.h \
    imageview.h \
    inferthread.h \
    logger.h \
    mainwindow.h \
    networkmanager.h \
    offlineverifyform.h \
    serialmanager.h \
    settingform.h \
    trt_yolo.h

FORMS += \
    mainwindow.ui \
    offlineverifyform.ui \
    settingform.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
QT += widgets
QT += serialport network
CONFIG += c++17

INCLUDEPATH +=/usr/local/cuda/include \
    /usr/local/TensorRT-10.3/include \
    /usr/local/include/opencv4

LIBS += \
    -L/usr/local/cuda/lib64 \
    -L/usr/local/TensorRT-10.3/targets/x86_64-linux-gnu/lib \
    -lopencv_core \
    -lopencv_imgproc \
    -lopencv_highgui \
    -lopencv_imgcodecs \
    -lopencv_videoio \
    -lopencv_dnn \
    -lnvinfer \
    -lnvinfer_plugin \
    -lnvonnxparser \
    -lcudart

INCLUDEPATH += /opt/MVS/include
LIBS += -L/opt/MVS/lib/64 -lMvCameraControl

DISTFILES += \
    model/yolo12n_trt10_x86.engine

    # ==========================================
    # ⭐ 新增：编译后自动复制模型文件到构建目录
    # ==========================================

    # 定义源文件路径 (项目目录下的 model/xxx)
    MODEL_SOURCE = $$PWD/model/yolo12n_trt10_x86.engine

    # 定义目标文件夹 (构建目录下的 model 文件夹)
    MODEL_DEST_DIR = $$OUT_PWD/model

    # Linux/Unix 下的复制命令：
    # 1. mkdir -p 创建目录 (如果不存在)
    # 2. cp -f 强制复制文件
    QMAKE_POST_LINK += mkdir -p $$MODEL_DEST_DIR && cp -f $$MODEL_SOURCE $$MODEL_DEST_DIR

# --- 自动生成版本号逻辑 ---

# 获取系统当前日期 (格式: YYYYMMDD)
# Linux 环境
unix {
    BUILD_DATE = $$system(date +%Y%m%d%H%M%S)
}
# Windows 环境 (如果以后切换到 Windows 开发)
# win32 {
#     BUILD_DATE = $$system(echo %date:~0,4%%date:~5,2%%date:~8,2%)
# }

# 定义主版本号
MAJOR_VERSION = 1
MINOR_VERSION = 0

# 将版本号注入到代码宏定义中
# 结果类似于 v1.0.20231027
DEFINES += APP_VERSION_STR=\\\"v$${MAJOR_VERSION}.$${MINOR_VERSION}.$${BUILD_DATE}\\\"

# 如果你还想要编译的具体时间（时分），可以再增加一个宏
# UNIX 环境下：BUILD_TIME = $$system(date +%H%M%S)
