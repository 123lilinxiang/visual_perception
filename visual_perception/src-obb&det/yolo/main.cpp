#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <cuda_runtime_api.h>
#include <opencv2/opencv.hpp>
#include <yaml-cpp/yaml.h>

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <cv_bridge/cv_bridge.h>
#include <message_filters/subscriber.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <message_filters/synchronizer.h>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "msg_det/msg/detect_res.hpp"

// 请按你的项目实际头文件路径修改
#include "yolov8-obb.h"
#include "yolov8-detect.h"
#include "cam2robot.h"

const std::vector<std::string> OBB_CLASS_NAMES = {"0"};

const std::vector<std::vector<unsigned int>> OBB_COLORS = {{0, 114, 189}};

const std::vector<std::string> DETECT_CLASS_NAMES = {"0"};

const std::vector<std::vector<unsigned int>> DETECT_COLORS = {{0, 114, 189}};

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

YAML::Node getRequiredYamlNode(
    const YAML::Node& node,
    const std::string& key)
{
    if (!node || !node[key]) {
        throw std::runtime_error(
            "Missing YAML key: " + key
        );
    }

    return node[key];
}

Eigen::Vector3d readVec3FromYaml(
    const YAML::Node& node,
    const std::string& key)
{
    const YAML::Node value =
        getRequiredYamlNode(node, key);

    if (!value.IsSequence() || value.size() != 3) {
        throw std::runtime_error(
            "YAML key `" + key +
            "` must be a 3-element array."
        );
    }

    return Eigen::Vector3d(
        value[0].as<double>(),
        value[1].as<double>(),
        value[2].as<double>()
    );
}

Eigen::Vector4d readVec4FromYaml(
    const YAML::Node& node,
    const std::string& key)
{
    const YAML::Node value =
        getRequiredYamlNode(node, key);

    if (!value.IsSequence() || value.size() != 4) {
        throw std::runtime_error(
            "YAML key `" + key +
            "` must be a 4-element array."
        );
    }

    return Eigen::Vector4d(
        value[0].as<double>(),
        value[1].as<double>(),
        value[2].as<double>(),
        value[3].as<double>()
    );
}

std::unique_ptr<CamBaseTransformer> createTransformer(const YAML::Node& config)
{
    const YAML::Node coord_trans =getRequiredYamlNode(config, "coord_trans");

    const Eigen::Vector3d head_joint_yaw_offset =
        readVec3FromYaml(
            coord_trans,
            "head_joint_yaw_offset"
        );

    const Eigen::Vector3d head_joint_pitch_offset =
        readVec3FromYaml(
            coord_trans,
            "head_joint_pitch_offset"
        );

    const Eigen::Vector3d camera_offset =
        readVec3FromYaml(
            coord_trans,
            "camera_offset"
        );

    const double camera_tilt_deg =
        getRequiredYamlNode(
            coord_trans,
            "camera_tilt"
        ).as<double>();

    const double camera_tilt_rad =
        camera_tilt_deg * kDegToRad;

    return std::make_unique<CamBaseTransformer>(
        head_joint_yaw_offset,
        head_joint_pitch_offset,
        camera_offset,
        camera_tilt_rad
    );
}

