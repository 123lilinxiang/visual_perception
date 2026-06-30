//
// ROS 2 YOLOv8 color + aligned depth inference node
//

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <cuda_runtime_api.h>

#include <opencv2/opencv.hpp>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <cv_bridge/cv_bridge.h>

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include <rmw/qos_profiles.h>

#include "yolov8-detect.h"
#include "cam2base.h"
#include "msg_det/msg/detect_res.hpp"
#include "std_msgs/msg/bool.hpp"
using namespace std::chrono_literals;

const std::vector<std::string> CLASS_NAMES = {
    "0","1"
};

const std::vector<std::vector<unsigned int>> COLORS = {
    {0, 114, 189},
    {0, 255, 1}
};

std::vector<std::vector<float>> result_array;

class YoloV8RosNode : public rclcpp::Node
{
public:
    using ImageMsg = sensor_msgs::msg::Image;

    using SyncPolicy =
        message_filters::sync_policies::ApproximateTime<
            ImageMsg,
            ImageMsg>;

    explicit YoloV8RosNode()
        : Node("yolov8_ros_node")
    {
        // =========================
        // ROS 参数
        // =========================

        engine_path_ = this->declare_parameter<std::string>("engine_path","");
        color_topic_ = this->declare_parameter<std::string>("color_topic","/camera/camera/color/image_raw");
        depth_topic_ = this->declare_parameter<std::string>("depth_topic","/camera/camera/aligned_depth_to_color/image_raw");
        inference_hz_ = this->declare_parameter<double>("inference_hz",10.0);
        show_result_ = this->declare_parameter<bool>("show_result",true);
        score_thres_ = this->declare_parameter<double>("score_threshold",0.25);
        iou_thres_ = this->declare_parameter<double>("iou_threshold",0.65);
        topk_ = this->declare_parameter<int>("topk",1);
        num_labels_ = this->declare_parameter<int>("num_labels",2);
        input_width_ = this->declare_parameter<int>("input_width",640);
        input_height_ = this->declare_parameter<int>("input_height",640);
        detect_res_pub_left =this->create_publisher<msg_det::msg::DetectRes>("/detect_res_left",10);
        detect_res_pub_right =this->create_publisher<msg_det::msg::DetectRes>("/detect_res_right",10);


        if (engine_path_.empty()) {
            throw std::runtime_error(
                "参数 engine_path 不能为空，请指定 TensorRT engine 文件路径"
            );
        }

        if (inference_hz_ <= 0.0) {
            RCLCPP_WARN(
                this->get_logger(),
                "inference_hz 必须大于 0，自动改为 10 Hz"
            );

            inference_hz_ = 10.0;
        }

        // =========================
        // CUDA 与 YOLO 初始化
        // =========================

        const cudaError_t cuda_result = cudaSetDevice(0);

        if (cuda_result != cudaSuccess) {
            throw std::runtime_error(
                std::string("cudaSetDevice(0) 失败：") +
                cudaGetErrorString(cuda_result)
            );
        }

        RCLCPP_INFO(
            this->get_logger(),
            "正在加载 TensorRT engine：%s",
            engine_path_.c_str()
        );

        yolov8_ = std::make_unique<YOLOv8>(engine_path_);
        yolov8_->make_pipe(true);

        RCLCPP_INFO(
            this->get_logger(),
            "TensorRT engine 加载完成"
        );

        // =========================
        // 创建彩色图和深度图订阅器
        // =========================

        color_sub_.subscribe(this,color_topic_,rmw_qos_profile_sensor_data);
        depth_sub_.subscribe(this,depth_topic_,rmw_qos_profile_sensor_data);

        // 缓存队列大小为 10
        sync_ = std::make_shared<
            message_filters::Synchronizer<SyncPolicy>
        >(
            SyncPolicy(10),
            color_sub_,
            depth_sub_
        );

        sync_->registerCallback(
            std::bind(
                &YoloV8RosNode::imageCallback,
                this,
                std::placeholders::_1,
                std::placeholders::_2
            )
        );

        // =========================
        // 10 Hz 推理定时器
        // =========================

        const auto timer_period =
            std::chrono::duration<double>(1.0 / inference_hz_);

        timer_ = this->create_wall_timer(std::chrono::duration_cast<std::chrono::nanoseconds>(timer_period),std::bind(&YoloV8RosNode::inferenceTimerCallback,this));

        if (show_result_) {
            cv::namedWindow("YOLOv8 Result",cv::WINDOW_NORMAL);
        }

        RCLCPP_INFO(this->get_logger(),"YOLOv8 ROS 2 节点启动成功");
        RCLCPP_INFO(this->get_logger(),"彩色图话题：%s",color_topic_.c_str());
        RCLCPP_INFO(this->get_logger(),"深度图话题：%s",depth_topic_.c_str());
        RCLCPP_INFO(this->get_logger(),"YOLO 推理频率：%.2f Hz",inference_hz_);
    }

