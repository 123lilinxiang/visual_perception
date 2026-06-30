
#include "cam2base.h"


bool getDepthInRadius(const cv::Mat& depth_image,int center_x,int center_y,int radius,double& depth_m)
    {
        depth_m = 0.0;

        if (depth_image.empty()) {
            std::cerr << "深度图为空" << std::endl;
            return false;
        }

        if (depth_image.channels() != 1) {
            std::cerr << "深度图必须是单通道图像" << std::endl;
            return false;
        }

        if (radius < 0) {
            std::cerr << "radius 不能小于 0" << std::endl;
            return false;
        }

        if (center_x < 0 ||
            center_x >= depth_image.cols ||
            center_y < 0 ||
            center_y >= depth_image.rows) {

            std::cerr
                << "中心点超出深度图范围："
                << "x=" << center_x
                << ", y=" << center_y
                << ", depth_size="
                << depth_image.cols << "x"
                << depth_image.rows
                << std::endl;

            return false;
        }

        std::vector<double> valid_depths;

        // 遍历以中心点为圆心、radius 为半径的圆形区域
        for (int offset_y = -radius;
            offset_y <= radius;
            ++offset_y) {

            for (int offset_x = -radius;
                offset_x <= radius;
                ++offset_x) {

                // 只读取圆内的像素
                if (offset_x * offset_x +
                        offset_y * offset_y >
                    radius * radius) {
                    continue;
                }

                const int x = center_x + offset_x;
                const int y = center_y + offset_y;

                // 边界检查
                if (x < 0 ||
                    x >= depth_image.cols ||
                    y < 0 ||
                    y >= depth_image.rows) {
                    continue;
                }

                double current_depth_m = 0.0;

                switch (depth_image.type()) {
                    case CV_16UC1: {
                        // uint16 深度一般单位为毫米
                        const std::uint16_t raw_depth =
                            depth_image.at<std::uint16_t>(y, x);

                        if (raw_depth == 0) {
                            continue;
                        }

                        current_depth_m =static_cast<double>(raw_depth) * 0.001;

                        break;
                    }

                    case CV_32FC1: {
                        // float 深度一般已经是米
                        const float raw_depth =
                            depth_image.at<float>(y, x);

                        if (!std::isfinite(raw_depth) ||
                            raw_depth <= 0.0f) {
                            continue;
                        }

                        current_depth_m =static_cast<double>(raw_depth);

                        break;
                    }

                    case CV_64FC1: {
                        const double raw_depth =
                            depth_image.at<double>(y, x);

                        if (!std::isfinite(raw_depth) ||
                            raw_depth <= 0.0) {
                            continue;
                        }

                        current_depth_m = raw_depth;

                        break;
                    }

                    default: {
                        std::cerr
                            << "不支持的深度图类型："
                            << depth_image.type()
                            << std::endl;

                        return false;
                    }
                }

                if (std::isfinite(current_depth_m) &&
                    current_depth_m > 0.0) {
                    valid_depths.push_back(current_depth_m-0.05);
                }
            }
        }

        if (valid_depths.empty()) {
            std::cerr
                << "中心点周围没有有效深度："
                << "center=("
                << center_x << ", "
                << center_y << "), radius="
                << radius
                << std::endl;

            return false;
        }

        // 排序并取中位数
        std::sort(
            valid_depths.begin(),
            valid_depths.end()
        );

        const std::size_t count =
            valid_depths.size();

        if (count % 2 == 1) {
            depth_m =
                valid_depths[count / 2];
        } else {
            depth_m =
                (
                    valid_depths[count / 2 - 1] +
                    valid_depths[count / 2]
                ) / 2.0;
        }

        return true;
    }