float getDepthValueMeters(
    const cv::Mat& depth_img,
    int x,
    int y,
    int radius = 2)
{
    if (depth_img.empty()) {
        return -1.0f;
    }

    if (x < 0 ||
        y < 0 ||
        x >= depth_img.cols ||
        y >= depth_img.rows)
    {
        return -1.0f;
    }

    std::vector<float> values;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int px = x + dx;
            const int py = y + dy;

            if (px < 0 ||
                py < 0 ||
                px >= depth_img.cols ||
                py >= depth_img.rows)
            {
                continue;
            }

            float depth_m = -1.0f;

            if (depth_img.type() == CV_16UC1) {
                const uint16_t raw =
                    depth_img.at<uint16_t>(py, px);

                if (raw == 0) {
                    continue;
                }

                // RealSense 对齐深度通常为毫米
                depth_m = static_cast<float>(raw) / 1000.0f;
            }
            else if (depth_img.type() == CV_32FC1) {
                const float raw =
                    depth_img.at<float>(py, px);

                if (!std::isfinite(raw) || raw <= 0.0f) {
                    continue;
                }

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
    const double x =(u - cx) * depth_m / fx;
    const double y =(v - cy) * depth_m / fy;
    return Eigen::Vector3d(x,y,depth_m);
}

bool computeNormalFromDepthCircle(const cv::Mat& depth,int center_x,int center_y,int radius,double fx,double fy,double cx0,double cy0,
    Eigen::Vector3d& normal_camera,Eigen::Vector3d& center_camera)
{
    normal_camera.setZero();
    center_camera.setZero();

    if (depth.empty() || radius <= 0) {
        return false;
    }

    if (center_x < 0 ||
        center_x >= depth.cols ||
        center_y < 0 ||
        center_y >= depth.rows)
    {
        return false;
    }

    if (!std::isfinite(fx) ||!std::isfinite(fy) ||fx <= 0.0 ||fy <= 0.0)
    {
        return false;
    }

    std::vector<Eigen::Vector3d> points;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }

            const int u = center_x + dx;
            const int v = center_y + dy;

            if (u < 0 ||u >= depth.cols ||v < 0 ||v >= depth.rows)
            {
                continue;
            }

            const float z =getDepthValueMeters(depth, u, v, 1);

            if (!std::isfinite(z) || z <= 0.0f) {
                continue;
            }

            const double x =(static_cast<double>(u) - cx0) * z / fx;
            const double y =(static_cast<double>(v) - cy0) * z / fy;
            points.emplace_back(x, y, z);
        }
    }

    if (points.size() < 20) {
        return false;
    }

    for (const auto& point : points) {
        center_camera += point;
    }

    center_camera /=static_cast<double>(points.size());

    Eigen::Matrix3d covariance =Eigen::Matrix3d::Zero();

    for (const auto& point : points) {
        const Eigen::Vector3d offset =
            point - center_camera;

        covariance +=offset * offset.transpose();
    }

    covariance /=static_cast<double>(points.size());

    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d>solver(covariance);

    if (solver.info() != Eigen::Success) {
        return false;
    }

    normal_camera =solver.eigenvectors().col(0);

    const double normal_length =normal_camera.norm();

    if (!std::isfinite(normal_length) ||normal_length < 1e-8)
    {
        normal_camera.setZero();
        return false;
    }

    normal_camera.normalize();

    // 令法向量朝向相机，大致指向 -Z
    if (normal_camera.dot(center_camera) > 0.0) {
        normal_camera = -normal_camera;
    }

    return normal_camera.allFinite();
}

std::string getCurrentTimeString()
{
    const auto now =std::chrono::system_clock::now();

    const auto now_time_t =std::chrono::system_clock::to_time_t(now);

    const auto milliseconds =std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
    localtime_r(&now_time_t, &tm_buf);

    std::ostringstream stream;
    stream
        << std::put_time(
            &tm_buf,
            "%Y-%m-%d %H:%M:%S"
        )
        << "."
        << std::setfill('0')
        << std::setw(3)
        << milliseconds.count();

    return stream.str();
}

}  // namespace

class YoloObbDepthNode : public rclcpp::Node
{
public:
    using ImageMsg = sensor_msgs::msg::Image;
    using SyncPolicy =message_filters::sync_policies::ApproximateTime<ImageMsg,ImageMsg>;
    using Synchronizer =message_filters::Synchronizer<SyncPolicy>;

