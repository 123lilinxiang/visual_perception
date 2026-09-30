#pragma once

#include <NvInfer.h>
#include <NvInferPlugin.h>
#include <NvInferVersion.h>
#include <cuda_runtime_api.h>

#include <climits>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if NV_TENSORRT_MAJOR < 8
#error "This package requires TensorRT 8 or newer. Use the SDK supplied for your JetPack."
#endif

namespace yolo_trt {
inline void check_cuda(cudaError_t result)
{
    if (result != cudaSuccess) {
        throw std::runtime_error(std::string("CUDA: ") + cudaGetErrorString(result));
    }
}

class Logger final : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* message) noexcept override
    {
        if (severity <= Severity::kWARNING) {
            std::cerr << "[TensorRT] " << message << '\n';
        }
    }
};

inline std::size_t volume(const nvinfer1::Dims& dims)
{
    if (dims.nbDims <= 0) throw std::runtime_error("Invalid TensorRT tensor rank");
    std::size_t result = 1;
    for (int i = 0; i < dims.nbDims; ++i) {
        if (dims.d[i] <= 0 || dims.d[i] > INT_MAX) {
            throw std::runtime_error("Unresolved or unsupported TensorRT dimension");
        }
        const auto value = static_cast<std::size_t>(dims.d[i]);
        if (result > std::numeric_limits<std::size_t>::max() / value / sizeof(float)) {
            throw std::runtime_error("TensorRT tensor size overflow");
        }
        result *= value;
    }
    return result;
}

inline bool same_shape(const nvinfer1::Dims& a, const nvinfer1::Dims& b)
{
    if (a.nbDims != b.nbDims) return false;
    for (int i = 0; i < a.nbDims; ++i) if (a.d[i] != b.d[i]) return false;
    return true;
}

// Shared runtime for the existing raw YOLOv8 Detection / OBB postprocessors.
// Supports one FP32 NCHW image (batch=1), one FP32 [1,C,N] output, profile 0.
// Internal network layers may use FP16. End-to-end/NMS engines are not raw YOLO.
// Bindings remain inputs-first for the existing postprocessors; TRT8's enqueue
// array is separately mapped by the real engine index, including profile slots.
template<class Binding>
class Runtime {
public:
    int num_bindings = 0;
    int num_inputs = 0;
    int num_outputs = 0;
    std::vector<Binding> input_bindings;
    std::vector<Binding> output_bindings;
    std::vector<void*> device_ptrs;
    std::vector<void*> host_ptrs;