bool estimateNormalCameraByDepthPatch(
    const cv::Mat& depth_image,
    double center_u,
    double center_v,
    const cv::Matx33d& camera_matrix,
    cv::Vec3d& normal_camera,
    int sample_radius = 10,
    int depth_radius = 3)
{
    const int u = static_cast<int>(std::round(center_u));
    const int v = static_cast<int>(std::round(center_v));

    if (u - sample_radius < 0 ||
        u + sample_radius >= depth_image.cols ||
        v - sample_radius < 0 ||
        v + sample_radius >= depth_image.rows) {
        return false;
    }

    auto getDepthAround = [&](int px, int py, double& depth_m) -> bool {
        px = std::clamp(px, 0, depth_image.cols - 1);
        py = std::clamp(py, 0, depth_image.rows - 1);

        return getDepthInRadius(
            depth_image,
            px,
            py,
            depth_radius,
            depth_m
        );
    };

    double d_left = 0.0;
    double d_right = 0.0;
    double d_up = 0.0;
    double d_down = 0.0;
    double d_center = 0.0;

    if (!getDepthAround(u - sample_radius, v, d_left)) {
        return false;
    }

    if (!getDepthAround(u + sample_radius, v, d_right)) {
        return false;
    }

    if (!getDepthAround(u, v - sample_radius, d_up)) {
        return false;
    }

    if (!getDepthAround(u, v + sample_radius, d_down)) {
        return false;
    }

    if (!getDepthAround(u, v, d_center)) {
        return false;
    }

    cv::Vec3d p_left;
    cv::Vec3d p_right;
    cv::Vec3d p_up;
    cv::Vec3d p_down;
    cv::Vec3d p_center;

    if (!pixelToCameraPoint(
            u - sample_radius,
            v,
            d_left,
            camera_matrix,
            p_left)) {
        return false;
    }

    if (!pixelToCameraPoint(
            u + sample_radius,
            v,
            d_right,
            camera_matrix,
            p_right)) {
        return false;
    }

    if (!pixelToCameraPoint(
            u,
            v - sample_radius,
            d_up,
            camera_matrix,
            p_up)) {
        return false;
    }

    if (!pixelToCameraPoint(
            u,
            v + sample_radius,
            d_down,
            camera_matrix,
            p_down)) {
        return false;
    }

    if (!pixelToCameraPoint(
            u,
            v,
            d_center,
            camera_matrix,
            p_center)) {
        return false;
    }

    const cv::Vec3d vx = p_right - p_left;
    const cv::Vec3d vy = p_down - p_up;

    normal_camera = vx.cross(vy);

    const double len = cv::norm(normal_camera);

    if (!std::isfinite(len) || len < 1e-9) {
        return false;
    }

    normal_camera /= len;

    /*
     * 让法向量朝向相机。
     * 相机坐标里，p_center 是相机指向物体。
     * 如果 normal 和 p_center 同向，说明 normal 背向相机，需要反过来。
     */
    if (normal_camera.dot(p_center) > 0.0) {
        normal_camera = -normal_camera;
    }

    return true;
}


