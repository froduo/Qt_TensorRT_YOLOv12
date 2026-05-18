# Qt TensorRT YOLOv12

工业视觉监控核心应用，基于 `Qt + TensorRT + OpenCV + Hikrobot MVS SDK`，支持实时采集、GPU 推理、TCP 结果输出、串口云台控制和在线参数配置。

![platform](https://img.shields.io/badge/platform-linux-blue)
![language](https://img.shields.io/badge/C%2B%2B-17-orange)
![framework](https://img.shields.io/badge/Qt-Widgets-41CD52)
![inference](https://img.shields.io/badge/TensorRT-10.x-76B900)
![status](https://img.shields.io/badge/status-active-success)

## 项目亮点

- 实时视频流检测：相机采集线程与推理解耦，低阻塞高响应
- TensorRT 推理加速：面向 YOLOv12 Engine 文件直接部署
- 双通道输出：TCP 发送检测框结果，串口发送云台控制指令
- 配置可视化：参数面板支持模型路径、阈值、IP、串口、相机参数
- 在线热更新：保存参数后可立即生效，支持引擎热重载
- 工程化细节：版本号注入、日志落盘、断线自动重连
- **UI 优化**：莫兰迪色系界面设计，科技感菜单与状态显示
- **图片交互**：双击缩放、鼠标拖拽平移、滚轮缩放
- **离线验证**：支持图片推理测试，带推理时长统计

## 演示截图

建议在仓库中新增如下资源并取消注释：

```md
<!--
![main-ui](docs/images/main-ui.png)
![setting-ui](docs/images/setting-ui.png)
-->
```

## 快速开始

### 1) 环境依赖

- Linux (x86_64)
- NVIDIA GPU + CUDA
- TensorRT 10.x
- OpenCV 4.x
- Qt (Widgets / SerialPort / Network)
- Hikrobot MVS SDK
- C++17 编译器

工程默认依赖路径写在 `Qt_TensorRT_YOLOv12.pro`：

- CUDA: `/usr/local/cuda`
- TensorRT: `/usr/local/TensorRT-10.3`
- OpenCV: `/usr/local/include/opencv4`
- MVS: `/opt/MVS`

如与本机不一致，请按实际路径修改 `INCLUDEPATH` 与 `LIBS`。

### 2) 编译

```bash
qmake Qt_TensorRT_YOLOv12.pro
make -j$(nproc)
```

Qt Creator 用户可直接打开 `Qt_TensorRT_YOLOv12.pro`，选择 Kit 后构建。

### 3) 模型准备

默认模型路径（运行目录）：

- `./model/yolo12n_trt10_x86.engine`

工程已配置 qmake 链接后自动复制模型到构建目录 `model/`。如果复制失败，手动创建 `model/` 并放入 `.engine` 文件即可。

### 4) 启动流程

1. 连接相机、串口设备和 TCP 服务端（可选）
2. 运行程序
3. 点击 `START CAMERA` 开始实时检测
4. 通过顶部菜单 `Settings` -> `System Config` 修改参数并保存
5. 通过顶部菜单 `Settings` -> `Offline Infer` 进行离线图片测试
6. 通过顶部菜单 `Settings` -> `Offline Verify` 进行批量验证

## 系统架构

```text
Camera (MVS SDK)
   -> GrabThread (采集线程)
      -> InferThread (推理线程, TensorRT)
         -> MainWindow (UI渲染/状态展示)
            -> NetworkManager (TCP发送检测结果)
            -> SerialManager  (串口发送云台控制)
```

入口 `main.cpp` 中已注册跨线程类型：

- `cv::Mat`
- `std::vector<Detection>`

## 功能说明

### 实时检测

- 打开相机后开始抓帧
- 推理线程完成 `preprocess -> infer -> postprocess -> draw`
- UI 实时显示帧、FPS、推理时延、检测数量
- **图片交互**：双击缩放自适应、鼠标拖拽平移、滚轮缩放

### 离线推理

- 支持加载本地图片（jpg/png/bmp）
- 支持推理时长统计显示
- **错误检查**：模型路径验证、文件格式验证、异常捕获

### 离线验证

- 支持选择模型文件和测试图片
- 批量推理验证功能
- 推理日志带时长信息

### 参数配置

通过 `Settings -> System Config` 弹窗可配置：

- Camera: SN、Exposure、Gain
- Communication: TCP IP/Port、串口、波特率
- Inference: EnginePath、ScoreThreshold、ClassesPath
- **类别配置**：支持导入自定义类别文件（默认加载 coco.yaml）

保存后会写入 `config.ini` 并立即应用到运行模块。

## 配置文件

配置文件位于：

- `应用程序目录/config.ini`

示例：

```ini
[Camera]
SN=SN12345678
Exposure=5000
Gain=0

[Serial]
Port=ttyUSB0
Baud=115200

[Network]
IP=192.168.200.172
Port=8080

[Inference]
EnginePath=./model/yolo12n_trt10_x86.engine
ScoreThreshold=0.25
ClassesPath=./model/coco.yaml
```

## 通信协议

### TCP 输出（检测结果）

每帧发送 JSON 数组，末尾带换行 `\n`：

```json
[
  {"class_id":0,"score":0.92,"x":120,"y":80,"w":150,"h":300},
  {"class_id":2,"score":0.88,"x":420,"y":200,"w":210,"h":130}
]
```

### 串口输出（云台控制）

检测到目标后，基于偏移调用云台控制；当前示例命令：

- `SA0xxx#`

其中 `xxx` 为递增序号，可按下位机协议自行扩展。

## 目录结构

```text
.
├── main.cpp
├── mainwindow.h/.cpp/.ui
├── settingform.h/.cpp/.ui
├── offlineverifyform.h/.cpp/.ui
├── imageview.h/.cpp
├── cameracontroller.h/.cpp
├── grabthread.h/.cpp
├── inferthread.h/.cpp
├── trt_yolo.h/.cpp
├── networkmanager.h/.cpp
├── serialmanager.h/.cpp
├── app_config.h
├── logger.h
├── model/
│   ├── yolo12n_trt10_x86.engine
│   └── coco.yaml
└── Qt_TensorRT_YOLOv12.pro
```

## 日志与版本

- 日志目录：`logs/`
- 日志文件：`run_log_YYYYMMDD.txt`
- 常用日志宏：`LOG_INFO` / `LOG_WARN` / `LOG_ERR`
- 构建版本格式：`v<major>.<minor>.<YYYYMMDDHHMMSS>`

## UI 功能说明

### 主界面布局

- **图像显示区**：实时视频流显示，支持交互操作
  - 双击：自适应缩放
  - 拖拽：平移查看
  - 滚轮：缩放

- **控制面板**：相机控制按钮
  - `START CAMERA`：启动相机
  - `STOP SYSTEM`：停止系统

- **状态面板**（右下角三个分组）：
  - **INFERENCE**：FPS、推理时间、检测数量
  - **CAMERA**：曝光时间、增益
  - **STATUS**：相机连接状态、模型加载状态

- **顶部菜单**：
  - `Settings -> Offline Infer`：离线图片推理
  - `Settings -> Offline Verify`：离线批量验证
  - `Settings -> System Config`：系统参数配置

### 视觉设计

- **配色方案**：莫兰迪绿色系主题
- **状态指示**：
  - 正常状态：绿色发光效果
  - 异常状态：红色发光效果
- **容器样式**：圆角边框、半透明背景

## 常见问题

### 模型加载失败

- 确认 `.engine` 存在且路径正确
- 确认 TensorRT 与 engine 版本匹配
- 检查 CUDA/TensorRT 动态库可见性

### 无法发现相机

- 确认 MVS SDK 安装完整
- 确认设备供电与连接正常
- 检查配置中的相机 SN

### 串口打不开

- 检查设备名（如 `ttyUSB0`）
- 检查用户权限（`dialout`）
- 校验波特率与协议

### TCP 不通

- 检查目标 IP/Port 与防火墙
- 程序默认支持自动重连（5 秒周期）

### GLIBCXX 版本错误

- 运行时提示 `GLIBCXX_3.4.30 not found`
- 解决方案：设置环境变量 `LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libstdc++.so.6`

## 更新日志

### v1.1.0 (2026-05-18)

**UI 优化**
- 窗口尺寸调整为 1280x768
- 莫兰迪绿色系主题设计
- 科技感菜单样式优化
- 状态面板发光效果增强

**交互功能**
- 图片双击自适应缩放
- 鼠标拖拽平移
- 滚轮缩放控制

**功能增强**
- 检测类别配置文件支持（coco.yaml）
- 离线验证功能完善
- 文件对话框修复（防止重复弹窗）
- 状态标签样式持久化

**Bug 修复**
- 状态标签样式丢失问题
- 文件选择重复触发问题

## Roadmap

- [ ] 增加类别过滤与 ROI 检测区域
- [ ] 支持多路视频输入
- [ ] 引入更规范的串口协议（校验位/CRC）
- [ ] 增加性能统计导出
- [ ] 增加 CI 构建与基础测试

## 贡献指南

欢迎提交 Issue / PR。建议流程：

1. Fork 并新建功能分支
2. 提交修改并附复现步骤
3. 在 PR 中说明测试环境与结果

## 许可证

当前仓库未显式声明许可证。对外发布前建议补充 `LICENSE`，并确认第三方依赖许可条款（Qt、OpenCV、TensorRT、MVS SDK）。
