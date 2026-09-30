#ifndef OBB_NORMAL_YOLOv8_obb_HPP
#define OBB_NORMAL_YOLOv8_obb_HPP

#include "NvInferPlugin.h"
#include "common-obb.hpp"
#include <fstream>
#include "trt-runtime.hpp"
#include <chrono>
#include <cmath>
using namespace obb;



class YOLOv8_obb : public yolo_trt::Runtime<obb::Binding> {
public:
    explicit YOLOv8_obb(const std::string& engine_file_path);

    ~YOLOv8_obb() = default;


    void copy_from_Mat(const cv::Mat& image);

    void copy_from_Mat(const cv::Mat& image, cv::Size& size);

    void letterbox(const cv::Mat& image, cv::Mat& out, cv::Size& size);


    void postprocess(std::vector<obb::Object>& objs,
                     float                score_thres = 0.25f,
                     float                iou_thres   = 0.65f,
                     int                  topk        = 100,
                     int                  num_labels  = 15);

    static void draw_objects(const cv::Mat&                                image,
                             cv::Mat&                                      res,
                             const std::vector<obb::Object>&                    objs,
                             const std::vector<std::string>&               CLASS_NAMES,
                             const std::vector<std::vector<unsigned int>>& COLORS,
                             std::vector<std::vector<float>>& result_array);

    obb::PreParam pparam;

};

#endif