void appendCenterDepthToResults(
    const cv::Mat& color_image,
    const cv::Mat& depth_image,
    std::vector<std::vector<float>>& result_array,
    int radius = 5)
{
    if (color_image.empty()) {
        std::cerr << "color_image 为空" << std::endl;
        return;
    }

    if (depth_image.empty()) {
        std::cerr << "depth_image 为空" << std::endl;
        return;
    }

    if (color_image.cols != depth_image.cols ||
        color_image.rows != depth_image.rows) {

        std::cerr
            << "彩色图与深度图尺寸不一致，不能直接使用检测框中心读取深度"
            << std::endl;

        return;
    }

    const cv::Matx33d camera_matrix =
        getScaledCameraMatrix(
            color_image.cols,
            color_image.rows
        );

    const float nan =
        std::numeric_limits<float>::quiet_NaN();

    for (std::size_t i = 0; i < result_array.size(); ++i) {

        auto& result = result_array[i];

        if (result.size() < 6) {
            std::cerr
                << "第 " << i
                << " 个 result_array 数据不足"
                << std::endl;

            continue;
        }

        /*
         * 防止重复调用不断追加。
         * 保留原始检测框 6 个数据。
         */
        result.resize(6);

        const float x = result[0];
        const float y = result[1];
        const float width = result[2];
        const float height = result[3];

        const int label =
            static_cast<int>(result[4]);

        const float probability = result[5];

        int center_x =
            static_cast<int>(
                std::round(x + width * 0.5f)
            );

        int center_y =
            static_cast<int>(
                std::round(y + height * 0.5f)
            );

        center_x = std::clamp(
            center_x,
            0,
            depth_image.cols - 1
        );

        center_y = std::clamp(
            center_y,
            0,
            depth_image.rows - 1
        );

        double depth_m = 0.0;

        const bool depth_valid =
            getDepthInRadius(
                depth_image,
                center_x,
                center_y,
                radius,
                depth_m
            );

        result.push_back(
            static_cast<float>(center_x)
        );

        result.push_back(
            static_cast<float>(center_y)
        );

        if (depth_valid) {
            result.push_back(
                static_cast<float>(depth_m)
            );
        } else {
            result.push_back(nan);
        }

        /*
         * 估计物体表面法向量 normal_camera。
         */
        cv::Vec3d normal_camera;

        const bool normal_valid =
            depth_valid &&
            estimateNormalCameraByDepthPatch(
                depth_image,
                center_x,
                center_y,
                camera_matrix,
                normal_camera,
                10,   // sample_radius，中心周围取样距离
                3     // depth_radius，每个采样点附近取深度
            );

        if (normal_valid) {
            result.push_back(
                static_cast<float>(normal_camera[0])
            );

            result.push_back(
                static_cast<float>(normal_camera[1])
            );

            result.push_back(
                static_cast<float>(normal_camera[2])
            );

            std::cout
                << "目标 " << i
                << ", label=" << label
                << ", probability=" << probability
                << ", center=(" << center_x << ", " << center_y << ")"
                << ", depth=" << depth_m << " m"
                << ", normal_camera=["
                << normal_camera[0] << ", "
                << normal_camera[1] << ", "
                << normal_camera[2] << "]"
                << std::endl;
        }
        else {
            result.push_back(nan);
            result.push_back(nan);
            result.push_back(nan);

            std::cout
                << "目标 " << i
                << ", label=" << label
                << ", center=(" << center_x << ", " << center_y << ")"
                << ", depth=" << depth_m
                << ", 法向量估计失败"
                << std::endl;
        }
    }
}

cv::Vec3d cameraVectorToBase(
    const cv::Vec3d& v_camera,
    bool is_left_side)
{
    const auto& T =
        is_left_side
            ? T_BASE_CAMERA_left
            : T_BASE_CAMERA_right;

    cv::Vec3d v_base;

    v_base[0] =
        T(0, 0) * v_camera[0] +
        T(0, 1) * v_camera[1] +
        T(0, 2) * v_camera[2];

    v_base[1] =
        T(1, 0) * v_camera[0] +
        T(1, 1) * v_camera[1] +
        T(1, 2) * v_camera[2];

    v_base[2] =
        T(2, 0) * v_camera[0] +
        T(2, 1) * v_camera[1] +
        T(2, 2) * v_camera[2];

    const double len = cv::norm(v_base);

    if (len > 1e-9) {
        v_base /= len;
    }

    return v_base;
}


cv::Vec3d baseVectorToArmVector(
    const cv::Vec3d& v_base,
    bool is_left_side)
{
    cv::Vec3d v_arm;

    if (is_left_side) {
        /*
         * 左臂：
         * end_x = -base_y
         * end_y =  base_z
         * end_z = -base_x
         */
        v_arm[0] = -v_base[1];
        v_arm[1] =  v_base[2];
        v_arm[2] = -v_base[0];
    }
    else {
        /*
         * 右臂：
         * end_x =  base_y
         * end_y = -base_z
         * end_z = -base_x
         */
        v_arm[0] =  v_base[1];
        v_arm[1] = -v_base[2];
        v_arm[2] = -v_base[0];
    }

    const double len = cv::norm(v_arm);

    if (len > 1e-9) {
        v_arm /= len;
    }

    return v_arm;
}

