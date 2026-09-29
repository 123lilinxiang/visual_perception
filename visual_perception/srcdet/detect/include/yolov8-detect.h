#pragma once

#include "NvInferPlugin.h"
#include "common.hpp"

#include <fstream>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>
#include "msg_det/msg/detect_res.hpp"

using namespace det;

class YOLOv8
{
public:
    explicit YOLOv8(const std::string& engine_file_path);
    ~YOLOv8();

    YOLOv8(const YOLOv8&) = delete;
    YOLOv8& operator=(const YOLOv8&) = delete;

    void make_pipe(bool warmup = true);
    void copy_from_Mat(cv::Mat& image, cv::Size& size);
    void letterbox(const cv::Mat& image, cv::Mat& out, cv::Size& size);
    void infer();

    void postprocess(
        std::vector<Object>& objs,
        float score_thres = 0.65f,
        float iou_thres = 0.65f,
        int topk = 1,
        int num_labels = 2
    );

    static void draw_objects(
        const cv::Mat& image,
        cv::Mat& res,
        const std::vector<Object>& objs,
        const std::vector<std::string>& class_names,
        const std::vector<std::vector<unsigned int>>& colors,
        std::vector<std::vector<float>>& result_array
    );

    int num_bindings = 0;
    int num_inputs = 0;
    int num_outputs = 0;

    std::vector<Binding> input_bindings;
    std::vector<Binding> output_bindings;
    std::vector<void*> host_ptrs;
    std::vector<void*> device_ptrs;

    PreParam pparam;

private:
    nvinfer1::ICudaEngine* engine = nullptr;
    nvinfer1::IRuntime* runtime = nullptr;
    nvinfer1::IExecutionContext* context = nullptr;
    cudaStream_t stream = nullptr;
    Logger gLogger{nvinfer1::ILogger::Severity::kERROR};
};
