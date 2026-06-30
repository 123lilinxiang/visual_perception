#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
#include <opencv2/opencv.hpp>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include "msg_det/msg/detect_res.hpp"

// ============================================================
// 相机内参，对应 1280 × 720
// ============================================================
static constexpr double CAMERA_FX = 608.5936279296875;
static constexpr double CAMERA_FY = 608.7183227539062;
static constexpr double CAMERA_CX = 640.227783203125;
static constexpr double CAMERA_CY = 360.9166259765625;

static constexpr int INTRINSICS_WIDTH  = 1280;
static constexpr int INTRINSICS_HEIGHT = 720;

// ============================================================
// 相机畸变参数
// ============================================================
static const cv::Mat DIST_COEFFS = (
    cv::Mat_<double>(1, 5)
        << -0.02925739251077175,
           0.03408520296216011,
           0.00013774150284007192,
          -0.000108235894003883,
          -0.012049867771565914
);

// 原始 image_raw 使用 true
// 已经去畸变的 image_rect 使用 false
static constexpr bool USE_DISTORTION = true;

// ============================================================
// Eye-to-hand 矩阵
//
// 含义：相机坐标系 -> 机器人基座坐标系
// P_base = T_BASE_CAMERA * P_camera
// ============================================================
static const cv::Matx44d T_BASE_CAMERA_left(
    -0.002185488,  0.827000094,  0.562197534, -0.192574591,
     0.019485470,  0.562127356, -0.826821113, -0.125032582,
    -0.999807752,  0.009147676, -0.017343008, -0.061427916,
     0.0,          0.0,          0.0,          1.0
);

static const cv::Matx44d T_BASE_CAMERA_right(
    0.007346992874, 0.850601758020 ,0.525759137771, -0.195756253017,
    -0.032678379472, -0.525288277356 ,0.850296624236 ,0.111305459988,
    0.999438915197, -0.023428079854, 0.023936997797, -0.090821521949,
    0.0,          0.0,          0.0,          1.0
);

void appendCenterDepthToResults(const cv::Mat& color_image,const cv::Mat& depth_image,std::vector<std::vector<float>>& result_array,int radius );
cv::Matx33d getScaledCameraMatrix(int current_width,int current_height);
cv::Matx33d getScaledCameraMatrix(int current_width,int current_height);
bool pixelToCameraPoint(double u,double v,double depth_m,const cv::Matx33d& camera_matrix,cv::Vec3d& point_camera);
bool cameraPointToBase(const cv::Vec3d& point_camera,cv::Vec3d& point_base,bool is_left_side);
// void appendBaseCoordinates(const cv::Mat& color_image,std::vector<std::vector<float>>& result_array,
//     const rclcpp::Publisher<msg_det::msg::DetectRes>::SharedPtr& detect_res_pub);
void appendBaseCoordinates(
    const cv::Mat& color_image,
    std::vector<std::vector<float>>& result_array,
    const rclcpp::Publisher<msg_det::msg::DetectRes>::SharedPtr& detect_res_pub_left,
    const rclcpp::Publisher<msg_det::msg::DetectRes>::SharedPtr& detect_res_pub_right,
    std::atomic_bool& left_robot_ready_,std::atomic_bool& right_robot_ready_);