    explicit YoloObbDepthNode(const std::string& engine_path,const YAML::Node& config): Node("yolov8_obb_depth_node")
    {
        const cudaError_t cuda_result =cudaSetDevice(0);

        if (cuda_result != cudaSuccess) {
            throw std::runtime_error(
                std::string("cudaSetDevice failed: ") +
                cudaGetErrorString(cuda_result)
            );
        }

        const YAML::Node detector =getRequiredYamlNode(config, "detector");
        const YAML::Node predictor =getRequiredYamlNode(detector,"obb_predictor");
        image_size_ = cv::Size{640, 640};
        num_labels_ = predictor["num_labels"]? predictor["num_labels"].as<int>(): 1;
        topk_ = predictor["topk"]? predictor["topk"].as<int>(): 1;
        score_thres_ = predictor["score_threshold"]? predictor["score_threshold"].as<float>(): 0.25f;
        iou_thres_ = predictor["iou_threshold"]? predictor["iou_threshold"].as<float>(): 0.65f;
        normal_offset_y_ = predictor["normal_offset_y"]? predictor["normal_offset_y"].as<int>(): -50;
        normal_radius_ = predictor["normal_radius"]? predictor["normal_radius"].as<int>(): 10;
        offset0_ =readVec4FromYaml(predictor, "offset0");
        transformer_ = createTransformer(config);
        yolov8_obb_ =std::make_unique<YOLOv8_obb>(engine_path);
        yolov8_obb_->make_pipe(true);
        detect_pub_ =this->create_publisher<msg_det::msg::DetectRes>("/detect_res", 10);
        color_topic_ =this->declare_parameter<std::string>("color_topic","/camera/camera/color/image_raw");
        depth_topic_ =this->declare_parameter<std::string>("depth_topic","/camera/camera/aligned_depth_to_color/image_raw");
        show_image_ =this->declare_parameter<bool>("show_image",true);
        color_sub_.subscribe(this, color_topic_);
        depth_sub_.subscribe(this, depth_topic_);
        sync_ = std::make_shared<Synchronizer>(SyncPolicy(10),color_sub_,depth_sub_);

        sync_->registerCallback(std::bind(&YoloObbDepthNode::imageCallback,this,std::placeholders::_1,std::placeholders::_2));

        if (show_image_) {cv::namedWindow("obb_result",cv::WINDOW_AUTOSIZE);}

        const std::string timestamp =getCurrentTimeString();

        RCLCPP_INFO(
            this->get_logger(),
            "[%s] YOLO OBB Depth Node started.",
            timestamp.c_str()
        );

        RCLCPP_INFO(
            this->get_logger(),
            "[%s] OBB engine: %s",
            timestamp.c_str(),
            engine_path.c_str()
        );

        RCLCPP_INFO(
            this->get_logger(),
            "[%s] Color topic: %s",
            timestamp.c_str(),
            color_topic_.c_str()
        );

        RCLCPP_INFO(
            this->get_logger(),
            "[%s] Depth topic: %s",
            timestamp.c_str(),
            depth_topic_.c_str()
        );
    }

