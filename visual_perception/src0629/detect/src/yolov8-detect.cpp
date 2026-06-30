#include "yolov8-detect.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace det;

namespace
{
bool has_dynamic_dim(const nvinfer1::Dims& dims)
{
    for (int i = 0; i < dims.nbDims; ++i) {
        if (dims.d[i] < 0) {
            return true;
        }
    }
    return false;
}

bool has_invalid_dim(const nvinfer1::Dims& dims)
{
    if (dims.nbDims <= 0) {
        return true;
    }

    for (int i = 0; i < dims.nbDims; ++i) {
        if (dims.d[i] <= 0) {
            return true;
        }
    }
    return false;
}
}  // namespace

YOLOv8::YOLOv8(const std::string& engine_file_path)
{
    std::ifstream file(engine_file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("无法打开 TensorRT engine：" + engine_file_path);
    }

    const std::streamsize engine_size = file.tellg();
    if (engine_size <= 0) {
        throw std::runtime_error("TensorRT engine 文件为空：" + engine_file_path);
    }

    file.seekg(0, std::ios::beg);
    std::vector<char> engine_data(static_cast<std::size_t>(engine_size));

    if (!file.read(engine_data.data(), engine_size)) {
        throw std::runtime_error("读取 TensorRT engine 失败：" + engine_file_path);
    }

    initLibNvInferPlugins(&this->gLogger, "");

    this->runtime = nvinfer1::createInferRuntime(this->gLogger);
    if (this->runtime == nullptr) {
        throw std::runtime_error("createInferRuntime() 失败");
    }

    this->engine = this->runtime->deserializeCudaEngine(
        engine_data.data(),
        engine_data.size()
    );

    if (this->engine == nullptr) {
        delete this->runtime;
        this->runtime = nullptr;
        throw std::runtime_error(
            "deserializeCudaEngine() 失败。请确认 engine 是在当前 Orin 和当前 TensorRT 版本上生成的"
        );
    }

    this->context = this->engine->createExecutionContext();
    if (this->context == nullptr) {
        delete this->engine;
        delete this->runtime;
        this->engine = nullptr;
        this->runtime = nullptr;
        throw std::runtime_error("createExecutionContext() 失败");
    }

    CHECK(cudaStreamCreate(&this->stream));

    // TensorRT 10：使用基于 tensor name 的 I/O API。
    this->num_bindings = this->engine->getNbIOTensors();
    if (this->num_bindings <= 0) {
        throw std::runtime_error("TensorRT engine 中没有 I/O tensor");
    }

    // 第一遍：收集输入，先设置所有动态输入形状。
    for (int i = 0; i < this->num_bindings; ++i) {
        const char* tensor_name = this->engine->getIOTensorName(i);
        if (tensor_name == nullptr) {
            throw std::runtime_error("getIOTensorName() 返回空指针");
        }

        if (this->engine->getTensorIOMode(tensor_name) !=
            nvinfer1::TensorIOMode::kINPUT) {
            continue;
        }

        Binding binding{};
        binding.name = tensor_name;

        const nvinfer1::DataType dtype =
            this->engine->getTensorDataType(tensor_name);
        binding.dsize = type_to_size(dtype);

        nvinfer1::Dims dims = this->engine->getTensorShape(tensor_name);

        // 动态输入使用 profile 0 的最大尺寸分配缓冲区。
        if (has_dynamic_dim(dims)) {
            dims = this->engine->getProfileShape(
                tensor_name,
                0,
                nvinfer1::OptProfileSelector::kMAX
            );
        }

        if (has_invalid_dim(dims)) {
            throw std::runtime_error(
                "输入 tensor 尺寸无效：" + binding.name
            );
        }

        if (!this->context->setInputShape(tensor_name, dims)) {
            throw std::runtime_error(
                "setInputShape() 失败：" + binding.name
            );
        }

        binding.dims = dims;
        binding.size = get_size_by_dims(dims);
        this->input_bindings.push_back(binding);
        ++this->num_inputs;
    }

    if (this->num_inputs <= 0) {
        throw std::runtime_error("TensorRT engine 中没有输入 tensor");
    }

    // 第二遍：输入形状已设置，此时读取输出的实际形状。
    for (int i = 0; i < this->num_bindings; ++i) {
        const char* tensor_name = this->engine->getIOTensorName(i);

        if (this->engine->getTensorIOMode(tensor_name) !=
            nvinfer1::TensorIOMode::kOUTPUT) {
            continue;
        }

        Binding binding{};
        binding.name = tensor_name;

        const nvinfer1::DataType dtype =
            this->engine->getTensorDataType(tensor_name);
        binding.dsize = type_to_size(dtype);

        nvinfer1::Dims dims = this->context->getTensorShape(tensor_name);
        if (has_invalid_dim(dims)) {
            throw std::runtime_error(
                "输出 tensor 尺寸未解析或无效：" + binding.name
            );
        }

        binding.dims = dims;
        binding.size = get_size_by_dims(dims);
        this->output_bindings.push_back(binding);
        ++this->num_outputs;
    }

    if (this->num_outputs <= 0) {
        throw std::runtime_error("TensorRT engine 中没有输出 tensor");
    }
}

