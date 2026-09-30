// Runs the same runtime/preprocess/postprocess without ROS topics or a camera.
#include "yolov8-detect.h"
#include "yolov8-obb.h"
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 4 && argc != 6) {
        std::cerr << "Usage: yolo_trt_smoke ENGINE detection|obb NUM_LABELS [WIDTH HEIGHT]\n";
        return 2;
    }
    try {
        yolo_trt::check_cuda(cudaSetDevice(0));
        const std::string mode = argv[2];
        const int labels = std::stoi(argv[3]);
        // Deterministic BGR8 test image. This checks execution, not accuracy.
        cv::Mat image(480, 640, CV_8UC3, cv::Scalar(114, 114, 114));
        const auto run = [&](auto& detector, auto& objects) {
            detector.make_pipe(true);
            if (argc == 6) {
                cv::Size size{std::stoi(argv[4]), std::stoi(argv[5])};
                detector.copy_from_Mat(image, size);
            } else {
                detector.copy_from_Mat(image);
            }
            detector.infer();
            detector.postprocess(objects, 0.25f, 0.65f, 100, labels);
            std::cout << "PASS: engine load, warmup, input copy, inference, output copy and "
                         "postprocess completed; detections=" << objects.size() << '\n';
        };
        if (mode == "detection") {
            YOLOv8 detector(argv[1]);
            std::vector<det::Object> objects;
            run(detector, objects);
        } else if (mode == "obb") {
            YOLOv8_obb detector(argv[1]);
            std::vector<obb::Object> objects;
            run(detector, objects);
        } else {
            throw std::runtime_error("Mode must be detection or obb");
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