    ~YoloObbDepthNode() override
    {
        if (show_image_) {
            cv::destroyWindow("obb_result");
        }
    }

private:
    void imageCallback(const ImageMsg::ConstSharedPtr& color_msg,const ImageMsg::ConstSharedPtr& depth_msg)
    {
        cv_bridge::CvImageConstPtr color_ptr;
        cv_bridge::CvImageConstPtr depth_ptr;

        try {
            color_ptr = cv_bridge::toCvShare(color_msg,sensor_msgs::image_encodings::BGR8);
            depth_ptr = cv_bridge::toCvShare(depth_msg);
        }
        catch (const cv_bridge::Exception& exception) {
            RCLCPP_ERROR(
                this->get_logger(),
                "cv_bridge exception: %s",
                exception.what()
            );
            return;
        }

        cv::Mat image = color_ptr->image.clone();
        const cv::Mat& depth = depth_ptr->image;

        if (image.empty() || depth.empty()) {
            RCLCPP_WARN(
                this->get_logger(),
                "Empty color or depth frame."
            );
            return;
        }

        std::vector<obb::Object> objects;
        std::vector<std::vector<float>> result_array;
        cv::Mat result_image;

        yolov8_obb_->copy_from_Mat(image,image_size_);
        yolov8_obb_->infer();

        yolov8_obb_->postprocess(
            objects,
            score_thres_,
            iou_thres_,
            topk_,
            num_labels_
        );
        yolov8_obb_->draw_objects(image,result_image,objects,OBB_CLASS_NAMES,OBB_COLORS,result_array);
        
        for (std::size_t index = 0;index < result_array.size();++index)
        {
            if (result_array[index].size() < 3) {
                RCLCPP_WARN(
                    this->get_logger(),
                    "Invalid OBB result size: %zu",
                    result_array[index].size()
                );
                continue;
            }

            const int cx_color =static_cast<int>(std::round(result_array[index][1]));
            const int cy_color =static_cast<int>(std::round(result_array[index][2]));

            if (cx_color < 0 ||cx_color >= image.cols ||cy_color < 0 ||cy_color >= image.rows)
            {
                RCLCPP_WARN(
                    this->get_logger(),
                    "Invalid OBB center=[%d,%d], image=%dx%d",
                    cx_color,
                    cy_color,
                    image.cols,
                    image.rows
                );
                continue;
            }

            const double scale_x =static_cast<double>(depth.cols) /static_cast<double>(image.cols);
            const double scale_y =static_cast<double>(depth.rows) /static_cast<double>(image.rows);
            const int cx_depth =static_cast<int>(std::round(cx_color * scale_x));
            const int cy_depth =static_cast<int>(std::round(cy_color * scale_y));

            int normal_cx_color = cx_color;
            int normal_cy_color =cy_color + normal_offset_y_;

            // 法向量采样点越界时退回检测中心，避免负坐标
            if (normal_cx_color < 0 ||normal_cx_color >= image.cols ||normal_cy_color < 0 ||normal_cy_color >= image.rows)
            {
                RCLCPP_WARN(
                    this->get_logger(),
                    "Normal point outside image. "
                    "detect_center=[%d,%d], requested=[%d,%d]. "
                    "Fallback to detection center.",
                    cx_color,
                    cy_color,
                    normal_cx_color,
                    normal_cy_color
                );

                normal_cx_color = cx_color;
                normal_cy_color = cy_color;
            }

            const int normal_cx_depth =static_cast<int>(std::round(normal_cx_color * scale_x));
            const int normal_cy_depth =static_cast<int>(std::round(normal_cy_color * scale_y));
            int normal_radius_depth =static_cast<int>(std::round(static_cast<double>(normal_radius_) *(scale_x + scale_y) * 0.5));
            normal_radius_depth =std::max(1, normal_radius_depth);

            const double fx_depth = fx_ * scale_x;
            const double fy_depth = fy_ * scale_y;
            const double cx0_depth = cx0_ * scale_x;
            const double cy0_depth = cy0_ * scale_y;

            Eigen::Vector3d normal_camera = Eigen::Vector3d::Zero();

            Eigen::Vector3d normal_center_camera =Eigen::Vector3d::Zero();

            const bool normal_ok =computeNormalFromDepthCircle(depth,normal_cx_depth,normal_cy_depth,normal_radius_depth,
                    fx_depth,fy_depth,cx0_depth,cy0_depth,
                    normal_camera,normal_center_camera);

            if (!normal_ok) {normal_camera.setZero();

                RCLCPP_WARN(
                    this->get_logger(),
                    "Failed to compute normal. "
                    "center=[%d,%d], radius=%d",
                    normal_cx_depth,
                    normal_cy_depth,
                    normal_radius_depth
                );
            }

            const float depth_m =getDepthValueMeters(depth,cx_depth,cy_depth,2);

            if (!std::isfinite(depth_m) ||depth_m <= 0.0f)
            {
                RCLCPP_WARN(
                    this->get_logger(),
                    "Invalid depth at OBB center=[%d,%d]",
                    cx_depth,
                    cy_depth
                );
                continue;
            }

            const Eigen::Vector3d point_camera =pixelToCameraPoint(static_cast<double>(cx_color),static_cast<double>(cy_color),depth_m,fx_,fy_,cx0_,cy0_);

            constexpr double head_yaw = 0.0;
            constexpr double head_pitch = 0.0;

            const Eigen::Vector3d point_robot =transformer_->transformPoint(point_camera,head_yaw,head_pitch);
            msg_det::msg::DetectRes message;
            message.type =static_cast<int16_t>(result_array[index][0]);
            const Eigen::Vector4d offset =getOffsetByType(message.type);
            const Eigen::Vector3d publish_point =point_robot + offset.head<3>();

            message.pos = {
                publish_point.x(),
                publish_point.y(),
                publish_point.z()
            };

            message.normal = {
                normal_camera.x(),
                normal_camera.y(),
                normal_camera.z()
            };

            detect_pub_->publish(message);

            const std::string timestamp =getCurrentTimeString();

            RCLCPP_INFO(
                this->get_logger(),
                "[%s] OBB published: "
                "type=%d, pixel=[%d,%d], depth=%.3f, "
                "pos=[%.3f,%.3f,%.3f], "
                "normal=[%.4f,%.4f,%.4f]",
                timestamp.c_str(),
                message.type,
                cx_color,
                cy_color,
                depth_m,
                message.pos[0],
                message.pos[1],
                message.pos[2],
                message.normal[0],
                message.normal[1],
                message.normal[2]
            );
        }

        if (show_image_ && !result_image.empty()) {
            cv::imshow("obb_result", result_image);
            cv::waitKey(1);
        }
    }

