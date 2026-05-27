#include "trt_yolo.h"
#include <fstream>
#include <iostream>
#include <opencv2/dnn.hpp>
#include <NvOnnxParser.h>

using namespace nvinfer1;

static inline float sigmoid(float x)
{
    return 1.f / (1.f + expf(-x));
}

static const std::vector<std::string> defaultClassNames = {
    "person","bicycle","car","motorcycle","airplane","bus","train","truck",
    "boat","traffic light","fire hydrant","stop sign","parking meter","bench",
    "bird","cat","dog","horse","sheep","cow","elephant","bear","zebra","giraffe",
    "backpack","umbrella","handbag","tie","suitcase","frisbee","skis","snowboard",
    "sports ball","kite","baseball bat","baseball glove","skateboard","surfboard",
    "tennis racket","bottle","wine glass","cup","fork","knife","spoon","bowl",
    "banana","apple","sandwich","orange","broccoli","carrot","hot dog","pizza",
    "donut","cake","chair","couch","potted plant","bed","dining table","toilet",
    "tv","laptop","mouse","remote","keyboard","cell phone","microwave","oven",
    "toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear",
    "hair drier","toothbrush"
};

TrtYolo::TrtYolo(const std::string& enginePath, ILogger& logger, const std::string& classesPath)
    : logger(logger)
{
    bool ok = loadEngine(enginePath);
    if (!ok) {
        m_lastError = "Failed to load engine: " + enginePath;
        return;
    }
    if (!classesPath.empty()) {
        loadClasses(classesPath);
    } else {
        classNames = defaultClassNames;
    }
}

TrtYolo::~TrtYolo()
{
    // TensorRT 8.x 使用 delete 释放内存
    if (context) delete context;
    if (engine) delete engine;
    if (runtime) delete runtime;

    if (buffers[0]) cudaFree(buffers[0]);
    if (buffers[1]) cudaFree(buffers[1]);

    cudaStreamDestroy(stream);
}

size_t TrtYolo::getSizeByDim(const Dims& dims)
{
    size_t size = 1;
    for (int i = 0; i < dims.nbDims; i++)
        size *= dims.d[i];
    return size;
}

bool TrtYolo::loadEngine(const std::string& enginePath)
{
    std::ifstream file(enginePath, std::ios::binary);
    if (!file.is_open()) {
        m_lastError = "Cannot open engine file: " + enginePath;
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> engineData(size);
    file.read(engineData.data(), size);
    file.close();

    runtime = createInferRuntime(logger);
    if (!runtime) {
        m_lastError = "Failed to create TensorRT runtime";
        return false;
    }

    engine = runtime->deserializeCudaEngine(engineData.data(), size);
    if (!engine) {
        m_lastError = "Failed to deserialize TensorRT engine.\n"
                      "The engine file may be built with a different TensorRT version or for a different architecture.\n"
                      "Expected: TensorRT 8.5.2 on aarch64\n"
                      "File: " + enginePath;
        return false;
    }

    context = engine->createExecutionContext();
    if (!context) {
        m_lastError = "Failed to create TensorRT execution context";
        delete engine;
        engine = nullptr;
        return false;
    }

    // --- TensorRT 8.x 兼容的 I/O 处理逻辑 ---
    int nbBindings = engine->getNbBindings();
    for (int i = 0; i < nbBindings; i++)
    {
        const char* bindingName = engine->getBindingName(i);
        bool isInput = engine->bindingIsInput(i);

        if (isInput) {
            inputIndex = i;
            inputTensorName = bindingName;
            std::cout << "Input Binding [" << i << "]: " << bindingName << std::endl;
        } else {
            outputIndex = i;
            outputTensorName = bindingName;
            std::cout << "Output Binding [" << i << "]: " << bindingName << std::endl;
        }
    }

    // 设置输入维度 (TensorRT 8.x 使用 setBindingDimensions)
    context->setBindingDimensions(inputIndex, Dims4(1, 3, inputH, inputW));

    // 计算输入输出大小
    inputSize = 1 * 3 * inputH * inputW * sizeof(float);

    // 获取输出维度 (TensorRT 8.x 使用 getBindingDimensions)
    auto outDims = engine->getBindingDimensions(outputIndex);
    outputSize = getSizeByDim(outDims) * sizeof(float);

    // ⭐ 日志：输出模型路径和张量形状
    std::cout << "[TrtYolo] Engine: " << enginePath << std::endl;
    std::cout << "[TrtYolo] Output shape: ";
    for(int i=0; i<outDims.nbDims; i++) std::cout << outDims.d[i] << " ";
    std::cout << std::endl;
    std::cout << "[TrtYolo] Output size: " << outputSize << " bytes" << std::endl;

    hostInput.resize(inputSize/sizeof(float));
    hostOutput.resize(outputSize/sizeof(float));

    cudaMalloc(&buffers[0], inputSize);
    cudaMalloc(&buffers[1], outputSize);

    cudaStreamCreate(&stream);
    return true;
}

void TrtYolo::preprocess(const cv::Mat& img)
{
    lastImage = img.clone();
    int w = img.cols;
    int h = img.rows;
    scale = std::min((float)inputW / w, (float)inputH / h);
    int newW = int(w * scale);
    int newH = int(h * scale);

    cv::Mat resized;
    cv::resize(img, resized, cv::Size(newW, newH));

    // ⭐ 使用浮点除法保持精度，与 postprocess 中的坐标还原保持一致
    padX = (inputW - newW) / 2.0f;
    padY = (inputH - newH) / 2.0f;

    cv::Mat padded(inputH, inputW, CV_8UC3, cv::Scalar(114,114,114));
    // cv::Rect 需要整数坐标，使用 static_cast<int> 确保与 postprocess 中的浮点还原一致
    resized.copyTo(padded(cv::Rect(static_cast<int>(padX), static_cast<int>(padY), newW, newH)));

    cv::Mat rgb;
    cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);

    cv::Mat floatImg;
    rgb.convertTo(floatImg, CV_32FC3, 1.0 / 255.0);

    std::vector<cv::Mat> channels(3);
    cv::split(floatImg, channels);
    for (int i = 0; i < 3; ++i) {
        memcpy(hostInput.data() + i * inputH * inputW, channels[i].data, inputH * inputW * sizeof(float));
    }

    cudaMemcpyAsync(buffers[0], hostInput.data(), inputSize, cudaMemcpyHostToDevice, stream);
}