    ~YoloV8RosNode() override
    {
        if (show_result_) {
            cv::destroyAllWindows();
        }
    }

private:

    void imageCallback(const ImageMsg::ConstSharedPtr& color_msg,const ImageMsg::ConstSharedPtr& depth_msg)
    {
        try {
            const cv_bridge::CvImageConstPtr color_ptr =
                cv_bridge::toCvShare(
                    color_msg,
                    sensor_msgs::image_encodings::BGR8
                );

            const cv_bridge::CvImageConstPtr depth_ptr =
                cv_bridge::toCvShare(
                    depth_msg,
                    depth_msg->encoding
                );

            if (color_ptr->image.empty()) {
                RCLCPP_WARN(
                    this->get_logger(),
                    "接收到的彩色图为空"
                );
                return;
            }

            if (depth_ptr->image.empty()) {
                RCLCPP_WARN(
                    this->get_logger(),
                    "接收到的深度图为空"
                );
                return;
            }

            {
                std::lock_guard<std::mutex> lock(frame_mutex_);

                latest_color_image_ =
                    color_ptr->image.clone();

                latest_depth_image_ =
                    depth_ptr->image.clone();

                has_frame_ = true;
                ++received_frame_sequence_;
            }
        }
        catch (const cv_bridge::Exception& e) {
            RCLCPP_ERROR(
                this->get_logger(),
                "cv_bridge 转换失败：%s",
                e.what()
            );
        }
        catch (const cv::Exception& e) {
            RCLCPP_ERROR(
                this->get_logger(),
                "OpenCV 处理失败：%s",
                e.what()
            );
        }
        catch (const std::exception& e) {
            RCLCPP_ERROR(
                this->get_logger(),
                "图像回调异常：%s",
                e.what()
            );
        }
    }