YOLOv8::~YOLOv8()
{
    if (this->stream != nullptr) {
        cudaStreamSynchronize(this->stream);
    }

    for (void* ptr : this->device_ptrs) {
        if (ptr != nullptr) {
            cudaFree(ptr);
        }
    }
    this->device_ptrs.clear();

    for (void* ptr : this->host_ptrs) {
        if (ptr != nullptr) {
            cudaFreeHost(ptr);
        }
    }
    this->host_ptrs.clear();

    if (this->stream != nullptr) {
        cudaStreamDestroy(this->stream);
        this->stream = nullptr;
    }

    // TensorRT 10 删除 destroy()，使用 delete 释放接口对象。
    delete this->context;
    delete this->engine;
    delete this->runtime;

    this->context = nullptr;
    this->engine = nullptr;
    this->runtime = nullptr;
}

void YOLOv8::make_pipe(bool warmup)
{
    this->device_ptrs.clear();
    this->host_ptrs.clear();

    for (const auto& binding : this->input_bindings) {
        void* device_ptr = nullptr;
        const std::size_t bytes = binding.size * binding.dsize;

        CHECK(cudaMallocAsync(&device_ptr, bytes, this->stream));
        this->device_ptrs.push_back(device_ptr);

        if (!this->context->setInputShape(
                binding.name.c_str(), binding.dims)) {
            throw std::runtime_error(
                "setInputShape() 失败：" + binding.name
            );
        }

        if (!this->context->setTensorAddress(
                binding.name.c_str(), device_ptr)) {
            throw std::runtime_error(
                "设置输入 tensor 地址失败：" + binding.name
            );
        }
    }

    for (const auto& binding : this->output_bindings) {
        void* device_ptr = nullptr;
        void* host_ptr = nullptr;
        const std::size_t bytes = binding.size * binding.dsize;

        CHECK(cudaMallocAsync(&device_ptr, bytes, this->stream));
        CHECK(cudaHostAlloc(&host_ptr, bytes, 0));

        this->device_ptrs.push_back(device_ptr);
        this->host_ptrs.push_back(host_ptr);

        if (!this->context->setTensorAddress(
                binding.name.c_str(), device_ptr)) {
            throw std::runtime_error(
                "设置输出 tensor 地址失败：" + binding.name
            );
        }
    }

    CHECK(cudaStreamSynchronize(this->stream));

    if (warmup) {
        for (int round = 0; round < 10; ++round) {
            for (int input_index = 0;
                 input_index < this->num_inputs;
                 ++input_index) {
                const auto& binding = this->input_bindings[input_index];
                const std::size_t bytes = binding.size * binding.dsize;
                CHECK(cudaMemsetAsync(
                    this->device_ptrs[input_index],
                    0,
                    bytes,
                    this->stream
                ));
            }
            this->infer();
        }
        std::printf("model warmup 10 times\n");
    }
}

