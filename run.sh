#!/bin/bash
# ============================================
# Qt_TensorRT_YOLOv12 启动脚本 (ARM64)
# TensorRT 8.5.2 (apt 安装)
# CUDA 11.4
# ============================================

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 可执行文件就在脚本同级目录下
EXECUTABLE="${SCRIPT_DIR}/Qt_TensorRT_YOLOv12"
if [ ! -f "$EXECUTABLE" ]; then
    # 回退：尝试 build 目录
    BUILD_DIR="${SCRIPT_DIR}/../../build-Qt_TensorRT_YOLOv12-Desktop-Debug"
    if [ ! -d "$BUILD_DIR" ]; then
        BUILD_DIR="${SCRIPT_DIR}/../../build-Qt_TensorRT_YOLOv12-Desktop-Release"
    fi
    EXECUTABLE="${BUILD_DIR}/Qt_TensorRT_YOLOv12"
fi

if [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 找不到可执行文件"
    echo "请先编译项目"
    exit 1
fi

# TensorRT 8.5.2 运行时库路径 (apt 安装到系统目录)
# 库文件在 /usr/lib/aarch64-linux-gnu/，ldconfig 已配置，无需额外设置

# CUDA 运行时库路径
CUDA_LIB_PATH="/usr/local/cuda/lib64"

# 海康威视相机 SDK 库路径
MVS_LIB_PATH="/opt/MVS/lib/aarch64"

# Qt 平台插件路径
QT_PLUGIN_PATH="/usr/lib/aarch64-linux-gnu/qt5/plugins"

# 设置库路径
export LD_LIBRARY_PATH="${CUDA_LIB_PATH}:${MVS_LIB_PATH}:${LD_LIBRARY_PATH}"
export QT_QPA_PLATFORM_PLUGIN_PATH="${QT_PLUGIN_PATH}/platforms"

echo "CUDA 库路径: ${CUDA_LIB_PATH}"
echo "MVS SDK 库路径: ${MVS_LIB_PATH}"
echo "Qt 插件路径: ${QT_PLUGIN_PATH}"
echo "TensorRT: 系统已配置 (apt 安装)"
echo ""

# 切换到脚本所在目录（确保相对路径正常工作）
cd "$SCRIPT_DIR"

# 直接运行
"$EXECUTABLE"
