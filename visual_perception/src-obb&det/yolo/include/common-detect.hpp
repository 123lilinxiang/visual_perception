//
// Created by ubuntu on 1/24/23.
//

#ifndef DETECT_NORMAL_COMMON_HPP
#define DETECT_NORMAL_COMMON_HPP

#include "NvInfer.h"
#include "filesystem.hpp"
#include "opencv2/opencv.hpp"

#include <cuda_runtime_api.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(call)                                                         \
    do {                                                                    \
        const cudaError_t error_code = (call);                              \
        if (error_code != cudaSuccess) {                                    \
            std::fprintf(stderr, "CUDA Error:\n");                          \
            std::fprintf(stderr, "    File:       %s\n", __FILE__);         \
            std::fprintf(stderr, "    Line:       %d\n", __LINE__);         \
            std::fprintf(                                                   \
                stderr,                                                     \
                "    Error code: %d\n",                                     \
                static_cast<int>(error_code)                                \
            );                                                              \
            std::fprintf(                                                   \
                stderr,                                                     \
                "    Error text: %s\n",                                     \
                cudaGetErrorString(error_code)                              \
            );                                                              \
            std::exit(EXIT_FAILURE);                                        \
        }                                                                   \
    } while (0)

namespace det
{

class Logger : public nvinfer1::ILogger
{
public:
    nvinfer1::ILogger::Severity reportableSeverity;

    explicit Logger(
        nvinfer1::ILogger::Severity severity =
            nvinfer1::ILogger::Severity::kINFO)
        : reportableSeverity(severity)
    {
    }

    void log(
        nvinfer1::ILogger::Severity severity,
        const char* msg) noexcept override
    {
        if (severity > reportableSeverity) {
            return;
        }

        switch (severity) {
            case nvinfer1::ILogger::Severity::kINTERNAL_ERROR:
                std::cerr << "INTERNAL_ERROR: ";
                break;

            case nvinfer1::ILogger::Severity::kERROR:
                std::cerr << "ERROR: ";
                break;

            case nvinfer1::ILogger::Severity::kWARNING:
                std::cerr << "WARNING: ";
                break;

            case nvinfer1::ILogger::Severity::kINFO:
                std::cerr << "INFO: ";
                break;

            default:
                std::cerr << "VERBOSE: ";
                break;
        }

        std::cerr << msg << std::endl;
    }
};

inline int get_size_by_dims(
    const nvinfer1::Dims& dims)
{
    int size = 1;

    for (int i = 0; i < dims.nbDims; ++i) {
        size *= dims.d[i];
    }

    return size;
}

inline int type_to_size(
    const nvinfer1::DataType& data_type)
{
    switch (data_type) {
        case nvinfer1::DataType::kFLOAT:
            return 4;

        case nvinfer1::DataType::kHALF:
            return 2;

        case nvinfer1::DataType::kINT32:
            return 4;

        case nvinfer1::DataType::kINT8:
            return 1;

        case nvinfer1::DataType::kBOOL:
            return 1;

        default:
            return 4;
    }
}

inline float clamp(
    float value,
    float minimum,
    float maximum)
{
    return value > minimum
        ? (value < maximum ? value : maximum)
        : minimum;
}

struct Binding
{
    size_t size = 1;
    size_t dsize = 1;

    nvinfer1::Dims dims{};
    std::string name;
};

struct Object
{
    cv::Rect_<float> rect;

    int label = 0;
    float prob = 0.0f;
};

struct PreParam
{
    float ratio = 1.0f;
    float dw = 0.0f;
    float dh = 0.0f;

    float height = 0.0f;
    float width = 0.0f;
};

}  // namespace det

#endif  // DETECT_NORMAL_COMMON_HPP