    explicit Runtime(const std::string& engine_file_path)
    {
        try {
            std::ifstream file(engine_file_path, std::ios::binary | std::ios::ate);
            if (!file) throw std::runtime_error("Cannot open engine: " + engine_file_path);
            const auto length = file.tellg();
            if (length <= 0) throw std::runtime_error("Empty engine: " + engine_file_path);
            std::vector<char> data(static_cast<std::size_t>(length));
            file.seekg(0);
            if (!file.read(data.data(), static_cast<std::streamsize>(data.size()))) {
                throw std::runtime_error("Cannot read engine: " + engine_file_path);
            }
            if (!initLibNvInferPlugins(&logger_, "")) {
                throw std::runtime_error("initLibNvInferPlugins failed");
            }
            runtime_ = nvinfer1::createInferRuntime(logger_);
            if (!runtime_) throw std::runtime_error("createInferRuntime failed");
            engine_ = runtime_->deserializeCudaEngine(data.data(), data.size());
            if (!engine_) {
                throw std::runtime_error("Cannot deserialize engine. Rebuild the ONNX on this "
                    "AGX Orin with its installed TensorRT; do not reuse an x86 engine: " + engine_file_path);
            }
            context_ = engine_->createExecutionContext();
            if (!context_) throw std::runtime_error("createExecutionContext failed");
            check_cuda(cudaStreamCreate(&stream_));

#if NV_TENSORRT_MAJOR >= 10
            num_bindings = engine_->getNbIOTensors();
            const int count = num_bindings;
#else
            if (engine_->hasImplicitBatchDimension()) {
                throw std::runtime_error("Export an explicit-batch ONNX engine");
            }
            num_bindings = engine_->getNbBindings();
            const int profiles = engine_->getNbOptimizationProfiles();
            const int count = num_bindings / (profiles > 0 ? profiles : 1);
            enqueue_bindings_.resize(num_bindings, nullptr);
#endif
            // First discover all names and roles. Outputs are resolved only AFTER
            // the input shape has been set, regardless of engine I/O ordering.
            for (int i = 0; i < count; ++i) {
                Binding binding;
#if NV_TENSORRT_MAJOR >= 10
                const char* name = engine_->getIOTensorName(i);
                if (!name) throw std::runtime_error("Null TensorRT tensor name");
                binding.name = name;
                const bool is_input = engine_->getTensorIOMode(name) == nvinfer1::TensorIOMode::kINPUT;
                const bool supported = engine_->getTensorDataType(name) == nvinfer1::DataType::kFLOAT
                    && engine_->getTensorLocation(name) == nvinfer1::TensorLocation::kDEVICE
                    && engine_->getTensorFormat(name) == nvinfer1::TensorFormat::kLINEAR
                    && !engine_->isShapeInferenceIO(name);
                binding.dims = engine_->getTensorShape(name);
#else
                const char* name = engine_->getBindingName(i);
                if (!name) throw std::runtime_error("Null TensorRT binding name");
                binding.name = name;
                const bool is_input = engine_->bindingIsInput(i);
                const bool supported = engine_->getBindingDataType(i) == nvinfer1::DataType::kFLOAT
                    && engine_->getLocation(i) == nvinfer1::TensorLocation::kDEVICE
                    && engine_->getBindingFormat(i) == nvinfer1::TensorFormat::kLINEAR
                    && !engine_->isShapeBinding(i);
                binding.dims = engine_->getBindingDimensions(i);
#endif
                if (!supported) {
                    throw std::runtime_error("Expected FP32 linear device I/O tensor: " + binding.name
                        + ". FP16 internal layers are supported, but model I/O must be FP32.");
                }
                binding.dsize = sizeof(float);
                if (is_input) {
                    input_bindings.push_back(binding);
                    input_index_ = i;
                } else {
                    output_bindings.push_back(binding);
                    output_index_ = i;
                }
            }
            num_inputs = static_cast<int>(input_bindings.size());
            num_outputs = static_cast<int>(output_bindings.size());
            if (num_inputs != 1 || num_outputs != 1) {
                throw std::runtime_error("Expected one image input and one raw YOLO output; export with NMS disabled");
            }
            auto shape = input_bindings[0].dims;
            bool dynamic = false;
            for (int i = 0; i < shape.nbDims; ++i) dynamic |= shape.d[i] < 0;
            if (dynamic) {
#if NV_TENSORRT_MAJOR >= 10
                shape = engine_->getProfileShape(input_bindings[0].name.c_str(), 0, nvinfer1::OptProfileSelector::kOPT);
#else
                shape = engine_->getProfileDimensions(input_index_, 0, nvinfer1::OptProfileSelector::kOPT);
#endif
            }
            set_input_shape(shape, true);
            std::cout << "[TensorRT " << NV_TENSORRT_MAJOR << '.' << NV_TENSORRT_MINOR
                      << "] " << input_bindings[0].name << " [1,3,"
                      << shape.d[2] << ',' << shape.d[3] << "] -> "
                      << output_bindings[0].name << " [1,"
                      << output_bindings[0].dims.d[1] << ','
                      << output_bindings[0].dims.d[2] << "]\n";
        } catch (...) {
            release();
            throw;
        }
    }

    virtual ~Runtime() { release(); }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    void make_pipe(bool warmup = true)
    {
        if (ready_) return;
        allocate_buffers();
        ready_ = true;
        if (warmup) {
            check_cuda(cudaMemsetAsync(device_ptrs[0], 0, input_bindings[0].size * sizeof(float), stream_));
            for (int i = 0; i < 10; ++i) infer();
            std::cout << "model warmup 10 times\n";
        }
    }

    void infer()
    {
        if (!ready_) throw std::runtime_error("Call make_pipe before infer");
#if NV_TENSORRT_MAJOR >= 10
        const bool ok = context_->enqueueV3(stream_);
#else
        const bool ok = context_->enqueueV2(enqueue_bindings_.data(), stream_, nullptr);
#endif
        if (!ok) throw std::runtime_error("TensorRT enqueue failed");
        check_cuda(cudaMemcpyAsync(host_ptrs[0], device_ptrs[1],
            output_bindings[0].size * sizeof(float), cudaMemcpyDeviceToHost, stream_));
        check_cuda(cudaStreamSynchronize(stream_));
    }

protected:
    void set_input_shape(const nvinfer1::Dims& shape, bool force = false)
    {
        if (shape.nbDims != 4 || shape.d[0] != 1 || shape.d[1] != 3) {
            throw std::runtime_error("Expected image input [1,3,H,W] (batch=1)");
        }
        const auto input_size = volume(shape);
        if (!force && same_shape(shape, input_bindings[0].dims)) return;
        check_cuda(cudaStreamSynchronize(stream_));
#if NV_TENSORRT_MAJOR >= 10
        const bool ok = context_->setInputShape(input_bindings[0].name.c_str(), shape);
#else
        const bool ok = context_->setBindingDimensions(input_index_, shape);
#endif
        if (!ok) throw std::runtime_error("Input size is outside engine shape/profile 0; rebuild engine or correct input_size");
#if NV_TENSORRT_MAJOR >= 10
        const auto out = context_->getTensorShape(output_bindings[0].name.c_str());
#else
        const auto out = context_->getBindingDimensions(output_index_);
#endif
        const auto output_size = volume(out);
        if (out.nbDims != 3 || out.d[0] != 1) {
            throw std::runtime_error("Expected raw YOLO output [1,channels,anchors]; end-to-end/NMS engines are unsupported");
        }
        input_bindings[0].dims = shape;
        input_bindings[0].size = input_size;
        output_bindings[0].dims = out;
        output_bindings[0].size = output_size;
        if (ready_) allocate_buffers();
    }

