#include <any>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float64_multi_array.hpp"

#include "rokae/robot.h"
#include "rokae/utility.h"

using namespace rokae;

struct MotionTarget {
    std::vector<double> trunk;   // 4轴躯干
    std::vector<double> head;    // 2轴头部
    double speed;
};

double degToRad(double degree) {
    return degree * M_PI / 180.0;
}

void convertToRadian(std::vector<double>& values) {
    for (auto& v : values) {
        v = degToRad(v);
    }
}

bool waitForFinish(
    BaseRobot& robot,
    const std::string& traj_id,
    double timeout_s,
    const rclcpp::Logger& logger
) {
    using namespace rokae::EventInfoKey::MoveExecution;

    error_code ec;
    auto start_time = std::chrono::steady_clock::now();

    while (rclcpp::ok()) {
        auto now = std::chrono::steady_clock::now();
        double elapsed_s = std::chrono::duration<double>(now - start_time).count();

        if (timeout_s > 0.0 && elapsed_s > timeout_s) {
            RCLCPP_ERROR(logger, "等待运动完成超时，traj_id=%s", traj_id.c_str());
            return false;
        }

        auto st = robot.operationState(ec);
        if (ec) {
            RCLCPP_ERROR(logger, "查询 operationState 失败: %s", ec.message().c_str());
            return false;
        }

        auto info = robot.queryEventInfo(Event::moveExecution, ec);
        if (ec) {
            RCLCPP_ERROR(logger, "查询运动事件失败: %s", ec.message().c_str());
            return false;
        }

        std::string current_id;
        try {
            if (info.count(ID)) {
                current_id = std::any_cast<std::string>(info.at(ID));
            }
        } catch (const std::exception& e) {
            RCLCPP_WARN(logger, "解析运动 ID 失败: %s", e.what());
        }

        if (st == OperationState::idle || st == OperationState::unknown) {
            if (info.count(Error)) {
                try {
                    auto move_ec = std::any_cast<error_code>(info.at(Error));
                    if (move_ec) {
                        RCLCPP_ERROR(
                            logger,
                            "路径 %s 执行错误: %s",
                            current_id.c_str(),
                            move_ec.message().c_str()
                        );
                        return false;
                    }
                } catch (const std::exception& e) {
                    RCLCPP_WARN(logger, "解析运动错误信息失败: %s", e.what());
                }
            }

            if (current_id == traj_id) {
                try {
                    if (info.count(ReachTarget) && std::any_cast<bool>(info.at(ReachTarget))) {
                        RCLCPP_INFO(logger, "路径 %s 已完成", traj_id.c_str());
                        return true;
                    }
                } catch (const std::exception& e) {
                    RCLCPP_WARN(logger, "解析 ReachTarget 失败: %s", e.what());
                }

                RCLCPP_ERROR(logger, "路径 %s 未到达目标", traj_id.c_str());
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return false;
}

class TrunkHeadMoveJNode : public rclcpp::Node {
public:
    TrunkHeadMoveJNode()
        : Node("trunk_head_movej_node")
    {
        this->declare_parameter<std::string>("robot_ip", "192.168.71.162");
        this->declare_parameter<std::string>("topic_name", "/rokae/trunk_head_movej");
        this->declare_parameter<double>("speed", 50.0);
        this->declare_parameter<bool>("angles_in_degree", false);
        this->declare_parameter<bool>("replace_pending_command", true);
        this->declare_parameter<double>("finish_timeout_s", 20.0);

        robot_ip_ = this->get_parameter("robot_ip").as_string();
        topic_name_ = this->get_parameter("topic_name").as_string();
        speed_ = this->get_parameter("speed").as_double();
        angles_in_degree_ = this->get_parameter("angles_in_degree").as_bool();
        replace_pending_command_ = this->get_parameter("replace_pending_command").as_bool();
        finish_timeout_s_ = this->get_parameter("finish_timeout_s").as_double();

        RCLCPP_INFO(this->get_logger(), "躯干 + 头部 MoveJ topic 节点启动");
        RCLCPP_INFO(this->get_logger(), "robot_ip: %s", robot_ip_.c_str());
        RCLCPP_INFO(this->get_logger(), "topic_name: %s", topic_name_.c_str());
        RCLCPP_INFO(this->get_logger(), "speed: %.3f", speed_);
        RCLCPP_INFO(
            this->get_logger(),
            "topic角度单位: %s",
            angles_in_degree_ ? "degree，内部自动转 rad" : "rad"
        );

        initRobot();

        sub_ = this->create_subscription<std_msgs::msg::Float64MultiArray>(
            topic_name_,
            10,
            std::bind(&TrunkHeadMoveJNode::topicCallback, this, std::placeholders::_1)
        );

        worker_thread_ = std::thread(&TrunkHeadMoveJNode::workerLoop, this);
    }

    ~TrunkHeadMoveJNode() override {
        running_ = false;
        cv_.notify_all();

        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

private:
    void initRobot() {
        try {
            robot_ = std::make_unique<PCB4Robot>(robot_ip_);
        } catch (const std::exception& e) {
            RCLCPP_ERROR(this->get_logger(), "连接机器人失败: %s", e.what());
            throw;
        }

        error_code ec;

        robot_->setOperateMode(OperateMode::automatic, ec);
        if (ec) {
            throw std::runtime_error("设置自动模式失败: " + ec.message());
        }

        robot_->setPowerState(true, ec);
        if (ec) {
            throw std::runtime_error("机器人上电失败: " + ec.message());
        }

        robot_->setMotionControlMode(MotionControlMode::NrtCommand, ec);
        if (ec) {
            throw std::runtime_error("设置 NrtCommand 模式失败: " + ec.message());
        }

        RCLCPP_INFO(this->get_logger(), "机器人初始化完成");
    }

    void topicCallback(const std_msgs::msg::Float64MultiArray::SharedPtr msg) {
        if (msg->data.size() != 6) {
            RCLCPP_ERROR(
                this->get_logger(),
                "topic数据长度错误，需要 6 个数: [trunk1, trunk2, trunk3, trunk4, head1, head2]，当前收到 %zu 个",
                msg->data.size()
            );
            return;
        }

        MotionTarget target;
        target.trunk = {
            msg->data[0],
            msg->data[1],
            msg->data[2],
            msg->data[3]
        };

        target.head = {
            msg->data[4],
            msg->data[5]
        };

        target.speed = speed_;

        if (angles_in_degree_) {
            convertToRadian(target.trunk);
            convertToRadian(target.head);
        }

        RCLCPP_INFO(
            this->get_logger(),
            "收到MoveJ目标: trunk=[%.6f, %.6f, %.6f, %.6f], head=[%.6f, %.6f], speed=%.3f",
            target.trunk[0],
            target.trunk[1],
            target.trunk[2],
            target.trunk[3],
            target.head[0],
            target.head[1],
            target.speed
        );

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);

            if (replace_pending_command_) {
                std::queue<MotionTarget> empty;
                std::swap(command_queue_, empty);
            }

            command_queue_.push(target);
        }

        cv_.notify_one();
    }

    void workerLoop() {
        while (running_ && rclcpp::ok()) {
            MotionTarget target;

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                cv_.wait(lock, [&]() {
                    return !running_ || !command_queue_.empty();
                });

                if (!running_) {
                    break;
                }

                target = command_queue_.front();
                command_queue_.pop();
            }

            executeMoveJ(target);
        }
    }

    void executeMoveJ(const MotionTarget& target) {
        if (!robot_) {
            RCLCPP_ERROR(this->get_logger(), "robot未初始化，无法执行运动");
            return;
        }

        error_code ec;

        try {
            MoveAbsJCommand cmd(target.trunk);

            // 头部外部轴，固定2轴
            cmd.target.external = target.head;

            // 速度
            cmd.speed = target.speed;

            std::string traj_id;

            robot_->moveAppend({cmd}, traj_id, ec);
            if (ec) {
                RCLCPP_ERROR(this->get_logger(), "moveAppend失败: %s", ec.message().c_str());
                return;
            }

            RCLCPP_INFO(this->get_logger(), "moveAppend成功，traj_id=%s", traj_id.c_str());

            robot_->moveStart(ec);
            if (ec) {
                RCLCPP_ERROR(this->get_logger(), "moveStart失败: %s", ec.message().c_str());
                return;
            }

            RCLCPP_INFO(this->get_logger(), "moveStart成功，等待运动完成...");

            bool ok = waitForFinish(*robot_, traj_id, finish_timeout_s_, this->get_logger());

            if (!ok) {
                RCLCPP_ERROR(this->get_logger(), "本次MoveJ执行失败");
                return;
            }

            RCLCPP_INFO(this->get_logger(), "本次MoveJ执行完成");

        } catch (const std::exception& e) {
            RCLCPP_ERROR(this->get_logger(), "执行MoveJ异常: %s", e.what());
        }
    }

private:
    std::string robot_ip_;
    std::string topic_name_;

    double speed_;
    bool angles_in_degree_;
    bool replace_pending_command_;
    double finish_timeout_s_;

    std::unique_ptr<PCB4Robot> robot_;

    rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr sub_;

    std::atomic_bool running_{true};
    std::thread worker_thread_;

    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::queue<MotionTarget> command_queue_;
};

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);

    try {
        auto node = std::make_shared<TrunkHeadMoveJNode>();
        rclcpp::spin(node);
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] 节点启动失败: " << e.what() << std::endl;
    }

    rclcpp::shutdown();
    return 0;
}

// export LD_LIBRARY_PATH=/home/admin/vsproject/control/src/trunkcontrol/lib/Linux/aarch64:$LD_LIBRARY_PATH
// ros2 run trunk_head_control trunk_head_movej_node 