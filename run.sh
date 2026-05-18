#!/bin/bash
# ============================================
# Qt_TensorRT_YOLOv12 启动脚本
# 解决 Anaconda libstdc++ 版本冲突问题
# ============================================

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../../build-Qt_TensorRT_YOLOv12-Desktop-Debug"

# 检查构建目录是否存在
if [ ! -d "$BUILD_DIR" ]; then
    BUILD_DIR="${SCRIPT_DIR}/../../build-Qt_TensorRT_YOLOv12-Desktop-Release"
fi

# 检查可执行文件
EXECUTABLE="${BUILD_DIR}/Qt_TensorRT_YOLOv12"
if [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 找不到可执行文件 $EXECUTABLE"
    echo "请先编译项目"
    exit 1
fi

# 系统的 libstdc++ (包含 GLIBCXX_3.4.30)
SYSTEM_LIBSTDCXX="/usr/lib/x86_64-linux-gnu/libstdc++.so.6"

if [ ! -f "$SYSTEM_LIBSTDCXX" ]; then
    echo "警告: 未找到系统 libstdc++，尝试直接运行..."
    "$EXECUTABLE"
else
    echo "使用系统 libstdc++: $SYSTEM_LIBSTDCXX"
    echo "启动程序: $EXECUTABLE"
    echo ""
    LD_PRELOAD="$SYSTEM_LIBSTDCXX" "$EXECUTABLE"
fi
