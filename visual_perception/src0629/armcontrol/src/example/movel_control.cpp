#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include <rclcpp/executors/multi_threaded_executor.hpp>

#include "std_msgs/msg/bool.hpp"
#include "msg_det/msg/detect_res.hpp"

#include "rokae/robot.h"
#include "print_helper.hpp"
#include "rokae/utility.h"
#include "rokae/model.h"

#include <rclcpp/rclcpp.hpp>

#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/float64_multi_array.hpp>

#include <msg_det/msg/detect_res.hpp>

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <algorithm>

using namespace std::chrono_literals;
using namespace rokae;


class RokaeMovejNode : public rclcpp::Node
{
public:
    RokaeMovejNode(
        const std::string& node_name,
        const std::string& robot_ip,
        const std::string& local_ip,
        const std::string& detect_topic_name,
        const std::string& ready_topic_name
    )
    : Node(node_name),
      robot_ip_(robot_ip),
      local_ip_(local_ip),
      detect_topic_name_(detect_topic_name),
      ready_topic_name_(ready_topic_name)
    {
        this->declare_parameter<double>("speed", 50.0);
        this->declare_parameter<double>("acc", 5.0);
        this->declare_parameter<double>("elbow_deg", 16.6993);


        speed_ = this->get_parameter("speed").as_double();
        acc_ = this->get_parameter("acc").as_double();
        elbow_deg_ = this->get_parameter("elbow_deg").as_double();


        RCLCPP_INFO(
            this->get_logger(),
            "Connecting robot_ip=%s, local_ip=%s",
            robot_ip_.c_str(),
            local_ip_.c_str()
        );

        RCLCPP_INFO(
            this->get_logger(),
            "Subscribe detect topic: %s",
            detect_topic_name_.c_str()
        );

        RCLCPP_INFO(
            this->get_logger(),
            "Publish ready topic: %s",
            ready_topic_name_.c_str()
        );

        RCLCPP_INFO(
            this->get_logger(),
            "MoveJ parameters: speed=%.3f, acc=%.3f, elbow=%.4f deg",
            speed_,
            acc_,
            elbow_deg_
        );

        robot_ = std::make_unique<ArRobot>(robot_ip_, local_ip_);

        if (!initRobot()) {
            RCLCPP_ERROR(this->get_logger(), "Robot init failed.");
            throw std::runtime_error("Robot init failed");
        }
        base_in_world_ = robot_->baseFrame(ec);

        if (ec) {
        RCLCPP_ERROR(
            this->get_logger(),
            "robot_->baseFrame failed, ec=%d, msg=%s",
            ec.value(),
            ec.message().c_str()
        );


        RCLCPP_INFO(
            this->get_logger(),
            "base_in_world=[%.6f, %.6f, %.6f, %.6f, %.6f, %.6f]",
            base_in_world_[0],
            base_in_world_[1],
            base_in_world_[2],
            base_in_world_[3],
            base_in_world_[4],
            base_in_world_[5]
        );


        throw std::runtime_error("robot baseFrame failed");
    }


        auto ready_qos = rclcpp::QoS(rclcpp::KeepLast(1))
            .reliable()
            .transient_local();

        robot_ready_pub_ = this->create_publisher<std_msgs::msg::Bool>(
            ready_topic_name_,
            ready_qos
        );

        detect_sub_ = this->create_subscription<msg_det::msg::DetectRes>(
            detect_topic_name_,
            10,
            std::bind(
                &RokaeMovejNode::detectResCallback,
                this,
                std::placeholders::_1
            )
        );

        publishRobotReady(true);

        RCLCPP_INFO(
            this->get_logger(),
            "Rokae MoveJ flan_in_base topic node started. Robot IP: %s",
            robot_ip_.c_str()
        );
    }

private:
    static constexpr double PI_ = 3.14159265358979323846;