void YOLOv8::letterbox(
    const cv::Mat& image,
    cv::Mat& out,
    cv::Size& size)
{
    const float inp_h = static_cast<float>(size.height);
    const float inp_w = static_cast<float>(size.width);
    const float height = static_cast<float>(image.rows);
    const float width = static_cast<float>(image.cols);

    const float r = std::min(inp_h / height, inp_w / width);
    const int padw = static_cast<int>(std::round(width * r));
    const int padh = static_cast<int>(std::round(height * r));

    cv::Mat tmp;
    if (static_cast<int>(width) != padw ||
        static_cast<int>(height) != padh) {
        cv::resize(image, tmp, cv::Size(padw, padh));
    } else {
        tmp = image.clone();
    }

    float dw = inp_w - static_cast<float>(padw);
    float dh = inp_h - static_cast<float>(padh);
    dw /= 2.0f;
    dh /= 2.0f;

    const int top = static_cast<int>(std::round(dh - 0.1f));
    const int bottom = static_cast<int>(std::round(dh + 0.1f));
    const int left = static_cast<int>(std::round(dw - 0.1f));
    const int right = static_cast<int>(std::round(dw + 0.1f));

    cv::copyMakeBorder(
        tmp,
        tmp,
        top,
        bottom,
        left,
        right,
        cv::BORDER_CONSTANT,
        cv::Scalar(114, 114, 114)
    );

    out.create({1, 3, size.height, size.width}, CV_32F);

    std::vector<cv::Mat> channels;
    cv::split(tmp, channels);

    float* out_data = out.ptr<float>();

    cv::Mat c0(
        size.height,
        size.width,
        CV_32F,
        out_data
    );

    cv::Mat c1(
        size.height,
        size.width,
        CV_32F,
        out_data + size.height * size.width
    );

    cv::Mat c2(
        size.height,
        size.width,
        CV_32F,
        out_data + size.height * size.width * 2
    );

    channels[0].convertTo(c2, CV_32F, 1.0 / 255.0);
    channels[1].convertTo(c1, CV_32F, 1.0 / 255.0);
    channels[2].convertTo(c0, CV_32F, 1.0 / 255.0);

    this->pparam.ratio = 1.0f / r;
    this->pparam.dw = dw;
    this->pparam.dh = dh;
    this->pparam.height = height;
    this->pparam.width = width;
}

void YOLOv8::copy_from_Mat(cv::Mat& image, cv::Size& size)
{
    if (this->input_bindings.empty() || this->device_ptrs.empty()) {
        throw std::runtime_error("YOLOv8 缓冲区尚未初始化，请先调用 make_pipe()");
    }

    cv::Mat nchw;
    this->letterbox(image, nchw, size);

    const auto& input = this->input_bindings.front();
    const nvinfer1::Dims input_dims{
        4,
        {1, 3, size.height, size.width}
    };

    if (!this->context->setInputShape(
            input.name.c_str(), input_dims)) {
        throw std::runtime_error(
            "setInputShape() 失败，输入尺寸=" +
            std::to_string(size.width) + "x" +
            std::to_string(size.height)
        );
    }

    if (!this->context->setTensorAddress(
            input.name.c_str(), this->device_ptrs[0])) {
        throw std::runtime_error("设置输入 tensor 地址失败：" + input.name);
    }

    const std::size_t copy_bytes = nchw.total() * nchw.elemSize();
    const std::size_t allocated_bytes = input.size * input.dsize;

    if (copy_bytes > allocated_bytes) {
        throw std::runtime_error(
            "输入图像所需显存超过已分配缓冲区。请按更大的 maxShapes 重新生成 engine"
        );
    }

    CHECK(cudaMemcpyAsync(
        this->device_ptrs[0],
        nchw.ptr<float>(),
        copy_bytes,
        cudaMemcpyHostToDevice,
        this->stream
    ));
}