    Eigen::Vector4d getOffsetByType(int type) const
    {
        switch (type) {
            case 0:
                return offset0_;

            default:
                return Eigen::Vector4d::Zero();
        }
    }

private:
    std::unique_ptr<YOLOv8_obb> yolov8_obb_;

    message_filters::Subscriber<ImageMsg> color_sub_;
    message_filters::Subscriber<ImageMsg> depth_sub_;
    std::shared_ptr<Synchronizer> sync_;

    rclcpp::Publisher<msg_det::msg::DetectRes>::SharedPtr detect_pub_;

    std::unique_ptr<CamBaseTransformer>transformer_;

    Eigen::Vector4d offset0_ =Eigen::Vector4d::Zero();

    std::string color_topic_;
    std::string depth_topic_;

    cv::Size image_size_{640, 640};

    int num_labels_{1};
    int topk_{1};
    int normal_offset_y_{-50};
    int normal_radius_{10};

    float score_thres_{0.25f};
    float iou_thres_{0.65f};

    bool show_image_{true};

    // 当前写死为彩色相机内参
    double fx_{909.701171875};
    double fy_{909.5648803710938};
    double cx0_{648.3807373046875};
    double cy0_{371.5822448730469};
};

class YoloDetectDepthNode : public rclcpp::Node
{
public:
    using ImageMsg = sensor_msgs::msg::Image;
    using SyncPolicy =message_filters::sync_policies::ApproximateTime<ImageMsg,ImageMsg>;
    using Synchronizer =message_filters::Synchronizer<SyncPolicy>;

    explicit YoloDetectDepthNode(const std::string& engine_path,const YAML::Node& config): Node("yolov8_detect_depth_node")
    {
        const cudaError_t cuda_result =cudaSetDevice(0);

        if (cuda_result != cudaSuccess) {
            throw std::runtime_error(
                std::string("cudaSetDevice failed: ") +
                cudaGetErrorString(cuda_result)
            );
        }

        const YAML::Node detector =getRequiredYamlNode(config, "detector");
        const YAML::Node predictor =getRequiredYamlNode(detector,"detection_predictor");
        image_size_ = cv::Size{640, 640};
        num_labels_ = predictor["num_labels"]? predictor["num_labels"].as<int>(): 1;
        topk_ = predictor["topk"]? predictor["topk"].as<int>(): 100;
        score_thres_ = predictor["score_threshold"]? predictor["score_threshold"].as<float>(): 0.25f;
        iou_thres_ = predictor["iou_threshold"]? predictor["iou_threshold"].as<float>(): 0.65f;
        offset0_ =readVec4FromYaml(predictor, "offset0");
        transformer_ = createTransformer(config);

        yolov8_ =std::make_unique<YOLOv8>(engine_path);

        yolov8_->make_pipe(true);

        detect_pub_ =this->create_publisher<msg_det::msg::DetectRes>("/detect_res", 10);

        color_topic_ =
            this->declare_parameter<std::string>(
                "color_topic",
                "/camera/camera/color/image_raw"
            );

        depth_topic_ =
            this->declare_parameter<std::string>(
                "depth_topic",
                "/camera/camera/aligned_depth_to_color/image_raw"
            );

        show_image_ =
            this->declare_parameter<bool>(
                "show_image",
                true
            );

        color_sub_.subscribe(this, color_topic_);
        depth_sub_.subscribe(this, depth_topic_);

        sync_ = std::make_shared<Synchronizer>(
            SyncPolicy(10),
            color_sub_,
            depth_sub_
        );

        sync_->registerCallback(
            std::bind(
                &YoloDetectDepthNode::imageCallback,
                this,
                std::placeholders::_1,
                std::placeholders::_2
            )
        );

        if (show_image_) {
            cv::namedWindow(
                "detection_result",
                cv::WINDOW_AUTOSIZE
            );
        }

        const std::string timestamp =
            getCurrentTimeString();

        RCLCPP_INFO(
            this->get_logger(),
            "[%s] YOLO Detection Depth Node started.",
            timestamp.c_str()
        );

        RCLCPP_INFO(
            this->get_logger(),
            "[%s] Detection engine: %s",
            timestamp.c_str(),
            engine_path.c_str()
        );

        RCLCPP_INFO(
            this->get_logger(),
            "[%s] Color topic: %s",
            timestamp.c_str(),
            color_topic_.c_str()
        );

        RCLCPP_INFO(
            this->get_logger(),
            "[%s] Depth topic: %s",
            timestamp.c_str(),
            depth_topic_.c_str()
        );
    }