    void copy_input(const float* data, std::size_t bytes)
    {
        if (!ready_) throw std::runtime_error("Call make_pipe before copy_from_Mat");
        if (!data || bytes != input_bindings[0].size * sizeof(float)) {
            throw std::runtime_error("Preprocessed image size does not match TensorRT input");
        }
        // Own pinned staging memory: cv::Mat can die immediately after this call.
        check_cuda(cudaStreamSynchronize(stream_));
        std::memcpy(input_host_, data, bytes);
        check_cuda(cudaMemcpyAsync(device_ptrs[0], input_host_, bytes, cudaMemcpyHostToDevice, stream_));
    }

    void validate_output(int labels, int extra_channels) const
    {
        if (!ready_ || labels <= 0 || labels > INT_MAX - extra_channels
            || output_bindings[0].dims.d[1] != labels + extra_channels) {
            throw std::runtime_error("num_labels or output layout mismatch: expected [1,4+classes"
                "+optional_angle,anchors]. Use raw YOLOv8 output and the correct detection/obb mode.");
        }
    }

private:
    Logger logger_;
    nvinfer1::IRuntime* runtime_ = nullptr;
    nvinfer1::ICudaEngine* engine_ = nullptr;
    nvinfer1::IExecutionContext* context_ = nullptr;
    cudaStream_t stream_ = nullptr;
    void* input_host_ = nullptr;
    std::size_t input_capacity_ = 0;
    std::size_t output_capacity_ = 0;
    int input_index_ = -1;
    int output_index_ = -1;
    bool ready_ = false;
#if NV_TENSORRT_MAJOR < 10
    std::vector<void*> enqueue_bindings_;
#endif

    void allocate_buffers()
    {
        check_cuda(cudaStreamSynchronize(stream_));
        device_ptrs.resize(2, nullptr);
        host_ptrs.resize(1, nullptr);
        const auto in_bytes = input_bindings[0].size * sizeof(float);
        const auto out_bytes = output_bindings[0].size * sizeof(float);
        // cudaMalloc avoids the optional stream-ordered memory-pool requirement
        // of cudaMallocAsync on older JetPack installations.
        if (in_bytes > input_capacity_) {
            if (device_ptrs[0]) { check_cuda(cudaFree(device_ptrs[0])); device_ptrs[0] = nullptr; }
            if (input_host_) { check_cuda(cudaFreeHost(input_host_)); input_host_ = nullptr; }
            input_capacity_ = 0;
            check_cuda(cudaMalloc(&device_ptrs[0], in_bytes));
            check_cuda(cudaHostAlloc(&input_host_, in_bytes, cudaHostAllocDefault));
            input_capacity_ = in_bytes;
        }
        if (out_bytes > output_capacity_) {
            if (device_ptrs[1]) { check_cuda(cudaFree(device_ptrs[1])); device_ptrs[1] = nullptr; }
            if (host_ptrs[0]) { check_cuda(cudaFreeHost(host_ptrs[0])); host_ptrs[0] = nullptr; }
            output_capacity_ = 0;
            check_cuda(cudaMalloc(&device_ptrs[1], out_bytes));
            check_cuda(cudaHostAlloc(&host_ptrs[0], out_bytes, cudaHostAllocDefault));
            output_capacity_ = out_bytes;
        }
#if NV_TENSORRT_MAJOR >= 10
        if (!context_->setTensorAddress(input_bindings[0].name.c_str(), device_ptrs[0])
            || !context_->setTensorAddress(output_bindings[0].name.c_str(), device_ptrs[1])) {
            throw std::runtime_error("TensorRT setTensorAddress failed");
        }
#else
        enqueue_bindings_[input_index_] = device_ptrs[0];
        enqueue_bindings_[output_index_] = device_ptrs[1];
#endif
    }

    void release() noexcept
    {
        if (stream_) cudaStreamSynchronize(stream_);
        // delete is supported since TRT8; destroy() was removed in TRT10.
        delete context_; context_ = nullptr;
        for (void* p : device_ptrs) if (p) cudaFree(p);
        for (void* p : host_ptrs) if (p) cudaFreeHost(p);
        device_ptrs.clear(); host_ptrs.clear();
        if (input_host_) { cudaFreeHost(input_host_); input_host_ = nullptr; }
        if (stream_) { cudaStreamDestroy(stream_); stream_ = nullptr; }
        delete engine_; engine_ = nullptr;
        delete runtime_; runtime_ = nullptr;
    }
};
}  // namespace yolo_trt
