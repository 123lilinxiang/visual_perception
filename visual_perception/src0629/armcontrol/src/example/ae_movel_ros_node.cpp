#include <iostream>
#include <thread>
#include <mutex>
#include <cmath>
#include <memory>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>


#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float64_multi_array.hpp"

#include "rokae/robot.h"
#include "print_helper.hpp"
#include "rokae/utility.h"
#include "rokae/model.h"

using namespace rokae;

class RokaeMovelNode : public rclcpp::Node
{
public:
    RokaeMovelNode()
        : Node("rokae_movel_node")
    {
        this->declare_parameter<std::string>("robot_ip", "192.168.71.161");
        this->declare_parameter<std::string>("local_ip", "192.168.71.51");
        this->declare_parameter<std::string>("axis", "y");  // 默认控制 Y 轴
        this->declare_parameter<double>("speed", 50.0);
        this->declare_parameter<double>("acc", 5.0);
        this->declare_parameter<double>("max_delta", 0.2);  // 单次最大 0.2m，防止误发
        this->declare_parameter<std::string>("pose_file","/home/admin/vsproject/control/rokae_saved_pose.txt");

        std::string robot_ip = this->get_parameter("robot_ip").as_string();
        std::string local_ip = this->get_parameter("local_ip").as_string();
        pose_file_ = this->get_parameter("pose_file").as_string();
        RCLCPP_INFO(this->get_logger(), "Pose save file: %s", pose_file_.c_str());
        axis_ = this->get_parameter("axis").as_string();
        speed_ = this->get_parameter("speed").as_double();
        acc_ = this->get_parameter("acc").as_double();
        max_delta_ = this->get_parameter("max_delta").as_double();

        RCLCPP_INFO(this->get_logger(), "Connecting robot_ip=%s, local_ip=%s",
                    robot_ip.c_str(), local_ip.c_str());

        robot_ = std::make_unique<ArRobot>(robot_ip, local_ip);

        if (!initRobot()) {
            RCLCPP_ERROR(this->get_logger(), "Robot init failed.");
            throw std::runtime_error("Robot init failed");
        }

        sub_ = this->create_subscription<std_msgs::msg::Float64MultiArray>(
            "/rokae/movel_delta6",
            10,
            std::bind(&RokaeMovelNode::movelDelta6Callback, this, std::placeholders::_1)
        );

        RCLCPP_INFO(this->get_logger(),
                    "Rokae movel node started. Topic: /rokae/movel_delta, axis=%s",
                    axis_.c_str());
    }

    void saveCurrentPoseToTxt()
    {
        std::lock_guard<std::mutex> lock(motion_mutex_);

        error_code ec;
        ec.clear();

        auto cur = robot_->posture(CoordinateType::endInRef, ec);
        if (ec) {
            RCLCPP_ERROR(this->get_logger(),
                        "save pose failed, get current posture failed, ec=%d, msg=%s",
                        ec.value(), ec.message().c_str());
            return;
        }

        printPose("saved current pose", cur);

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

        ofs.close();

        RCLCPP_INFO(this->get_logger(),
                    "end pose saved to: %s",
                    pose_file_.c_str());
    }

private:
    std::unique_ptr<ArRobot> robot_;
    rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr sub_;
    std::mutex motion_mutex_;