    ~YoloDetectDepthNode() override
    {
        if (show_image_) {
            cv::destroyWindow("detection_result");
        }
    }

private:
    void imageCallback(const ImageMsg::ConstSharedPtr& color_msg,const ImageMsg::ConstSharedPtr& depth_msg)
    {
        cv_bridge::CvImageConstPtr color_ptr;
        cv_bridge::CvImageConstPtr depth_ptr;

        try {
            color_ptr = cv_bridge::toCvShare(
                color_msg,
                sensor_msgs::image_encodings::BGR8
            );

            depth_ptr = cv_bridge::toCvShare(depth_msg);
        }
        catch (const cv_bridge::Exception& exception) {
            RCLCPP_ERROR(
                this->get_logger(),
                "cv_bridge exception: %s",
                exception.what()
            );
            return;
        }

        cv::Mat image = color_ptr->image.clone();
        const cv::Mat& depth = depth_ptr->image;

        if (image.empty() || depth.empty()) {
            RCLCPP_WARN(
                this->get_logger(),
                "Empty color or depth frame."
            );
            return;
        }

        std::vector<det::Object> objects;
        cv::Mat result_image;

        yolov8_->copy_from_Mat(
            image,
            image_size_
        );

        yolov8_->infer();

        yolov8_->postprocess(
            objects,
            score_thres_,
            iou_thres_,
            topk_,
            num_labels_
        );

        yolov8_->draw_objects(
            image,
            result_image,
            objects,
            DETECT_CLASS_NAMES,
            DETECT_COLORS
        );

        for (const auto& object : objects) {
            processObject(object, image, depth);
        }

        if (show_image_ && !result_image.empty()) {
            cv::imshow(
                "detection_result",
                result_image
            );
            cv::waitKey(1);
        }
    }

    void processObject(const det::Object& object,const cv::Mat& color,const cv::Mat& depth)
    {
        /*
         * 这里假设普通 YOLOv8 的 Object 定义为：
         *   cv::Rect_<float> rect;
         *   int label;
         *   float prob;
         *
         * 如果你的字段实际叫 box/class_id/conf，
         * 需要对应修改下面三处字段名。
         */
        const int cx_color =
            static_cast<int>(std::round(
                object.rect.x +
                object.rect.width * 0.5f
            ));

        const int cy_color =
            static_cast<int>(std::round(
                object.rect.y +
                object.rect.height * 0.5f
            ));

        if (cx_color < 0 ||
            cx_color >= color.cols ||
            cy_color < 0 ||
            cy_color >= color.rows)
        {
            RCLCPP_WARN(
                this->get_logger(),
                "Invalid detection center=[%d,%d], image=%dx%d",
                cx_color,
                cy_color,
                color.cols,
                color.rows
            );
            return;
        }

        const double scale_x =static_cast<double>(depth.cols) /static_cast<double>(color.cols);
        const double scale_y =static_cast<double>(depth.rows) /static_cast<double>(color.rows);
        const int cx_depth =static_cast<int>(std::round(cx_color * scale_x));
        const int cy_depth =static_cast<int>(std::round(cy_color * scale_y));
        const float depth_m =getDepthValueMeters(depth,cx_depth,cy_depth,2);

        if (!std::isfinite(depth_m) ||
            depth_m <= 0.0f)
        {
            RCLCPP_WARN(
                this->get_logger(),
                "Invalid depth at detection center=[%d,%d]",
                cx_depth,
                cy_depth
            );
            return;
        }

        const Eigen::Vector3d point_camera =pixelToCameraPoint(static_cast<double>(cx_color),static_cast<double>(cy_color),depth_m,fx_,fy_,cx0_,cy0_);

        constexpr double head_yaw = 0.0;
        constexpr double head_pitch = 0.0;

        const Eigen::Vector3d point_robot =
            transformer_->transformPoint(
                point_camera,
                head_yaw,
                head_pitch
            );

        const int type = object.label;

        const Eigen::Vector4d offset =getOffsetByType(type);

        const Eigen::Vector3d publish_point =point_robot + offset.head<3>();

        msg_det::msg::DetectRes message;

        message.type =static_cast<int16_t>(type);

        message.pos = {
            publish_point.x(),
            publish_point.y(),
            publish_point.z()
        };

        // 普通 Detection 暂时不计算法向量
        message.normal = {
            0.0,
            0.0,
            0.0
        };

        detect_pub_->publish(message);

        RCLCPP_INFO(
            this->get_logger(),
            "Detection published: "
            "class=%d, score=%.3f, "
            "pixel=[%d,%d], depth=%.3f, "
            "pos=[%.3f,%.3f,%.3f]",
            type,
            object.prob,
            cx_color,
            cy_color,
            depth_m,
            publish_point.x(),
            publish_point.y(),
            publish_point.z()
        );
    }