    void inferenceTimerCallback()
    {



        // 机械臂返回的判定

        detect_res_left_done_sub_ =
            this->create_subscription<std_msgs::msg::Bool>(
                "/detect_res_left_done",
                rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local(),
                [this](const std_msgs::msg::Bool::SharedPtr msg)
                {
                    left_robot_ready_.store(msg->data);

                    RCLCPP_INFO(
                        this->get_logger(),
                        "[RECEIVE] /detect_res_left_done: %s",
                        msg->data ? "true, keep publishing" : "false, stop publishing"
                    );
                }
            );
        
        detect_res_right_done_sub_ =
            this->create_subscription<std_msgs::msg::Bool>(
                "/detect_res_right_done",
                rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local(),
                [this](const std_msgs::msg::Bool::SharedPtr msg)
                {
                    right_robot_ready_.store(msg->data);

                    RCLCPP_INFO(
                        this->get_logger(),
                        "[RECEIVE] /detect_res_right_done: %s",
                        msg->data ? "true, keep publishing" : "false, stop publishing"
                    );
            }
        );

        cv::Mat color_image;
        cv::Mat depth_image;

        uint64_t current_frame_sequence = 0;

        {
            std::lock_guard<std::mutex> lock(frame_mutex_);

            if (!has_frame_) {
                return;
            }

            current_frame_sequence = received_frame_sequence_;

            // 不重复处理相同图像
            if (current_frame_sequence == processed_frame_sequence_) {
                return;
            }

            color_image = latest_color_image_.clone();
            depth_image = latest_depth_image_.clone();

            processed_frame_sequence_ = current_frame_sequence;
        }

        if (color_image.empty() || depth_image.empty()) {
            return;
        }

        try {
            std::vector<Object> objects;
            cv::Mat result_image;

            cv::Size input_size{
                input_width_,
                input_height_
            };

            // 将彩色图复制到 YOLO 输入缓冲区
            yolov8_->copy_from_Mat(
                color_image,
                input_size
            );

            const auto start =
                std::chrono::steady_clock::now();

            // TensorRT 推理
            yolov8_->infer();

            const auto infer_end =
                std::chrono::steady_clock::now();

            // 后处理
            yolov8_->postprocess(
                objects,
                static_cast<float>(score_thres_),
                static_cast<float>(iou_thres_),
                topk_,
                num_labels_
            );

            // 绘制检测框
            yolov8_->draw_objects(
                color_image,
                result_image,
                objects,
                CLASS_NAMES,
                COLORS,
                result_array
            );

            appendCenterDepthToResults(color_image,depth_image,result_array,5);
            appendBaseCoordinates(color_image,result_array,detect_res_pub_left,detect_res_pub_right,left_robot_ready_,right_robot_ready_);
            for (const auto& result : result_array) 
            {
                if (result.size() < 9) {
                    continue;
                }

                const int label =
                    static_cast<int>(result[4]);

                const int center_x =
                    static_cast<int>(result[6]);

                const int center_y =
                    static_cast<int>(result[7]);

                const float depth_m =
                    result[8];

                if (!std::isfinite(depth_m)) {
                    std::cout
                        << "目标深度无效"
                        << std::endl;

                    continue;
                }

                std::cout
                    << "label=" << label
                    << ", center=("
                    << center_x << ", "
                    << center_y << ")"
                    << ", depth="
                    << depth_m << " m"
                    << std::endl;
            }

            const auto process_end =
                std::chrono::steady_clock::now();

            const double infer_time_ms =
                std::chrono::duration<double, std::milli>(
                    infer_end - start
                ).count();

            const double total_time_ms =
                std::chrono::duration<double, std::milli>(
                    process_end - start
                ).count();

            RCLCPP_INFO(
                this->get_logger(),
                "frame=%lu, objects=%zu, infer=%.3f ms, total=%.3f ms",
                static_cast<unsigned long>(current_frame_sequence),
                objects.size(),
                infer_time_ms,
                total_time_ms
            );

            if (show_result_ && !result_image.empty()) {
                // 在左上角显示推理耗时
                const std::string time_text =
                    "Infer: " +
                    cv::format("%.2f", infer_time_ms) +
                    " ms";

                cv::putText(
                    result_image,
                    time_text,
                    cv::Point(20, 35),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.8,
                    cv::Scalar(0, 255, 0),
                    2,
                    cv::LINE_AA
                );

                cv::imshow(
                    "YOLOv8 Result",
                    result_image
                );

                const int key = cv::waitKey(1);

                if (key == 'q' || key == 'Q' || key == 27) {
                    RCLCPP_INFO(
                        this->get_logger(),
                        "收到退出按键，关闭节点"
                    );

                    rclcpp::shutdown();
                }
            }
        }
        catch (const cv::Exception& e) {
            RCLCPP_ERROR(
                this->get_logger(),
                "OpenCV 推理处理异常：%s",
                e.what()
            );
        }
        catch (const std::exception& e) {
            RCLCPP_ERROR(
                this->get_logger(),
                "YOLO 推理异常：%s",
                e.what()
            );
        }
    }

private:
    // =========================
    // 参数
    // =========================

    std::string engine_path_;
    std::string color_topic_;
    std::string depth_topic_;

    double inference_hz_{10.0};
    double score_thres_{0.65};
    double iou_thres_{0.65};

    int topk_{1};
    int num_labels_{2};
    int input_width_{640};
    int input_height_{640};

    bool show_result_{true};

    // =========================
    // YOLO
    // =========================

    std::unique_ptr<YOLOv8> yolov8_;

    // =========================
    // ROS 图像同步
    // =========================

    message_filters::Subscriber<ImageMsg> color_sub_;
    message_filters::Subscriber<ImageMsg> depth_sub_;
    
    std::shared_ptr<message_filters::Synchronizer<SyncPolicy>> sync_;

    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::Publisher<msg_det::msg::DetectRes>::SharedPtr detect_res_pub_right;
    rclcpp::Publisher<msg_det::msg::DetectRes>::SharedPtr detect_res_pub_left;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr detect_res_left_done_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr detect_res_right_done_sub_;
    std::atomic_bool left_robot_ready_{true};
    std::atomic_bool right_robot_ready_{true};
    // =========================
    // 图像缓存
    // =========================

    std::mutex frame_mutex_;

    cv::Mat latest_color_image_;
    cv::Mat latest_depth_image_;

    builtin_interfaces::msg::Time latest_color_stamp_;
    builtin_interfaces::msg::Time latest_depth_stamp_;

    std::string latest_depth_encoding_;

    uint64_t received_frame_sequence_{0};
    uint64_t processed_frame_sequence_{0};

    bool has_frame_{false};
    bool first_frame_received_{false};
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);

    try {
        const auto node =
            std::make_shared<YoloV8RosNode>();

        rclcpp::spin(node);
    }
    catch (const std::exception& e) {
        fprintf(
            stderr,
            "YOLOv8 ROS 2 节点启动失败：%s\n",
            e.what()
        );

        rclcpp::shutdown();

        return -1;
    }

    rclcpp::shutdown();

    return 0;
}


