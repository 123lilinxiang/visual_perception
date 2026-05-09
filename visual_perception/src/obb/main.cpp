//
// Created by ubuntu on 4/7/23.
//
#include "opencv2/opencv.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>

#include <cuda_runtime_api.h>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <cv_bridge/cv_bridge.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include "yolov8-obb.h"
#include "cam2robot.h"
#include <yaml-cpp/yaml.h>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include "msg_det/msg/detect_res.hpp"
#include <stdexcept>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace fs = ghc::filesystem;

const std::vector<std::string> CLASS_NAMES = {"0","1"};
const std::vector<std::vector<unsigned int>> COLORS = {{0, 114, 189},
                                                       {217, 83, 25}};
std::vector<std::vector<float>> result_array;


namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kDegToRad = kPi / 180.0;

    YAML::Node getRequiredYamlNode(const YAML::Node& node, const std::string& key)
    {
        if (!node || !node[key]) {
            throw std::runtime_error("Missing YAML key: " + key);
        }
        return node[key];
    }

    Eigen::Vector3d readVec3FromYaml(const YAML::Node& node, const std::string& key)
    {
        YAML::Node value = getRequiredYamlNode(node, key);

        if (!value.IsSequence() || value.size() != 3) {
            throw std::runtime_error("YAML key `" + key + "` must be a 3-element array.");
        }

        return Eigen::Vector3d(
            value[0].as<double>(),
            value[1].as<double>(),
            value[2].as<double>()
        );
    }
}


class YoloObbDepthNode : public rclcpp::Node
{
public:
    explicit YoloObbDepthNode(const std::string& engine_path, const YAML::Node& config)
        : Node("yolov8_obb_depth_node")
    {
        cudaSetDevice(0);

        image_size_ = cv::Size{640, 640};
        num_labels_ = 2;
        topk_ = 100;
        score_thres_ = 0.25f;
        iou_thres_ = 0.65f;

        yolov8_obb_ = std::make_unique<YOLOv8_obb>(engine_path);
        yolov8_obb_->make_pipe(true);
        detect_pub = this->create_publisher<msg_det::msg::DetectRes>("/robot/pps/detect_res",10);
        
        const YAML::Node coord_trans = getRequiredYamlNode(config, "coord_trans");
        const Eigen::Vector3d head_joint_yaw_offset =readVec3FromYaml(coord_trans, "head_joint_yaw_offset");
        const Eigen::Vector3d head_joint_pitch_offset =readVec3FromYaml(coord_trans, "head_joint_pitch_offset");
        const Eigen::Vector3d camera_offset =readVec3FromYaml(coord_trans, "camera_offset");

        // YAML 里是角度 65.0，这里转成弧度
        const double camera_tilt_deg =getRequiredYamlNode(coord_trans, "camera_tilt").as<double>();
        const double camera_tilt_rad = camera_tilt_deg * kDegToRad;

        transformer_ = std::make_unique<CamBaseTransformer>(
            head_joint_yaw_offset,
            head_joint_pitch_offset,
            camera_offset,
            camera_tilt_rad
        );
        

        color_topic_ = this->declare_parameter<std::string>("color_topic","/camera/camera/color/image_raw");
        depth_topic_ = this->declare_parameter<std::string>("depth_topic","/camera/camera/aligned_depth_to_color/image_raw");
        show_image_ = this->declare_parameter<bool>("show_image", true);

        color_sub_.subscribe(this, color_topic_);
        depth_sub_.subscribe(this, depth_topic_);

        sync_ = std::make_shared<Synchronizer>(SyncPolicy(10),color_sub_,depth_sub_);

        sync_->registerCallback(std::bind(&YoloObbDepthNode::imageCallback,this,std::placeholders::_1,std::placeholders::_2));

        if (show_image_) {
            cv::namedWindow("result", cv::WINDOW_AUTOSIZE);
        }

        std::string timestamp = getCurrentTimeString();
        RCLCPP_INFO(this->get_logger(),"[%s] YOLO OBB Depth Node started.",timestamp.c_str());
        RCLCPP_INFO(this->get_logger(),"[%s] Color topic: %s",timestamp.c_str(),color_topic_.c_str());
        RCLCPP_INFO(this->get_logger(),"[%s] Depth topic: %s",timestamp.c_str(),depth_topic_.c_str());
    }

    ~YoloObbDepthNode()
    {
        if (show_image_) {
            cv::destroyAllWindows();
        }
    }

private:
    using ImageMsg = sensor_msgs::msg::Image;
    using SyncPolicy = message_filters::sync_policies::ApproximateTime<ImageMsg, ImageMsg>;
    using Synchronizer = message_filters::Synchronizer<SyncPolicy>;