    Eigen::Vector4d getOffsetByType(int type) const
    {
        switch (type) {
            case 0:
                return offset0_;

            default:
                return Eigen::Vector4d::Zero();
        }
    }

private:
    std::unique_ptr<YOLOv8> yolov8_;

    message_filters::Subscriber<ImageMsg> color_sub_;
    message_filters::Subscriber<ImageMsg> depth_sub_;
    std::shared_ptr<Synchronizer> sync_;

    rclcpp::Publisher<
        msg_det::msg::DetectRes
    >::SharedPtr detect_pub_;

    std::unique_ptr<CamBaseTransformer>
        transformer_;

    Eigen::Vector4d offset0_ =
        Eigen::Vector4d::Zero();

    std::string color_topic_;
    std::string depth_topic_;

    cv::Size image_size_{640, 640};

    int num_labels_{80};
    int topk_{100};

    float score_thres_{0.25f};
    float iou_thres_{0.65f};

    bool show_image_{true};

    // 当前写死为彩色相机内参
    double fx_{909.701171875};
    double fy_{909.5648803710938};
    double cx0_{648.3807373046875};
    double cy0_{371.5822448730469};
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);

    try {
        const std::string config_path =ament_index_cpp::get_package_share_directory("yolo") +"/config/vision_config.yaml";
        const YAML::Node config =YAML::LoadFile(config_path);
        const YAML::Node detector =getRequiredYamlNode(config, "detector");
        const std::string detect_mode =getRequiredYamlNode(detector,"detect_mode").as<std::string>();

        RCLCPP_INFO(
            rclcpp::get_logger("yolo_main"), 
            "Selected detect mode: %s",
            detect_mode.c_str()
        );

        if (detect_mode == "obb") {
            const YAML::Node predictor = getRequiredYamlNode(detector,"obb_predictor");
            const std::string engine_path =getRequiredYamlNode(predictor,"engine_path").as<std::string>();
            auto node =std::make_shared<YoloObbDepthNode>(engine_path,config);
            rclcpp::spin(node);
        }
        else if (detect_mode == "detection") {
            const YAML::Node predictor =getRequiredYamlNode(detector,"detection_predictor");
            const std::string engine_path =getRequiredYamlNode(predictor,"engine_path").as<std::string>();
            auto node =std::make_shared<YoloDetectDepthNode>(engine_path,config);

            rclcpp::spin(node);
        }
        else {
            throw std::runtime_error(
                "Unsupported detector.detect_mode: `" +
                detect_mode +
                "`. Expected `obb` or `detection`."
            );
        }
    }
    catch (const std::exception& exception) {
        RCLCPP_FATAL(
            rclcpp::get_logger("yolo_main"),
            "Failed to start node: %s",
            exception.what()
        );

        rclcpp::shutdown();
        return 1;
    }

    rclcpp::shutdown();
    return 0;
}