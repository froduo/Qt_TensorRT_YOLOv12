#include "trt_yolo.h"
#include <fstream>
#include <iostream>
#include <opencv2/dnn.hpp>

using namespace nvinfer1;

static inline float sigmoid(float x)
{
    return 1.f / (1.f + expf(-x));
}

TrtYolo::TrtYolo(const std::string& enginePath, ILogger& logger)
    : logger(logger)
{
    loadEngine(enginePath);
}

TrtYolo::~TrtYolo()
{
    // TensorRT 10.x 显式要求使用 delete 释放内存
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
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> engineData(size);
    file.read(engineData.data(), size);
    file.close();

    runtime = createInferRuntime(logger);
    engine = runtime->deserializeCudaEngine(engineData.data(), size);
    context = engine->createExecutionContext();

    // --- TensorRT 10.x 新版 I/O 处理逻辑 ---
    int nbIOTensors = engine->getNbIOTensors(); // 代替 getNbBindings
    for (int i = 0; i < nbIOTensors; i++)
    {
        const char* tensorName = engine->getIOTensorName(i); // 代替 getBindingName
        TensorIOMode mode = engine->getTensorIOMode(tensorName); // 代替 bindingIsInput

        if (mode == TensorIOMode::kINPUT) {
            inputIndex = i;
            inputTensorName = tensorName; // 记录名称，V3 API 需要
            std::cout << "Input Tensor [" << i << "]: " << tensorName << std::endl;
        } else {
            outputIndex = i;
            outputTensorName = tensorName; // 记录名称
            std::cout << "Output Tensor [" << i << "]: " << tensorName << std::endl;
        }
    }

    // 设置输入维度 (代替 setBindingDimensions)
    context->setInputShape(inputTensorName.c_str(), Dims4(1, 3, inputH, inputW));

    // 计算输入输出大小
    inputSize = 1 * 3 * inputH * inputW * sizeof(float);

    // 获取输出维度 (代替 getBindingDimensions)
    auto outDims = engine->getTensorShape(outputTensorName.c_str());
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
    // --- TensorRT 10.x 使用 enqueueV3 ---
    // 1. 必须先绑定张量地址
    context->setTensorAddress(inputTensorName.c_str(), buffers[0]);
    context->setTensorAddress(outputTensorName.c_str(), buffers[1]);

    // 2. 执行异步推理
    context->enqueueV3(stream);

    cudaMemcpyAsync(hostOutput.data(), buffers[1], outputSize, cudaMemcpyDeviceToHost, stream);
    cudaStreamSynchronize(stream);
}

void TrtYolo::postprocess(std::vector<Detection>& results)
{
    results.clear();

    // 获取输出维度 (TensorRT 10 API)
    auto outDims = engine->getTensorShape(outputTensorName.c_str());
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
// draw 函数保持不变...
void TrtYolo::draw(cv::Mat& img, const std::vector<Detection>& results)
{
    for (const auto& d : results)
    {
        cv::Rect box(d.x, d.y, d.w, d.h);
        cv::rectangle(img, box, cv::Scalar(0,255,0), 2);

        std::string label = classNames[d.class_id] + cv::format(": %.2f", d.score);

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

