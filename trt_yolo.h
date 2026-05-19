#ifndef TRT_YOLO_H
#define TRT_YOLO_H

#include <opencv2/opencv.hpp>
#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <vector>
#include <string>

struct Detection
{
    int class_id;
    float score;
    int x;
    int y;
    int w;
    int h;
};
class TrtYolo
{
public:
    TrtYolo(const std::string& enginePath, nvinfer1::ILogger& logger, const std::string& classesPath = "");
    ~TrtYolo();
    bool loadClasses(const std::string& classesPath);

    void preprocess(const cv::Mat& img);
    void infer();
    void postprocess(std::vector<Detection>& results);
    void draw(cv::Mat& img, const std::vector<Detection>& results);
    float confThreshold = 0.25f;
    std::vector<std::string> classNames;
private:
    bool loadEngine(const std::string& enginePath);
    size_t getSizeByDim(const nvinfer1::Dims& dims);

private:
    nvinfer1::ILogger& logger;
    nvinfer1::IRuntime* runtime{nullptr};
    nvinfer1::ICudaEngine* engine{nullptr};
    nvinfer1::IExecutionContext* context{nullptr};

    // void* buffers[2]{nullptr, nullptr};
    cudaStream_t stream{};

    int inputIndex{0};
    int outputIndex{1};

    int inputW = 640;
    int inputH = 640;
    int numClasses = 80;

    size_t inputSize{0};
    size_t outputSize{0};

    std::vector<float> hostInput;
    std::vector<float> hostOutput;

    cv::Mat lastImage;

    float padX = 0.f;
    float padY = 0.f;
    float scale = 1.f;
    // 在 class TrtYolo 的 private 或 protected 区域添加：
private:
    std::string inputTensorName;
    std::string outputTensorName;

    // 建议将 buffers 定义也稍微修改下，避免歧义
    void* buffers[2];
};

#endif // TRT_YOLO_H
