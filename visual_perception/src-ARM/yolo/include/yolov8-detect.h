#ifndef DETECT_NORMAL_YOLOV8_HPP
#define DETECT_NORMAL_YOLOV8_HPP
#include "NvInferPlugin.h"
#include "common-detect.hpp"
#include <fstream>
#include "trt-runtime.hpp"

using namespace det;

class YOLOv8 : public yolo_trt::Runtime<det::Binding> {
public:
    explicit YOLOv8(const std::string& engine_file_path);
    ~YOLOv8() = default;

    void                 copy_from_Mat(const cv::Mat& image);
    void                 copy_from_Mat(const cv::Mat& image, cv::Size& size);
    void                 letterbox(const cv::Mat& image, cv::Mat& out, cv::Size& size);
    void                 postprocess(std::vector<det::Object>& objs,
                                     float                score_thres = 0.25f,
                                     float                iou_thres   = 0.65f,
                                     int                  topk        = 100,
                                     int                  num_labels  = 80);
    static void          draw_objects(const cv::Mat&                                image,
                                      cv::Mat&                                      res,
                                      const std::vector<det::Object>&                    objs,
                                      const std::vector<std::string>&               CLASS_NAMES,
                                      const std::vector<std::vector<unsigned int>>& COLORS);
    det::PreParam pparam;

};

#endif