    std::string axis_;
    double speed_{50.0};
    double acc_{5.0};
    double max_delta_{0.2};
    std::string pose_file_;

private:
    bool checkEc(const std::string &name, const error_code &ec)
    {
        if (ec) {
            RCLCPP_ERROR(this->get_logger(), "%s failed, ec=%d, msg=%s",
                         name.c_str(), ec.value(), ec.message().c_str());
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
        if (!checkEc("setOperateMode", ec)) return false;

        ec.clear();
        robot_->setMotionControlMode(MotionControlMode::NrtCommand, ec);
        if (!checkEc("setMotionControlMode", ec)) return false;

        ec.clear();
        robot_->setPowerState(true, ec);
        if (!checkEc("setPowerState", ec)) return false;

        ec.clear();
        auto st = robot_->operationState(ec);
        if (!checkEc("operationState", ec)) return false;

        RCLCPP_INFO(this->get_logger(), "current operationState = %d", static_cast<int>(st));

        return true;
    }

    int axisIndex() const
    {
        if (axis_ == "x" || axis_ == "X") return 0;
        if (axis_ == "y" || axis_ == "Y") return 1;
        if (axis_ == "z" || axis_ == "Z") return 2;

        // 默认 Y
        return 1;
    }

    void printPose(const std::string &name, const std::array<double, 6> &pose)
    {
        RCLCPP_INFO(this->get_logger(),
                    "%s: x=%.3f mm, y=%.3f mm, z=%.3f mm, rx=%.3f deg, ry=%.3f deg, rz=%.3f deg",
                    name.c_str(),
                    pose[0] * 1000.0,
                    pose[1] * 1000.0,
                    pose[2] * 1000.0,
                    pose[3] * 180.0 / M_PI,
                    pose[4] * 180.0 / M_PI,
                    pose[5] * 180.0 / M_PI);
    }

    void waitRobot()
    {
        bool hasRun = false;

        for (int i = 0; i < 300; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            error_code ec;
            auto st = robot_->operationState(ec);

            if (ec) {
                RCLCPP_ERROR(this->get_logger(), "operationState failed, ec=%d, msg=%s",
                             ec.value(), ec.message().c_str());
                return;
            }

            if (st != OperationState::idle) {
                hasRun = true;
            }

            if (hasRun && st == OperationState::idle) {
                RCLCPP_INFO(this->get_logger(), "motion finished");
                return;
            }
        }

        if (!hasRun) {
            RCLCPP_WARN(this->get_logger(), "Robot never left idle state.");
        } else {
            RCLCPP_WARN(this->get_logger(), "Wait motion timeout.");
        }
    }

   void movelDelta6Callback(const std_msgs::msg::Float64MultiArray::SharedPtr msg)
    {
        std::lock_guard<std::mutex> lock(motion_mutex_);

        if (msg->data.size() != 6) {
            RCLCPP_ERROR(this->get_logger(),
                        "movel_delta6 need exactly 6 values: [dx, dy, dz, drx, dry, drz], but got %zu",
                        msg->data.size());
            return;
        }

        double dx  = msg->data[0];
        double dy  = msg->data[1];
        double dz  = msg->data[2];
        double drx = msg->data[3];
        double dry = msg->data[4];
        double drz = msg->data[5];

        // 安全限制：前三个是平移，单位 m
        double max_trans_delta = 0.5; // 单次最大 200mm

        // 安全限制：后三个是姿态，单位 rad
        double max_rot_delta = 100.0 * M_PI / 180.0; // 单次最大 30度

        if (std::abs(dx) > max_trans_delta ||
            std::abs(dy) > max_trans_delta ||
            std::abs(dz) > max_trans_delta) {
            RCLCPP_ERROR(this->get_logger(),
                        "translation delta too large. dx=%.3f, dy=%.3f, dz=%.3f, max=%.3f m",
                        dx, dy, dz, max_trans_delta);
            return;
        }

        if (std::abs(drx) > max_rot_delta ||
            std::abs(dry) > max_rot_delta ||
            std::abs(drz) > max_rot_delta) {
            RCLCPP_ERROR(this->get_logger(),
                        "rotation delta too large. drx=%.3f, dry=%.3f, drz=%.3f, max=%.3f rad",
                        drx, dry, drz, max_rot_delta);
            return;
        }

        error_code ec;

        ec.clear();
        auto cur = robot_->posture(CoordinateType::endInRef, ec);
        if (ec) {
            RCLCPP_ERROR(this->get_logger(),
                        "get current posture failed, ec=%d, msg=%s",
                        ec.value(), ec.message().c_str());
            return;
        }

        printPose("current pose", cur);

        auto target = cur;

        // 核心：6个数据分别对应相加/相减
        // 前三个单位：m
        // 后三个单位：rad
        target[0] += dx;
        target[1] += dy;
        target[2] += dz;
        target[3] += drx;
        target[4] += dry;
        target[5] += drz;

        printPose("target pose", target);

        CartesianPosition target_cart(target);

        MoveLCommand cmd(target_cart, speed_, acc_);

        std::string cmdID;

        ec.clear();
        robot_->moveAppend({cmd}, cmdID, ec);
        if (ec) {
            RCLCPP_ERROR(this->get_logger(),
                        "moveAppend failed, ec=%d, msg=%s",
                        ec.value(), ec.message().c_str());
            return;
        }

        RCLCPP_INFO(this->get_logger(), "cmdID = %s", cmdID.c_str());

        ec.clear();
        robot_->moveStart(ec);
        if (ec) {
            RCLCPP_ERROR(this->get_logger(),
                        "moveStart failed, ec=%d, msg=%s",
                        ec.value(), ec.message().c_str());
            return;
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));

        ec.clear();
        auto after = robot_->posture(CoordinateType::endInRef, ec);
        if (!ec) {
            printPose("after pose", after);
        }
    }
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);

    try {
        auto node = std::make_shared<RokaeMovelNode>();

        std::thread keyboard_thread([node]() {
            std::string input;

            std::cout << "Input 's' then Enter to save current pose to txt." << std::endl;

            while (rclcpp::ok()) {
                std::getline(std::cin, input);

                if (!rclcpp::ok()) {
                    break;
                }

                if (input == "s" || input == "S") {
                    node->saveCurrentPoseToTxt();
                } else if (!input.empty()) {
                    std::cout << "Unknown input: " << input
                              << ", input 's' to save pose." << std::endl;
                }
            }
        });

        keyboard_thread.detach();

        rclcpp::spin(node);
    }
    catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    rclcpp::shutdown();
    return 0;
}

// ros2 topic pub /rokae/movel_delta6  std_msgs/msg/Float64MultiArray "{data: [0,0,0.1, 0, 0, 0]}" --once