    std::string robot_ip_;
    std::string local_ip_;
    std::string detect_topic_name_;
    std::string ready_topic_name_;
    std::error_code ec;
    std::unique_ptr<ArRobot> robot_;

    rclcpp::Subscription<msg_det::msg::DetectRes>::SharedPtr detect_sub_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr robot_ready_pub_;

    std::mutex motion_mutex_;

    double speed_{50.0};
    double acc_{5.0};
    double elbow_deg_{16.6993};
    rokae::Toolset tool_set_;
    std::array<double, 6> base_in_world_{
        0.0, 0.0, 0.0,
        0.0, 0.0, 0.0
    };
    /*
     * base_in_world：机器人基坐标系相对于外部参考系的位姿。
     * x y z 单位：米
     * rx ry rz 单位：弧度
     */
    // std::array<double, 6> base_in_world_ = robot.baseFrame(ec);

private:
    bool checkEc(const std::string& name, const error_code& ec)
    {
        if (ec) {
            RCLCPP_ERROR(
                this->get_logger(),
                "%s failed, ec=%d, msg=%s",
                name.c_str(),
                ec.value(),
                ec.message().c_str()
            );
            return false;
        }

        RCLCPP_INFO(this->get_logger(), "%s ok", name.c_str());
        return true;
    }

    bool initRobot()
    {
        // error_code ec;

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
        robot_->setDefaultConfOpt(false, ec);
        if (!checkEc("setDefaultConfOpt(false)", ec)) {
            return false;
        }

        ec.clear();
        const auto state = robot_->operationState(ec);
        if (!checkEc("operationState", ec)) {
            return false;
        }

        RCLCPP_INFO(
            this->get_logger(),
            "current operationState = %d",
            static_cast<int>(state)
        );

        return true;
    }

    void printPose(
        const std::string& name,
        const std::array<double, 6>& pose
    )
    {
        RCLCPP_INFO(
            this->get_logger(),
            "%s: x=%.3f mm, y=%.3f mm, z=%.3f mm, "
            "rx=%.3f deg, ry=%.3f deg, rz=%.3f deg",
            name.c_str(),
            pose[0] * 1000.0,
            pose[1] * 1000.0,
            pose[2] * 1000.0,
            pose[3] * 180.0 / PI_,
            pose[4] * 180.0 / PI_,
            pose[5] * 180.0 / PI_
        );
    }

    void publishRobotReady(bool ready)
    {
        if (!robot_ready_pub_) {
            return;
        }

        std_msgs::msg::Bool msg;
        msg.data = ready;

        robot_ready_pub_->publish(msg);

        RCLCPP_INFO(
            this->get_logger(),
            "[PUBLISH] %s: %s",
            ready_topic_name_.c_str(),
            ready ? "true, robot ready" : "false, robot moving"
        );
    }

    bool waitRobot()
    {
        bool has_run = false;
        int idle_after_start_count = 0;

        for (int i = 0; i < 300; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            error_code ec;
            const auto state = robot_->operationState(ec);

            if (ec) {
                RCLCPP_ERROR(
                    this->get_logger(),
                    "operationState failed, ec=%d, msg=%s",
                    ec.value(),
                    ec.message().c_str()
                );
                return false;
            }

            RCLCPP_INFO_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                1000,
                "waiting MoveJ, operationState=%d",
                static_cast<int>(state)
            );

            if (state != OperationState::idle) {
                has_run = true;
                idle_after_start_count = 0;
            }
            else if (!has_run) {
                ++idle_after_start_count;
            }

            if (!has_run && idle_after_start_count >= 10) {
                RCLCPP_WARN(
                    this->get_logger(),
                    "Robot still idle 1s after moveStart. MoveJ may not have been accepted."
                );
                return false;
            }

            if (has_run && state == OperationState::idle) {
                RCLCPP_INFO(this->get_logger(), "MoveJ finished");
                return true;
            }
        }