void YOLOv8::infer()
{
    if (!this->context->enqueueV3(this->stream)) {
        throw std::runtime_error("TensorRT enqueueV3() 推理失败");
    }

    for (int i = 0; i < this->num_outputs; ++i) {
        const std::size_t bytes =
            this->output_bindings[i].size *
            this->output_bindings[i].dsize;

        CHECK(cudaMemcpyAsync(
            this->host_ptrs[i],
            this->device_ptrs[i + this->num_inputs],
            bytes,
            cudaMemcpyDeviceToHost,
            this->stream
        ));
    }

    CHECK(cudaStreamSynchronize(this->stream));
}

void YOLOv8::postprocess(
    std::vector<Object>& objs,
    float score_thres,
    float iou_thres,
    int topk,
    int num_labels)
{
    objs.clear();

    if (this->output_bindings.empty() || this->host_ptrs.empty()) {
        return;
    }

    const auto& output_dims = this->output_bindings[0].dims;
    if (output_dims.nbDims < 3) {
        throw std::runtime_error("YOLOv8 输出维度不是预期的 [N,C,A]");
    }

    const int num_channels = static_cast<int>(output_dims.d[1]);
    const int num_anchors = static_cast<int>(output_dims.d[2]);

    if (num_labels <= 0 || 4 + num_labels > num_channels) {
        throw std::runtime_error(
            "num_labels 与模型输出通道不匹配：num_channels=" +
            std::to_string(num_channels) +
            ", num_labels=" + std::to_string(num_labels)
        );
    }

    auto& dw = this->pparam.dw;
    auto& dh = this->pparam.dh;
    auto& width = this->pparam.width;
    auto& height = this->pparam.height;
    auto& ratio = this->pparam.ratio;

    std::vector<cv::Rect> bboxes;
    std::vector<float> scores;
    std::vector<int> labels;
    std::vector<int> indices;

    cv::Mat output(
        num_channels,
        num_anchors,
        CV_32F,
        static_cast<float*>(this->host_ptrs[0])
    );
    output = output.t();

    for (int i = 0; i < num_anchors; ++i) {
        float* row_ptr = output.row(i).ptr<float>();
        float* bboxes_ptr = row_ptr;
        float* scores_ptr = row_ptr + 4;

        float* max_score_ptr = std::max_element(
            scores_ptr,
            scores_ptr + num_labels
        );

        const float score = *max_score_ptr;
        if (score <= score_thres) {
            continue;
        }

        const float x = *bboxes_ptr++ - dw;
        const float y = *bboxes_ptr++ - dh;
        const float w = *bboxes_ptr++;
        const float h = *bboxes_ptr;

        const float x0 = clamp((x - 0.5f * w) * ratio, 0.0f, width);
        const float y0 = clamp((y - 0.5f * h) * ratio, 0.0f, height);
        const float x1 = clamp((x + 0.5f * w) * ratio, 0.0f, width);
        const float y1 = clamp((y + 0.5f * h) * ratio, 0.0f, height);

        const int label = static_cast<int>(max_score_ptr - scores_ptr);

        cv::Rect_<float> bbox;
        bbox.x = x0;
        bbox.y = y0;
        bbox.width = x1 - x0;
        bbox.height = y1 - y0;

        bboxes.push_back(bbox);
        labels.push_back(label);
        scores.push_back(score);
    }

#ifdef BATCHED_NMS
    cv::dnn::NMSBoxesBatched(
        bboxes,
        scores,
        labels,
        score_thres,
        iou_thres,
        indices
    );
#else
    cv::dnn::NMSBoxes(
        bboxes,
        scores,
        score_thres,
        iou_thres,
        indices
    );
#endif

    int count = 0;
    for (const int index : indices) {
        if (count >= topk) {
            break;
        }

        Object object;
        object.rect = bboxes[index];
        object.prob = scores[index];
        object.label = labels[index];
        objs.push_back(object);
        ++count;
    }
}