std::array<double, 3> calcABCFromNormalInArm(
    const cv::Vec3d& normal_arm,
    double rx_fixed_rad)
{
    cv::Vec3d n = normal_arm;

    const double len = cv::norm(n);

    if (!std::isfinite(len) || len < 1e-9) {
        return {
            rx_fixed_rad,
            0.0,
            0.0
        };
    }

    n /= len;

    const double nx = n[0];
    const double ny = n[1];
    const double nz = n[2];

    /*
     * 对应你的 ABC 顺序：
     * R = Rz(C) * Ry(B) * Rx(A)
     *
     * TCP X轴方向为：
     * [cos(C)cos(B), sin(C)cos(B), -sin(B)]
     *
     * 所以：
     * B = asin(-nz)
     * C = atan2(ny, nx)
     */
    double v = -nz;
    v = std::clamp(v, -1.0, 1.0);

    const double ry = std::asin(v);
    const double rz = std::atan2(ny, nx);

    const double rx = rx_fixed_rad;

    return {
        rx,
        ry,
        rz
    };
}

cv::Matx33d getScaledCameraMatrix(
    int current_width,
    int current_height)
{
    const double scale_x =
        static_cast<double>(current_width) /
        static_cast<double>(INTRINSICS_WIDTH);

    const double scale_y =
        static_cast<double>(current_height) /
        static_cast<double>(INTRINSICS_HEIGHT);

    return cv::Matx33d(
        CAMERA_FX * scale_x,
        0.0,
        CAMERA_CX * scale_x,
        0.0,
        CAMERA_FY * scale_y,
        CAMERA_CY * scale_y,
        0.0,
        0.0,
        1.0
    );
}


bool pixelToCameraPoint(
    double u,
    double v,
    double depth_m,
    const cv::Matx33d& camera_matrix,
    cv::Vec3d& point_camera)
{
    if (!std::isfinite(depth_m) || depth_m <= 0.0) {
        return false;
    }

    if (USE_DISTORTION) {
        // 输入一个像素点
        std::vector<cv::Point2d> pixel_points{
            cv::Point2d(u, v)
        };

        std::vector<cv::Point2d> normalized_points;

        // 输出是去畸变后的归一化坐标
        cv::undistortPoints(
            pixel_points,
            normalized_points,
            cv::Mat(camera_matrix),
            DIST_COEFFS
        );

        if (normalized_points.empty()) {
            return false;
        }

        const double normalized_x =
            normalized_points[0].x;

        const double normalized_y =
            normalized_points[0].y;

        point_camera[0] =
            normalized_x * depth_m;

        point_camera[1] =
            normalized_y * depth_m;

        point_camera[2] =
            depth_m;
    }
    else {
        const double fx = camera_matrix(0, 0);
        const double fy = camera_matrix(1, 1);
        const double cx = camera_matrix(0, 2);
        const double cy = camera_matrix(1, 2);

        point_camera[0] =
            (u - cx) * depth_m / fx;

        point_camera[1] =
            (v - cy) * depth_m / fy;

        point_camera[2] =
            depth_m;
    }

    return std::isfinite(point_camera[0]) &&
           std::isfinite(point_camera[1]) &&
           std::isfinite(point_camera[2]);
}

bool cameraPointToBase(
    const cv::Vec3d& point_camera,
    cv::Vec3d& point_base,
    bool is_left_side)
{
    const cv::Vec4d point_camera_h(
        point_camera[0],
        point_camera[1],
        point_camera[2],
        1.0
    );

    cv::Vec4d point_base_h;

    if (is_left_side)
    {
        point_base_h = T_BASE_CAMERA_left * point_camera_h;
    }
    else
    {
        point_base_h = T_BASE_CAMERA_right * point_camera_h;
    }

    const double w = point_base_h[3];

    if (!std::isfinite(w) || std::abs(w) < 1e-12) {
        return false;
    }

    point_base[0] = point_base_h[0] / w;
    point_base[1] = point_base_h[1] / w;
    point_base[2] = point_base_h[2] / w;

    return std::isfinite(point_base[0]) &&
           std::isfinite(point_base[1]) &&
           std::isfinite(point_base[2]);
}