        RCLCPP_WARN(this->get_logger(), "Wait MoveJ timeout.");
        return false;
    }

    void detectResCallback(const msg_det::msg::DetectRes::SharedPtr msg)
    {
        std::lock_guard<std::mutex> lock(motion_mutex_);

        /*
         * 现在 topic 必须发送 6 个数：
         *
         * msg.pos[0] = x
         * msg.pos[1] = y
         * msg.pos[2] = z
         * msg.pos[3] = rx
         * msg.pos[4] = ry
         * msg.pos[5] = rz
         *
         * 含义：
         * flan_in_base = 法兰相对于机器人基坐标系的位姿
         *
         * 单位：
         * x y z：米
         * rx ry rz：弧度
         */
        if (msg->pos.size() < 6) {
            RCLCPP_ERROR(
                this->get_logger(),
                "DetectRes.pos needs 6 values: [x,y,z,rx,ry,rz], but got %zu",
                msg->pos.size()
            );

            publishRobotReady(true);
            return;
        }

        std::array<double, 6> flan_in_base = {
            msg->pos[0],
            msg->pos[1],
            msg->pos[2],
            msg->pos[3],
            msg->pos[4],
            msg->pos[5]
        };

        /*
         * 将法兰相对于基坐标系坐标，转换为末端相对于外部参考系坐标。
         *
         * target/end_in_ref 才是 CartesianPosition 和 MoveJCommand 使用的目标。
         */
        std::array<double, 6> target;

        try {
            target = rokae::Utils::FlanInBaseToEndInRef(base_in_world_,tool_set_,flan_in_base);
        }
        catch (const std::exception& e) {
            RCLCPP_ERROR(
                this->get_logger(),
                "FlanInBaseToEndInRef failed: %s",
                e.what()
            );

            publishRobotReady(true);
            return;
        }

        RCLCPP_INFO(
            this->get_logger(),
            "[RECEIVE] %s: type=%d, flan_in_base=[%.6f, %.6f, %.6f, %.6f, %.6f, %.6f]",
            detect_topic_name_.c_str(),
            static_cast<int>(msg->type),
            flan_in_base[0],
            flan_in_base[1],
            flan_in_base[2],
            flan_in_base[3],
            flan_in_base[4],
            flan_in_base[5]
        );

        RCLCPP_INFO(
            this->get_logger(),
            "[CONVERT] target endInRef=[%.6f, %.6f, %.6f, %.6f, %.6f, %.6f]",
            target[0],
            target[1],
            target[2],
            target[3],
            target[4],
            target[5]
        );

        error_code ec;

        ec.clear();
        const auto current =
            robot_->posture(CoordinateType::endInRef, ec);

        if (ec) {
            RCLCPP_ERROR(
                this->get_logger(),
                "get current posture failed, ec=%d, msg=%s",
                ec.value(),
                ec.message().c_str()
            );

            publishRobotReady(true);
            return;
        }

        printPose("current endInRef pose", current);
        printPose("target endInRef pose", target);

        const double dx = target[0] - current[0];
        const double dy = target[1] - current[1];
        const double dz = target[2] - current[2];

        const double distance =
            std::sqrt(dx * dx + dy * dy + dz * dz);

        RCLCPP_INFO(
            this->get_logger(),
            "target Cartesian distance=%.3f mm",
            distance * 1000.0
        );

        if (distance < 0.001) {
            RCLCPP_WARN(
                this->get_logger(),
                "Target is too close to current pose, skip MoveJ."
            );

            publishRobotReady(true);
            return;
        }

        CartesianPosition target_cart(target);
        target_cart.elbow = elbow_deg_ * PI_ / 180.0;

        MoveJCommand command(target_cart, speed_, acc_);

        std::string command_id;

        ec.clear();
        robot_->moveAppend({command}, command_id, ec);

        if (ec) {
            RCLCPP_ERROR(
                this->get_logger(),
                "MoveJ moveAppend failed, ec=%d, msg=%s",
                ec.value(),
                ec.message().c_str()
            );

            publishRobotReady(true);
            return;
        }

        RCLCPP_INFO(
            this->get_logger(),
            "MoveJ command ID=%s",
            command_id.c_str()
        );

        ec.clear();
        robot_->moveStart(ec);

        if (ec) {
            RCLCPP_ERROR(
                this->get_logger(),
                "MoveJ moveStart failed, ec=%d, msg=%s",
                ec.value(),
                ec.message().c_str()
            );

            publishRobotReady(true);
            return;
        }

        RCLCPP_INFO(this->get_logger(), "MoveJ moveStart ok");

        publishRobotReady(false);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        ec.clear();
        const auto state_after_start =
            robot_->operationState(ec);

        if (ec) {
            RCLCPP_ERROR(
                this->get_logger(),
                "operationState after MoveJ start failed, ec=%d, msg=%s",
                ec.value(),
                ec.message().c_str()
            );

            publishRobotReady(true);
            return;
        }

        RCLCPP_INFO(
            this->get_logger(),
            "operationState after MoveJ start=%d",
            static_cast<int>(state_after_start)
        );

        const bool move_ok = waitRobot();

        if (!move_ok) {
            RCLCPP_WARN(
                this->get_logger(),
                "MoveJ not confirmed finished, but release detect publisher."
            );
        }

        ec.clear();
        const auto after =
            robot_->posture(CoordinateType::endInRef, ec);

        if (!ec) {
            printPose("after endInRef pose", after);
        }
        else {
            RCLCPP_WARN(
                this->get_logger(),
                "get posture after MoveJ failed, ec=%d, msg=%s",
                ec.value(),
                ec.message().c_str()
            );
        }

        publishRobotReady(true);
    }
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);

    try {
        const std::string local_ip = "192.168.71.51";

        auto left_node = std::make_shared<RokaeMovejNode>(
            "rokae_movej_node_left",
            "192.168.71.161",
            local_ip,
            "/detect_res_left",
            "/detect_res_left_done"
        );

        auto right_node = std::make_shared<RokaeMovejNode>(
            "rokae_movej_node_right",
            "192.168.71.160",
            local_ip,
            "/detect_res_right",
            "/detect_res_right_done"
        );

        rclcpp::executors::MultiThreadedExecutor executor;

        executor.add_node(left_node);
        executor.add_node(right_node);

        executor.spin();
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    rclcpp::shutdown();
    return 0;
}