void YOLOv8::draw_objects(
    const cv::Mat& image,
    cv::Mat& res,
    const std::vector<Object>& objs,
    const std::vector<std::string>& class_names,
    const std::vector<std::vector<unsigned int>>& colors,
    std::vector<std::vector<float>>& result_array)
{
    res = image.clone();
    result_array.clear();

    for (const auto& obj : objs) {
        if (obj.label < 0 ||
            obj.label >= static_cast<int>(class_names.size()) ||
            obj.label >= static_cast<int>(colors.size())) {
            continue;
        }

        if(obj.rect.x>=image.cols/2)
        {
            result_array.push_back({
            static_cast<float>(obj.rect.x+10),
            static_cast<float>(obj.rect.y-10),
            static_cast<float>(obj.rect.width),
            static_cast<float>(obj.rect.height),
            static_cast<float>(obj.label),
            obj.prob});
        }
        else{
            result_array.push_back({
            static_cast<float>(obj.rect.x-30),
            static_cast<float>(obj.rect.y),
            static_cast<float>(obj.rect.width),
            static_cast<float>(obj.rect.height),
            static_cast<float>(obj.label),
            obj.prob});
        }

        const cv::Scalar color(
            colors[obj.label][0],
            colors[obj.label][1],
            colors[obj.label][2]
        );

        cv::rectangle(res, obj.rect, color, 2);

        char text[256];
        std::snprintf(
            text,
            sizeof(text),
            "%s %.1f%%",
            class_names[obj.label].c_str(),
            obj.prob * 100.0f
        );

        int baseline = 0;
        const cv::Size label_size = cv::getTextSize(
            text,
            cv::FONT_HERSHEY_SIMPLEX,
            0.4,
            1,
            &baseline
        );

        int x = static_cast<int>(obj.rect.x);
        int y = static_cast<int>(obj.rect.y) + 1;

        x = std::max(0, std::min(x, res.cols - 1));
        y = std::max(0, std::min(y, res.rows - 1));

        const int background_width =
            std::min(label_size.width, res.cols - x);
        const int background_height =
            std::min(label_size.height + baseline, res.rows - y);

        if (background_width > 0 && background_height > 0) {
            cv::rectangle(
                res,
                cv::Rect(x, y, background_width, background_height),
                cv::Scalar(0, 0, 255),
                -1
            );
        }

        cv::putText(
            res,
            text,
            cv::Point(
                x,
                std::min(y + label_size.height, res.rows - 1)
            ),
            cv::FONT_HERSHEY_SIMPLEX,
            0.4,
            cv::Scalar(255, 255, 255),
            1
        );
    }
}


// void YOLOv8::draw_objects(
//     const cv::Mat& image,
//     cv::Mat& res,
//     const std::vector<Object>& objs,
//     const std::vector<std::string>& class_names,
//     const std::vector<std::vector<unsigned int>>& colors,
//     std::vector<std::vector<float>>& result_array)
// {
//     res = image.clone();
//     result_array.clear();

//     for (const auto& obj : objs) {
//         if (obj.label < 0 ||
//             obj.label >= static_cast<int>(class_names.size()) ||
//             obj.label >= static_cast<int>(colors.size())) {
//             continue;
//         }

//         result_array.push_back({
//             static_cast<float>(obj.rect.x),
//             static_cast<float>(obj.rect.y),
//             static_cast<float>(obj.rect.width),
//             static_cast<float>(obj.rect.height),
//             static_cast<float>(obj.label),
//             obj.prob
//         });