void appendBaseCoordinates(
    const cv::Mat& color_image,
    std::vector<std::vector<float>>& result_array,
    const rclcpp::Publisher<msg_det::msg::DetectRes>::SharedPtr& detect_res_pub_left,
    const rclcpp::Publisher<msg_det::msg::DetectRes>::SharedPtr& detect_res_pub_right,
    std::atomic_bool& left_robot_ready,
    std::atomic_bool& right_robot_ready)
{
    if (color_image.empty()) {
        std::cerr << "[ERROR] color_image 为空" << std::endl;
        return;
    }

    const cv::Matx33d camera_matrix =
        getScaledCameraMatrix(
            color_image.cols,
            color_image.rows
        );

    using Clock = std::chrono::steady_clock;

    struct ReadyDelayState
    {
        bool last_ready{false};
        Clock::time_point ready_since{};
    };

    static std::mutex ready_delay_mutex;
    static ReadyDelayState left_ready_delay_state;
    static ReadyDelayState right_ready_delay_state;

    constexpr auto READY_DELAY = std::chrono::seconds(2);

    auto canPublishAfterReadyDelay =
        [&](bool send_right,
            const std::atomic_bool& ready_flag,
            const char* target_arm) -> bool
    {
        const bool ready =
            ready_flag.load(std::memory_order_relaxed);

        const auto now = Clock::now();

        std::lock_guard<std::mutex> lock(ready_delay_mutex);

        ReadyDelayState& state =
            send_right
                ? right_ready_delay_state
                : left_ready_delay_state;

        if (!ready) {
            state.last_ready = false;
            state.ready_since = Clock::time_point{};

            std::cout
                << "[SKIP] " << target_arm
                << " robot moving, ready=false, stop publish"
                << std::endl;

            return false;
        }

        if (!state.last_ready) {
            state.last_ready = true;
            state.ready_since = now;

            std::cout
                << "[READY TRUE] " << target_arm
                << " robot ready=true, wait 2s before publish"
                << std::endl;

            return false;
        }

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - state.ready_since
            );

        if (elapsed < READY_DELAY) {
            const auto remain =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    READY_DELAY - elapsed
                );

            std::cout
                << "[WAIT] " << target_arm
                << " ready delay not reached, remain "
                << remain.count()
                << " ms"
                << std::endl;

            return false;
        }

        return true;
    };

    const float nan =
        std::numeric_limits<float>::quiet_NaN();

    constexpr double PI = 3.14159265358979323846;

    for (std::size_t i = 0; i < result_array.size(); ++i) {

        auto& result = result_array[i];

        /*
         * appendCenterDepthToResults 后应该是：
         *
         * result[0]  = bbox x
         * result[1]  = bbox y
         * result[2]  = bbox width
         * result[3]  = bbox height
         * result[4]  = label
         * result[5]  = probability
         * result[6]  = center_u
         * result[7]  = center_v
         * result[8]  = depth_m
         * result[9]  = normal_camera_x
         * result[10] = normal_camera_y
         * result[11] = normal_camera_z
         */
        if (result.size() < 12) {
            std::cerr
                << "[WARN] 第 " << i
                << " 个目标没有完整中心点、深度或法向量信息，size="
                << result.size()
                << std::endl;

            continue;
        }

        /*
         * 注意：这里一定是 resize(12)，不能 resize(9)。
         * 否则 normal_camera 会被删掉。
         */
        result.resize(12);

        const int label =
            static_cast<int>(result[4]);

        const double center_u =
            static_cast<double>(result[6]);

        const double center_v =
            static_cast<double>(result[7]);

        const double depth_m =
            static_cast<double>(result[8]);

        const cv::Vec3d normal_camera(
            static_cast<double>(result[9]),
            static_cast<double>(result[10]),
            static_cast<double>(result[11])
        );

        const bool normal_valid =
            std::isfinite(normal_camera[0]) &&
            std::isfinite(normal_camera[1]) &&
            std::isfinite(normal_camera[2]) &&
            cv::norm(normal_camera) > 1e-9;

        /*
         * 根据像素坐标判断目标在画面左侧还是右侧。
         * 左侧发左臂，右侧发右臂。
         */
        const bool is_left_side =
            center_u < color_image.cols * 0.5;

        if (center_u < 0.0 ||
            center_u >= color_image.cols ||
            center_v < 0.0 ||
            center_v >= color_image.rows) {

            std::cerr
                << "[WARN] 目标 " << i
                << " 中心坐标越界：u=" << center_u
                << ", v=" << center_v
                << std::endl;

            result.insert(result.end(), {
                nan, nan, nan,   // point_camera
                nan, nan, nan,   // point_base
                nan, nan, nan    // rx ry rz
            });

            continue;
        }

        if (!std::isfinite(depth_m) || depth_m <= 0.0) {

            std::cerr
                << "[WARN] 目标 " << i
                << " 深度无效"
                << std::endl;

            result.insert(result.end(), {
                nan, nan, nan,
                nan, nan, nan,
                nan, nan, nan
            });

            continue;
        }

        /*
         * 像素坐标 -> 相机坐标
         */
        cv::Vec3d point_camera;

        if (!pixelToCameraPoint(
                center_u,
                center_v,
                depth_m,
                camera_matrix,
                point_camera)) {

            std::cerr
                << "[WARN] 目标 " << i
                << " 转换相机坐标失败"
                << std::endl;

            result.insert(result.end(), {
                nan, nan, nan,
                nan, nan, nan,
                nan, nan, nan
            });

            continue;
        }

        /*
         * 相机坐标 -> 机器人基座坐标
         */
        cv::Vec3d point_base;

        if (!cameraPointToBase(
                point_camera,
                point_base,
                is_left_side)) {

            std::cerr
                << "[WARN] 目标 " << i
                << " 转换机器人基座坐标失败"
                << std::endl;

            result.insert(result.end(), {
                static_cast<float>(point_camera[0]),
                static_cast<float>(point_camera[1]),
                static_cast<float>(point_camera[2]),
                nan, nan, nan,
                nan, nan, nan
            });

            continue;
        }

        /*
         * 追加相机坐标
         */
        result.push_back(
            static_cast<float>(point_camera[0])
        );

        result.push_back(
            static_cast<float>(point_camera[1])
        );

        result.push_back(
            static_cast<float>(point_camera[2])
        );

        /*
         * 追加机器人基座坐标
         */
        result.push_back(
            static_cast<float>(point_base[0])
        );

        result.push_back(
            static_cast<float>(point_base[1])
        );

        result.push_back(
            static_cast<float>(point_base[2])
        );

        /*
         * 计算最终要发送给机械臂的末端坐标。
         *
         * 这里沿用你原来的左右臂坐标映射。
         */
        double end_x = 0.0;
        double end_y = 0.0;
        double end_z = 0.0;

        // if (is_left_side) {
        //     end_x = -point_base[1];
        //     end_y =  point_base[2];
        //     end_z = -point_base[0];
        // }
        // else {
        //     end_x =  point_base[1];
        //     end_y = -point_base[2];
        //     end_z = -point_base[0];
        // }

        /*
         * 默认姿态。
         * 如果 normal 无效，就退回默认姿态。
         */
        double rx = 90.0 * PI / 180.0;
        double ry = 0.0;
        double rz = 90.0 * PI / 180.0;

        cv::Vec3d normal_base(0.0, 0.0, 0.0);
        cv::Vec3d normal_arm(0.0, 0.0, 0.0);

        if (normal_valid) {

            /*
             * 法向量是方向，不是点。
             * 所以只用旋转矩阵，不加平移。
             */
            normal_base =
                cameraVectorToBase(
                    normal_camera,
                    is_left_side
                );

            normal_arm =
                baseVectorToArmVector(
                    normal_base,
                    is_left_side
                );

            /*
             * 如果发现机械臂末端方向反了，
             * 就打开下面这一句。
             *
             * normal_arm = -normal_arm;
             */

            const auto abc =
                calcABCFromNormalInArm(
                    normal_arm,
                    rx
                );

            rx = abc[0];
            ry = abc[1];
            rz = -abc[2];
        }
        else {
            std::cout
                << "[WARN] normal_camera 无效，使用默认姿态 "
                << "abc=[90, 0, 90] deg"
                << std::endl;
        }

        /*
         * 追加最终角度
         */
        result.push_back(static_cast<float>(rx));
        result.push_back(static_cast<float>(ry));
        result.push_back(static_cast<float>(rz));

        /*
         * 根据目标所在图像左右区域选择机械臂。
         */
        const bool send_right =
            !is_left_side;

        auto target_pub =
            send_right
                ? detect_res_pub_right
                : detect_res_pub_left;

        std::atomic_bool* target_ready =
            send_right
                ? &right_robot_ready
                : &left_robot_ready;

        const char* target_arm =
            send_right ? "right" : "left";

        const char* target_topic =
            send_right
                ? "/detect_res_right"
                : "/detect_res_left";

        /*
         * 发布 DetectRes 消息。
         * 现在 pos 是 6 个数：
         * [x, y, z, rx, ry, rz]
         */
        if (target_pub) {

            if (!canPublishAfterReadyDelay(
                    send_right,
                    *target_ready,
                    target_arm)) {
                continue;
            }

            msg_det::msg::DetectRes msg;

            msg.type =
                static_cast<int16_t>(label);

            msg.pos.clear();

            msg.pos.push_back(point_base[0]);
            msg.pos.push_back(point_base[1]);
            msg.pos.push_back(point_base[2]);

            msg.pos.push_back(rx);
            msg.pos.push_back(ry);
            msg.pos.push_back(rz);

            target_pub->publish(msg);

            std::cout
                << "[PUBLISH] " << target_topic << ": "
                << "type=" << msg.type
                << ", pos=["
                << msg.pos[0] << ", "
                << msg.pos[1] << ", "
                << msg.pos[2] << ", "
                << msg.pos[3] << ", "
                << msg.pos[4] << ", "
                << msg.pos[5] << "]"
                << ", abc_deg=["
                << rx * 180.0 / PI << ", "
                << ry * 180.0 / PI << ", "
                << rz * 180.0 / PI << "]"
                << ", target_arm=" << target_arm
                << ", robot_ready=true"
                << std::endl;
        }
        else {
            std::cerr
                << "[WARN] " << target_arm
                << " publisher 为空，无法发布"
                << std::endl;
        }

        std::cout
            << "\n目标 " << i
            << ", label=" << label
            << std::endl;

        std::cout
            << "中心像素：u=" << center_u
            << ", v=" << center_v
            << std::endl;

        std::cout
            << "深度：" << depth_m << " m"
            << std::endl;

        std::cout
            << "相机坐标："
            << "x=" << point_camera[0]
            << ", y=" << point_camera[1]
            << ", z=" << point_camera[2]
            << " m"
            << std::endl;

        std::cout
            << "基座坐标："
            << "x=" << point_base[0]
            << ", y=" << point_base[1]
            << ", z=" << point_base[2]
            << " m"
            << std::endl;

        std::cout
            << "normal_camera："
            << "x=" << normal_camera[0]
            << ", y=" << normal_camera[1]
            << ", z=" << normal_camera[2]
            << std::endl;

        std::cout
            << "normal_base："
            << "x=" << normal_base[0]
            << ", y=" << normal_base[1]
            << ", z=" << normal_base[2]
            << std::endl;

        std::cout
            << "normal_arm："
            << "x=" << normal_arm[0]
            << ", y=" << normal_arm[1]
            << ", z=" << normal_arm[2]
            << std::endl;

        std::cout
            << "最终末端位姿："
            << "x=" << point_base[0]
            << ", y=" << point_base[1]
            << ", z=" << point_base[2]
            << ", rx=" << rx
            << ", ry=" << ry
            << ", rz=" << rz
            << " rad, abc_deg=["
            << rx * 180.0 / PI << ", "
            << ry * 180.0 / PI << ", "
            << rz * 180.0 / PI << "]，发送给 "
            << target_arm
            << " 臂"
            << std::endl;
    }
}