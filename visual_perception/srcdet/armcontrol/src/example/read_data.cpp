#include <iostream>
#include <mutex>
#include <cmath>
#include <memory>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <filesystem>
#include <cstdint>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/empty.hpp"

#include "rokae/robot.h"
#include "rokae/utility.h"
#include "rokae/model.h"

using namespace rokae;

class RokaeSavePoseNode : public rclcpp::Node
{
public:
    RokaeSavePoseNode()
        : Node("rokae_save_pose_node")
    {
        this->declare_parameter<std::string>("robot_ip", "192.168.71.160");
        this->declare_parameter<std::string>("local_ip", "192.168.71.51");
        this->declare_parameter<std::string>(
            "pose_file",
            "/home/admin/vsproject/control/rokae_saved_pose.txt"
        );

        std::string robot_ip = this->get_parameter("robot_ip").as_string();
        std::string local_ip = this->get_parameter("local_ip").as_string();
        pose_file_ = this->get_parameter("pose_file").as_string();

        RCLCPP_INFO(this->get_logger(),
                    "Connecting robot_ip=%s, local_ip=%s",
                    robot_ip.c_str(),
                    local_ip.c_str());

        RCLCPP_INFO(this->get_logger(),
                    "Pose save file: %s",
                    pose_file_.c_str());

        robot_ = std::make_unique<ArRobot>(robot_ip, local_ip);

        if (!initRobot()) {
            RCLCPP_ERROR(this->get_logger(), "Robot init failed.");
            throw std::runtime_error("Robot init failed");
        }

        save_sub_ = this->create_subscription<std_msgs::msg::Empty>(
            "/rokae/save_pose",
            10,
            [this](const std_msgs::msg::Empty::SharedPtr msg) {
                (void)msg;
                RCLCPP_INFO(this->get_logger(),
                            "Receive /rokae/save_pose, try saving current flange pose...");
                this->saveCurrentPoseToTxt();
            }
        );

        RCLCPP_INFO(this->get_logger(), "Node started.");
        RCLCPP_INFO(this->get_logger(), "Save pose topic: /rokae/save_pose");
        RCLCPP_INFO(this->get_logger(), "Use:");
        RCLCPP_INFO(this->get_logger(),
                    "ros2 topic pub --once /rokae/save_pose std_msgs/msg/Empty \"{}\"");
    }

private:
    std::unique_ptr<ArRobot> robot_;
    std::mutex motion_mutex_;
    std::string pose_file_;

    rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr save_sub_;

    std::uint64_t save_count_{0};

private:
    bool checkEc(const std::string &name, const error_code &ec)
    {
        if (ec) {
            RCLCPP_ERROR(this->get_logger(),
                         "%s failed, ec=%d, msg=%s",
                         name.c_str(),
                         ec.value(),
                         ec.message().c_str());
            return false;
        }

        RCLCPP_INFO(this->get_logger(), "%s ok", name.c_str());
        return true;
    }

    bool initRobot()
    {
        error_code ec;

        ec.clear();
        robot_->setOperateMode(OperateMode::automatic, ec);
        if (!checkEc("setOperateMode", ec)) {
            return false;
        }

        ec.clear();
        robot_->setMotionControlMode(MotionControlMode::NrtCommand, ec);
        if (!checkEc("setMotionControlMode", ec)) {
            return false;
        }

        ec.clear();
        robot_->setPowerState(true, ec);
        if (!checkEc("setPowerState", ec)) {
            return false;
        }

        ec.clear();
        auto st = robot_->operationState(ec);
        if (!checkEc("operationState", ec)) {
            return false;
        }

        RCLCPP_INFO(this->get_logger(),
                    "current operationState = %d",
                    static_cast<int>(st));

        return true;
    }

    bool preparePoseFilePath()
    {
        try {
            std::filesystem::path file_path(pose_file_);
            std::filesystem::path dir = file_path.parent_path();

            if (!dir.empty() && !std::filesystem::exists(dir)) {
                std::filesystem::create_directories(dir);

                RCLCPP_INFO(this->get_logger(),
                            "created pose directory: %s",
                            dir.string().c_str());
            }
        }
        catch (const std::exception &e) {
            RCLCPP_ERROR(this->get_logger(),
                         "create pose directory failed: %s",
                         e.what());
            return false;
        }

        return true;
    }

    bool checkRobotIdle()
    {
        error_code ec;
        ec.clear();

        auto st = robot_->operationState(ec);
        if (ec) {
            RCLCPP_ERROR(this->get_logger(),
                         "operationState failed, ec=%d, msg=%s",
                         ec.value(),
                         ec.message().c_str());
            return false;
        }

        if (st != OperationState::idle) {
            RCLCPP_WARN(this->get_logger(),
                        "robot is not idle, skip saving pose. state=%d",
                        static_cast<int>(st));
            return false;
        }

        return true;
    }

    void printPose(const std::string &name,
                   const std::array<double, 6> &pose)
    {
        constexpr double RAD2DEG = 180.0 / 3.14159265358979323846;

        RCLCPP_INFO(this->get_logger(),
                    "%s: x=%.3f mm, y=%.3f mm, z=%.3f mm, "
                    "rx=%.3f deg, ry=%.3f deg, rz=%.3f deg",
                    name.c_str(),
                    pose[0] * 1000.0,
                    pose[1] * 1000.0,
                    pose[2] * 1000.0,
                    pose[3] * RAD2DEG,
                    pose[4] * RAD2DEG,
                    pose[5] * RAD2DEG);
    }

    void saveCurrentPoseToTxt()
    {
        std::lock_guard<std::mutex> lock(motion_mutex_);

        if (!checkRobotIdle()) {
            return;
        }

        error_code ec;
        ec.clear();

        // 手眼标定建议保存 flangeInBase：
        // 即法兰坐标系在机器人 base 坐标系下的位姿。
        //
        // 保存格式：
        // x y z rx ry rz
        //
        // x/y/z 单位：m
        // rx/ry/rz 单位：rad
        auto cur = robot_->posture(CoordinateType::flangeInBase, ec);

        if (ec) {
            RCLCPP_ERROR(this->get_logger(),
                         "get current flangeInBase posture failed, ec=%d, msg=%s",
                         ec.value(),
                         ec.message().c_str());
            return;
        }

        printPose("current flangeInBase pose", cur);

        if (!preparePoseFilePath()) {
            return;
        }

        std::ofstream ofs(pose_file_, std::ios::app);
        if (!ofs.is_open()) {
            RCLCPP_ERROR(this->get_logger(),
                         "open pose file failed: %s",
                         pose_file_.c_str());
            return;
        }

        ofs << std::fixed << std::setprecision(12)
            << cur[0] << " "
            << cur[1] << " "
            << cur[2] << " "
            << cur[3] << " "
            << cur[4] << " "
            << cur[5]
            << std::endl;

        ofs.flush();
        ofs.close();

        save_count_++;

        RCLCPP_INFO(this->get_logger(),
                    "SUCCESS: pose #%lu saved to: %s",
                    static_cast<unsigned long>(save_count_),
                    pose_file_.c_str());
    }
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);

    try {
        auto node = std::make_shared<RokaeSavePoseNode>();
        rclcpp::spin(node);
    }
    catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    rclcpp::shutdown();
    return 0;
}

//ros2 topic pub --once /rokae/save_pose std_msgs/msg/Empty "{}"