    float getDepthValueMeters(const cv::Mat& depth_img, int x, int y, int radius = 2)
    {
        if (depth_img.empty()) {
            return -1.0f;
        }

        if (x < 0 || y < 0 || x >= depth_img.cols || y >= depth_img.rows) {
            return -1.0f;
        }

        std::vector<float> values;

        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                int px = x + dx;
                int py = y + dy;

                if (px < 0 || py < 0 || px >= depth_img.cols || py >= depth_img.rows) {
                    continue;
                }

                float depth_m = -1.0f;

                if (depth_img.type() == CV_16UC1) {
                    uint16_t raw = depth_img.at<uint16_t>(py, px);
                    if (raw == 0) {
                        continue;
                    }

                    // RealSense 16UC1 depth 一般单位是 mm
                    depth_m = static_cast<float>(raw) / 1000.0f;
                }
                else if (depth_img.type() == CV_32FC1) {
                    float raw = depth_img.at<float>(py, px);
                    if (!std::isfinite(raw) || raw <= 0.0f) {
                        continue;
                    }

                    // 32FC1 一般已经是 m
                    depth_m = raw;
                }
                else {
                    continue;
                }

                if (depth_m > 0.0f) {
                    values.push_back(depth_m);
                }
            }
        }

        if (values.empty()) {
            return -1.0f;
        }

        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    }

    Eigen::Vector3d pixelToCameraPoint(double u,double v,double depth_m,double fx,double fy,double cx,double cy)
    {
        double X = (u - cx) * depth_m / fx;
        double Y = (v - cy) * depth_m / fy;
        double Z = depth_m;

        return Eigen::Vector3d(X, Y, Z);
    }

    void imageCallback(const ImageMsg::ConstSharedPtr& color_msg,
                       const ImageMsg::ConstSharedPtr& depth_msg)
    {
        result_array.clear();
        cv_bridge::CvImageConstPtr color_ptr;
        cv_bridge::CvImageConstPtr depth_ptr;

        try {
            // RealSense color 可能是 RGB8，这里转成 OpenCV 常用 BGR8
            color_ptr = cv_bridge::toCvShare(
                color_msg,
                sensor_msgs::image_encodings::BGR8
            );

            // 深度图不要强制转 bgr，保持 16UC1 / 32FC1
            depth_ptr = cv_bridge::toCvShare(depth_msg);
        }
        catch (const cv_bridge::Exception& e) {
            RCLCPP_ERROR(this->get_logger(), "cv_bridge exception: %s", e.what());
            return;
        }

        cv::Mat image = color_ptr->image.clone();
        cv::Mat depth = depth_ptr->image;

        if (image.empty() || depth.empty()) {
            RCLCPP_WARN(this->get_logger(), "Empty image or depth frame.");
            return;
        }

        std::vector<Object> objs;
        cv::Mat res;

        yolov8_obb_->copy_from_Mat(image, image_size_);
        yolov8_obb_->infer();
        
        yolov8_obb_->postprocess(
            objs,
            score_thres_,
            iou_thres_,
            topk_,
            num_labels_
        );

        // 这个函数会把 [类别ID, 中心点x, 中心点y] 写入 result_array
        yolov8_obb_->draw_objects(
            image,
            res,
            objs,
            CLASS_NAMES,
            COLORS,
            result_array
        );

        // 根据中心点从深度图取深度
        for (size_t i = 0; i < result_array.size(); ++i) 
        {
            int cx_color = static_cast<int>(result_array[i][1]);
            int cy_color = static_cast<int>(result_array[i][2]);

            // 如果 color 和 depth 分辨率不同，做比例缩放
            int cx_depth = static_cast<int>(
                static_cast<float>(cx_color) * depth.cols / image.cols
            );
            int cy_depth = static_cast<int>(
                static_cast<float>(cy_color) * depth.rows / image.rows
            );

            float depth_m = getDepthValueMeters(depth, cx_depth, cy_depth, 2);

            // 追加 depth，变成 [class, cx, cy, depth_m]
            result_array[i].push_back(depth_m);

            if (depth_m <= 0.0f) {
                std::cout << "["
                        << result_array[i][0] << ", "
                        << result_array[i][1] << ", "
                        << result_array[i][2] << ", "
                        << "invalid_depth"
                        << "]";

                if (i != result_array.size() - 1) {
                    std::cout << ", ";
                }

                continue;
            }

            // 检测中心点，单位是像素
            double u = result_array[i][1];
            double v = result_array[i][2];

            // 如果你使用的是 aligned_depth_to_color，且 color/depth 分辨率一致，直接用 u/v。
            // 如果不一致，可以用缩放后的 cx_depth/cy_depth。
            // 这里推荐用 D2C 后的图像，且 color/depth 都是 640x480。   
            Eigen::Vector3d point_camera = pixelToCameraPoint(
                u,
                v,
                depth_m,
                fx_,
                fy_,
                cx0_,
                cy0_
            );

            // 如果当前头不转，先写 0。
            // 后面如果要接机器人头部角度，就把真实 yaw/pitch 填进来。
            double head_yaw = 0.0;
            double head_pitch = 0.0;

            Eigen::Vector3d point_robot = transformer_->transformPoint(
                point_camera,
                head_yaw,
                head_pitch
            );

            // 追加机器人坐标，变成：
            // [class, cx, cy, depth_m, robot_x, robot_y, robot_z]
            result_array[i].push_back(static_cast<float>(point_robot.x()));
            result_array[i].push_back(static_cast<float>(point_robot.y()));
            result_array[i].push_back(static_cast<float>(point_robot.z()));

            // 发布 ROS2 topic
            msg_det::msg::DetectRes msg;

            msg.type = static_cast<int16_t>(result_array[i][0]);
            msg.pos = {
                static_cast<double>(point_robot.x()),
                static_cast<double>(point_robot.y()),
                static_cast<double>(point_robot.z())
            };

            detect_pub->publish(msg);

            std::string timestamp = getCurrentTimeString();

            RCLCPP_INFO(
                this->get_logger(),
                "[%s] Published detect result: type=%d, pos=[%.3f, %.3f, %.3f], depth=%.3f",
                timestamp.c_str(),
                msg.type,
                msg.pos[0],
                msg.pos[1],
                msg.pos[2],
                depth_m
            );

            std::cout << "["
                    << "class=" << result_array[i][0] << ", "
                    << "cx=" << result_array[i][1] << ", "
                    << "cy=" << result_array[i][2] << ", "
                    << "depth=" << result_array[i][3] << "m, "
                    << "robot_x=" << result_array[i][4] << ", "
                    << "robot_y=" << result_array[i][5] << ", "
                    << "robot_z=" << result_array[i][6]
                    << "]";

            if (i != result_array.size() - 1) {
                std::cout << ", ";
            }
        }

        if (show_image_) {
            cv::imshow("result", res);
            cv::waitKey(1);
        }
    }

    std::string getCurrentTimeString()
    {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ) % 1000;

        std::tm tm_buf;
        localtime_r(&now_time_t, &tm_buf);

        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
            << "."
            << std::setfill('0') << std::setw(3) << ms.count();

        return oss.str();
    }