void TrtYolo::infer()
{
    // --- TensorRT 8.x 使用 enqueueV2 ---
    // 直接传入 buffers 数组
    context->enqueueV2(buffers, stream, nullptr);

    cudaMemcpyAsync(hostOutput.data(), buffers[1], outputSize, cudaMemcpyDeviceToHost, stream);
    cudaStreamSynchronize(stream);
}

void TrtYolo::postprocess(std::vector<Detection>& results)
{
    results.clear();

    // 获取输出维度 (TensorRT 8.x API)
    auto outDims = engine->getBindingDimensions(outputIndex);
    if (outDims.nbDims < 3) return;

    // ⭐ 自动检测输出张量布局：
    // 布局 A: [1, numAttr, numPred] 例如 [1, 84, 8400]
    // 布局 B: [1, numPred, numAttr] 例如 [1, 8400, 84]
    const int d1 = outDims.d[1];
    const int d2 = outDims.d[2];

    int numAttr, numPred;
    // 如果 d1 较小（6~512 范围）且 d2 远大于 d1，则为 [1, attr, pred] 布局
    if (d1 >= 6 && d1 <= 512 && d2 > d1) {
        numAttr = d1;
        numPred = d2;
    } else {
        // 否则假设为 [1, pred, attr] 布局
        numPred = d1;
        numAttr = d2;
    }

    int numClasses = numAttr - 4;
    if (numClasses <= 0) return;

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    std::vector<int> classIds;

    for (int i = 0; i < numPred; i++)
    {
        float maxScore = 0.f;
        int classId = -1;

        for (int c = 0; c < numClasses; c++)
        {
            float score = hostOutput[(4 + c) * numPred + i];
            if (score > maxScore)
            {
                maxScore = score;
                classId = c;
            }
        }

        if (maxScore < confThreshold)
            continue;

        float cx = hostOutput[0 * numPred + i];
        float cy = hostOutput[1 * numPred + i];
        float w  = hostOutput[2 * numPred + i];
        float h  = hostOutput[3 * numPred + i];

        cx = (cx - padX) / scale;
        cy = (cy - padY) / scale;
        w  = w / scale;
        h  = h / scale;

        int x = int(cx - w / 2);
        int y = int(cy - h / 2);

        x = std::max(0, x);
        y = std::max(0, y);
        int iw = std::min((int)w, lastImage.cols - x);
        int ih = std::min((int)h, lastImage.rows - y);

        // ⭐ 跳过无效检测框
        if (iw <= 0 || ih <= 0) continue;

        boxes.emplace_back(x, y, iw, ih);
        scores.push_back(maxScore);
        classIds.push_back(classId);
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, scores, confThreshold, 0.45f, indices);

    for (int idx : indices)
    {
        Detection det;
        det.x = boxes[idx].x;
        det.y = boxes[idx].y;
        det.w = boxes[idx].width;
        det.h = boxes[idx].height;
        det.score = scores[idx];
        det.class_id = classIds[idx];
        results.push_back(det);
    }

    // ⭐ 日志：输出检测结果统计
    std::cout << "[TrtYolo] Postprocess: numAttr=" << numAttr
              << " numPred=" << numPred
              << " numClasses=" << numClasses
              << " rawBoxes=" << boxes.size()
              << " afterNMS=" << results.size()
              << " confThresh=" << confThreshold
              << std::endl;
    if (!results.empty()) {
        std::cout << "[TrtYolo] Top detection: class=" << results[0].class_id
                  << " score=" << results[0].score
                  << " box=[" << results[0].x << "," << results[0].y
                  << "," << results[0].w << "," << results[0].h << "]"
                  << std::endl;
    }
}
// ⭐ ONNX → TensorRT Engine 构建函数
bool TrtYolo::buildEngineFromOnnx(const std::string& onnxPath,
                                  const std::string& enginePath,
                                  ILogger& logger,
                                  int inputW,
                                  int inputH,
                                  size_t maxBatchSize,
                                  bool useFP16)
{
    std::cout << "[TrtYolo] Building TensorRT engine from ONNX: " << onnxPath << std::endl;
    std::cout << "[TrtYolo] Output engine: " << enginePath << std::endl;

    // 1. 创建 builder
    IBuilder* builder = createInferBuilder(logger);
    if (!builder) {
        std::cerr << "[TrtYolo] Failed to create TensorRT builder" << std::endl;
        return false;
    }

    // 2. 创建 network (explicit batch mode)
    const auto explicitBatch = 1U << static_cast<uint32_t>(NetworkDefinitionCreationFlag::kEXPLICIT_BATCH);
    INetworkDefinition* network = builder->createNetworkV2(explicitBatch);
    if (!network) {
        std::cerr << "[TrtYolo] Failed to create TensorRT network" << std::endl;
        delete builder;
        return false;
    }

    // 3. 创建 ONNX parser
    nvonnxparser::IParser* parser = nvonnxparser::createParser(*network, logger);
    if (!parser) {
        std::cerr << "[TrtYolo] Failed to create ONNX parser" << std::endl;
        delete network;
        delete builder;
        return false;
    }

    // 4. 解析 ONNX 模型
    if (!parser->parseFromFile(onnxPath.c_str(), static_cast<int>(ILogger::Severity::kWARNING))) {
        std::cerr << "[TrtYolo] Failed to parse ONNX model: " << onnxPath << std::endl;
        for (int i = 0; i < parser->getNbErrors(); ++i) {
            auto err = parser->getError(i);
            std::cerr << "  Parser error [" << i << "]: "
                      << err->desc() << " (file=" << err->file()
                      << ", line=" << err->line()
                      << ", func=" << err->func() << ")" << std::endl;
        }
        delete parser;
        delete network;
        delete builder;
        return false;
    }

    std::cout << "[TrtYolo] ONNX model parsed successfully." << std::endl;

    // 5. 配置 builder
    IBuilderConfig* config = builder->createBuilderConfig();
    if (!config) {
        std::cerr << "[TrtYolo] Failed to create builder config" << std::endl;
        delete parser;
        delete network;
        delete builder;
        return false;
    }

    // 设置工作空间大小 (TensorRT 8.x: setMemoryPoolLimit)
    // Jetson 等嵌入式设备显存有限，使用 256MB 而非 1GB
    config->setMemoryPoolLimit(MemoryPoolType::kWORKSPACE, 256ULL << 20); // 256MB
    std::cout << "[TrtYolo] Workspace set to 256MB." << std::endl;

    // 设置推理精度
    bool fp16Enabled = false;
    if (useFP16 && builder->platformHasFastFp16()) {
        config->setFlag(BuilderFlag::kFP16);
        fp16Enabled = true;
        std::cout << "[TrtYolo] FP16 mode enabled (platform supports fast FP16)." << std::endl;
    } else if (useFP16 && !builder->platformHasFastFp16()) {
        std::cout << "[TrtYolo] FP16 requested but not supported on this platform, falling back to FP32." << std::endl;
    } else {
        std::cout << "[TrtYolo] Using FP32 mode." << std::endl;
    }

    // 设置输入张量的维度 (batch=1, channel=3, H, W)
    auto inputTensorName = network->getInput(0)->getName();
    network->getInput(0)->setDimensions(Dims4(static_cast<int>(maxBatchSize), 3, inputH, inputW));
    std::cout << "[TrtYolo] Input tensor: " << inputTensorName
              << " shape: [" << maxBatchSize << ", 3, " << inputH << ", " << inputW << "]"
              << std::endl;

    // 6. 构建 engine (序列化)
    // 设置最大尝试次数：先尝试用户指定的精度，失败则降级到 FP32
    const int maxAttempts = fp16Enabled ? 2 : 1;
    IHostMemory* serializedModel = nullptr;

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        if (attempt > 0) {
            // 第一次尝试失败，降级到 FP32
            std::cout << "[TrtYolo] FP16 build failed, retrying with FP32..." << std::endl;
            delete config;
            config = builder->createBuilderConfig();
            if (!config) {
                std::cerr << "[TrtYolo] Failed to create builder config on retry" << std::endl;
                break;
            }
            config->setMemoryPoolLimit(MemoryPoolType::kWORKSPACE, 256ULL << 20);
            // 不设置 FP16 flag
            std::cout << "[TrtYolo] Retrying with FP32 mode." << std::endl;
        }

        serializedModel = builder->buildSerializedNetwork(*network, *config);
        if (serializedModel) break; // 构建成功

        std::cerr << "[TrtYolo] Attempt " << (attempt + 1) << "/" << maxAttempts
                  << ": Failed to build serialized TensorRT engine" << std::endl;
    }

    if (!serializedModel) {
        std::cerr << "[TrtYolo] All attempts to build TensorRT engine failed." << std::endl;
        std::cerr << "[TrtYolo] Possible causes: insufficient GPU memory, unsupported ops." << std::endl;
        delete config;
        delete parser;
        delete network;
        delete builder;
        return false;
    }

    std::cout << "[TrtYolo] Engine built successfully. Size: "
              << serializedModel->size() << " bytes" << std::endl;

    // 7. 写入文件
    std::ofstream engineFile(enginePath, std::ios::binary);
    if (!engineFile.is_open()) {
        std::cerr << "[TrtYolo] Failed to write engine file: " << enginePath << std::endl;
        delete serializedModel;
        delete config;
        delete parser;
        delete network;
        delete builder;
        return false;
    }
    engineFile.write(static_cast<const char*>(serializedModel->data()), serializedModel->size());
    engineFile.close();

    std::cout << "[TrtYolo] Engine saved to: " << enginePath << std::endl;

    // 8. 清理 (TensorRT 8.x 使用 delete 而非 destroy)
    delete serializedModel;
    delete config;
    delete parser;
    delete network;
    delete builder;

    return true;
}