//         const cv::Scalar color(
//             colors[obj.label][0],
//             colors[obj.label][1],
//             colors[obj.label][2]
//         );

//         cv::rectangle(res, obj.rect, color, 2);

//         char text[256];
//         std::snprintf(
//             text,
//             sizeof(text),
//             "%s %.1f%%",
//             class_names[obj.label].c_str(),
//             obj.prob * 100.0f
//         );

//         int baseline = 0;
//         const cv::Size label_size = cv::getTextSize(
//             text,
//             cv::FONT_HERSHEY_SIMPLEX,
//             0.4,
//             1,
//             &baseline
//         );

//         int x = static_cast<int>(obj.rect.x);
//         int y = static_cast<int>(obj.rect.y) + 1;

//         x = std::max(0, std::min(x, res.cols - 1));
//         y = std::max(0, std::min(y, res.rows - 1));

//         const int background_width =
//             std::min(label_size.width, res.cols - x);
//         const int background_height =
//             std::min(label_size.height + baseline, res.rows - y);

//         if (background_width > 0 && background_height > 0) {
//             cv::rectangle(
//                 res,
//                 cv::Rect(x, y, background_width, background_height),
//                 cv::Scalar(0, 0, 255),
//                 -1
//             );
//         }

//         cv::putText(
//             res,
//             text,
//             cv::Point(
//                 x,
//                 std::min(y + label_size.height, res.rows - 1)
//             ),
//             cv::FONT_HERSHEY_SIMPLEX,
//             0.4,
//             cv::Scalar(255, 255, 255),
//             1
//         );
//     }

//     // =====================================================
//     // 录制检测结果视频：10Hz
//     // 注意：这里一定要放在 for 循环外面
//     // 因为 res 是整张已经画好框的结果图
//     // =====================================================
//     static cv::VideoWriter result_video_writer;
//     static bool video_writer_initialized = false;

//     static auto last_write_time =
//         std::chrono::steady_clock::now() - std::chrono::milliseconds(100);

//     const double record_fps = 10.0;
//     const int record_interval_ms = 100;  // 10Hz = 100ms 一帧

//     auto now = std::chrono::steady_clock::now();
//     auto elapsed_ms =
//         std::chrono::duration_cast<std::chrono::milliseconds>(
//             now - last_write_time
//         ).count();

//     if (elapsed_ms >= record_interval_ms) {

//         cv::Mat frame_to_write;

//         // VideoWriter 一般需要 CV_8UC3 的 BGR 图像
//         if (res.type() == CV_8UC3) {
//             frame_to_write = res;
//         } else if (res.type() == CV_8UC4) {
//             cv::cvtColor(res, frame_to_write, cv::COLOR_BGRA2BGR);
//         } else if (res.type() == CV_8UC1) {
//             cv::cvtColor(res, frame_to_write, cv::COLOR_GRAY2BGR);
//         } else {
//             res.convertTo(frame_to_write, CV_8UC3);
//         }

//         if (!video_writer_initialized) {
//             std::string save_path =
//                 "/home/admin/vsproject/control/video/yolo_result.mp4";

//             int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');

//             result_video_writer.open(
//                 save_path,
//                 fourcc,
//                 record_fps,
//                 frame_to_write.size(),
//                 true
//             );

//             if (!result_video_writer.isOpened()) {
//                 std::cerr << "[ERROR] 打开视频保存失败: "
//                           << save_path << std::endl;
//                 return;
//             }

//             video_writer_initialized = true;

//             std::cout << "[INFO] 开始录制目标检测结果视频: "
//                       << save_path << std::endl;
//             std::cout << "[INFO] 视频尺寸: "
//                       << frame_to_write.cols << "x"
//                       << frame_to_write.rows
//                       << ", FPS: " << record_fps << std::endl;
//         }

//         result_video_writer.write(frame_to_write);
//         last_write_time = now;
//     }
// }