private:
    std::unique_ptr<YOLOv8_obb> yolov8_obb_;

    message_filters::Subscriber<ImageMsg> color_sub_;
    message_filters::Subscriber<ImageMsg> depth_sub_;
    std::shared_ptr<Synchronizer> sync_;

    std::string color_topic_;
    std::string depth_topic_;

    cv::Size image_size_;
    int num_labels_;
    int topk_;
    float score_thres_;
    float iou_thres_;
    bool show_image_;

    std::unique_ptr<CamBaseTransformer> transformer_;
    // 相机内参，建议后面从 camera_info 读取
    double fx_ = 605.7542114257812;
    double fy_ = 605.45703125;
    double cx0_ = 326.2287902832031;
    double cy0_ = 259.4130859375;
    rclcpp::Publisher<msg_det::msg::DetectRes>::SharedPtr detect_pub;
};


int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);

    try {
        const std::string config_path =
            ament_index_cpp::get_package_share_directory("yolov8_obb") +
            "/config/vision_config.yaml";

        const YAML::Node config = YAML::LoadFile(config_path);

        if (!config["detector"] ||
            !config["detector"]["obb_predictor"] ||
            !config["detector"]["obb_predictor"]["engine_path"]) {
            throw std::runtime_error(
                "Missing YAML key: detector.obb_predictor.engine_path"
            );
        }

        const std::string engine_path =
            config["detector"]["obb_predictor"]["engine_path"].as<std::string>();

        auto node = std::make_shared<YoloObbDepthNode>(engine_path, config);

        rclcpp::spin(node);
    }
    catch (const std::exception& e) {
        RCLCPP_FATAL(
            rclcpp::get_logger("yolov8_obb_depth_node"),
            "Failed to start node: %s",
            e.what()
        );

        rclcpp::shutdown();
        return 1;
    }

    rclcpp::shutdown();
    return 0;
}