bool TrtYolo::loadClasses(const std::string& classesPath)
{
    std::ifstream file(classesPath);
    if (!file.is_open()) {
        std::cerr << "[TrtYolo] Failed to open classes file: " << classesPath << std::endl;
        return false;
    }

    classNames.clear();
    std::string line;
    while (std::getline(file, line)) {
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string className = line.substr(colonPos + 1);
            className.erase(className.begin(), std::find_if(className.begin(), className.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
            className.erase(std::find_if(className.rbegin(), className.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(), className.end());
            if (!className.empty()) {
                classNames.push_back(className);
            }
        }
    }
    file.close();

    std::cout << "[TrtYolo] Loaded " << classNames.size() << " classes from: " << classesPath << std::endl;
    return true;
}

void TrtYolo::draw(cv::Mat& img, const std::vector<Detection>& results)
{
    for (const auto& d : results)
    {
        cv::Rect box(d.x, d.y, d.w, d.h);
        cv::rectangle(img, box, cv::Scalar(0,255,0), 2);

        std::string label = (d.class_id < classNames.size()) ? classNames[d.class_id] : "unknown";
        label += cv::format(": %.2f", d.score);

        int baseline = 0;
        cv::Size textSize = cv::getTextSize(label,
                                            cv::FONT_HERSHEY_SIMPLEX,
                                            0.6, 2, &baseline);

        cv::Rect bg(box.x, box.y - textSize.height - 8,
                    textSize.width + 6, textSize.height + 6);

        if (bg.y < 0) bg.y = 0;

        cv::rectangle(img, bg, cv::Scalar(0,255,0), -1);

        cv::putText(img, label,
                    cv::Point(bg.x + 3, bg.y + textSize.height),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.6, cv::Scalar(0,0,0), 2);
    }
}