// export LD_LIBRARY_PATH=/home/admin/vsproject/control/src/armcontrol/lib/Linux/aarch64:$LD_LIBRARY_PATH
// ros2 run rokae_ros_control rokae_movel_ros_node
// ros2 topic pub /rokae/movel_pose6_right std_msgs/msg/Float64MultiArray "{data: [0.344609, -0.559282, -0.172906, 2.356194490192345, 0, 1.3962634015954636]}" --once
// ros2 topic pub /rokae/movel_pose6_left  std_msgs/msg/Float64MultiArray "{data: [-0.037, 0.8, -0.2, -2.5656340004316642, 1.2566370614359172, -0.9599310885968813]}" --once


// ros2 topic pub /rokae/movel_pose6_left std_msgs/msg/Float64MultiArray "{data: [0.5, 0.43, -0.2, 2.2, 0.6, 2.4]}" --once
// ros2 topic pub /rokae/movel_pose6_right  std_msgs/msg/Float64MultiArray "{data: [0.5, -0.43, -0.2, -2.9, 0.83, 2.9]}" --once

//ros2 topic pub /rokae/rokae_movej_node_right  std_msgs/msg/Float64MultiArray "{data: [0.284639, -0.806, -0.163, -2.9, 0.83, 2.9]}" --once

// ros2 topic pub /rokae/movej_pose6_left std_msgs/msg/Float64MultiArray "{data: [0.68, 0.06, -0.11, 1.57, 0, 1.57]}" --once


//从基坐标系 x向下  y向后  z向左 

//世界坐标系 x向前  y向左  z向上