#include <chrono>
#include <any>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "rokae/robot.h"
#include "rokae/utility.h"

using namespace rokae;

struct DemoConfig {
    // 控制器 IP
    std::string trunk_robot_ip;
    // 轨迹速度，越大动作越快，调试阶段建议先用较小值
    double trunk_speed;
    // 预留参数：当前示例仅读取，不下发给控制器
    double trunk_acc;
    // 动作序列循环次数，>=1
    int loop_count;
    // true: 配置按角度(deg)填写并自动转弧度(rad)
    bool angles_in_degree;
    // 每个动作的显示名称，仅用于日志
    std::vector<std::string> names;
    // 躯干关节目标，固定 4 轴
    std::vector<std::vector<double>> trunks;
    // 头部外部轴目标，固定 2 轴
    std::vector<std::vector<double>> heads;
    // 每个动作完成后的额外等待时间（毫秒）
    std::vector<int> wait_after_ms;
};

double degToRad(double degree) {
    return degree * M_PI / 180.0;
}

void maybeConvertToRadian(std::vector<double>& values, bool angles_in_degree) {
    if (!angles_in_degree) {
        return;
    }
    for (auto& v : values) {
        v = degToRad(v);
    }
}

bool readDoubleArray(const YAML::Node& node, size_t expected_size, std::vector<double>& values) {
    if (!node || !node.IsSequence() || node.size() != expected_size) {
        return false;
    }

    values.clear();
    values.reserve(expected_size);
    for (size_t i = 0; i < expected_size; ++i) {
        values.push_back(node[i].as<double>());
    }
    return true;
}

bool loadDemoConfig(const std::string& config_file, DemoConfig& config) {
    try {
        // 读取 YAML；文件不存在或格式错误会抛异常
        YAML::Node root = YAML::LoadFile(config_file);

        if (root["robot"] && root["robot"]["trunk_robot_ip"]) {
            config.trunk_robot_ip = root["robot"]["trunk_robot_ip"].as<std::string>();
        } else {
            std::cerr << "[ERROR] 配置缺失: robot.trunk_robot_ip" << std::endl;
            return false;
        }

        config.trunk_speed = (root["motion"] && root["motion"]["trunk_speed"])
            ? root["motion"]["trunk_speed"].as<double>()
            : 200.0;
        config.trunk_acc = (root["motion"] && root["motion"]["trunk_acc"])
            ? root["motion"]["trunk_acc"].as<double>()
            : 100.0;
        config.loop_count = (root["motion"] && root["motion"]["loop_count"])
            ? root["motion"]["loop_count"].as<int>()
            : 1;
        const std::string angle_unit = (root["motion"] && root["motion"]["angle_unit"])
            ? root["motion"]["angle_unit"].as<std::string>()
            : "deg";
        config.angles_in_degree = (angle_unit != "rad");

        if (config.loop_count < 1) {
            std::cerr << "[ERROR] 配置非法: motion.loop_count 需要 >= 1" << std::endl;
            return false;
        }

        // 逐条加载动作序列，任一条格式不对将直接返回失败
        if (root["sequences"] && root["sequences"].IsSequence()) {
            for (const auto& item : root["sequences"]) {
                std::vector<double> trunk;
                std::vector<double> head;
                std::string name = item["name"] ? item["name"].as<std::string>() : "未命名动作";
                if (!readDoubleArray(item["trunk"], 4, trunk)) {
                    std::cerr << "[ERROR] 配置缺失或非法: sequences[].trunk (需要 4 个数)" << std::endl;
                    return false;
                }
                if (!readDoubleArray(item["head"], 2, head)) {
                    std::cerr << "[ERROR] 配置缺失或非法: sequences[].head (需要 2 个数)" << std::endl;
                    return false;
                }
                maybeConvertToRadian(trunk, config.angles_in_degree);
                maybeConvertToRadian(head, config.angles_in_degree);
                config.names.push_back(name);
                config.trunks.push_back(trunk);
                config.heads.push_back(head);
                config.wait_after_ms.push_back(item["wait_after_ms"] ? item["wait_after_ms"].as<int>() : 0);
            }
        }

        if (config.trunks.empty()) {
            // 未配置 sequences 时给出一组可运行的保守默认动作
            config.names = {"回到初始位", "动作1", "回到初始位"};
            config.trunks = {
                {0.0, -1.57, 0.0, 0.0},
                {0.04, -1.48, 0.03, 0.02},
                {0.0, -1.57, 0.0, 0.0},
            };
            config.heads = {
                {0.0, 0.0},
                {0.0, 0.0},
                {0.0, 0.0},
            };
            config.wait_after_ms = {500, 500, 500};
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] 加载配置文件失败: " << e.what() << std::endl;
        return false;
    }
}

bool waitForFinish(BaseRobot& robot, const std::string& traj_id) {
    using namespace rokae::EventInfoKey::MoveExecution;
    error_code ec;
    while (true) {
        // 轮询控制器状态：idle/unknown 代表当前队列执行结束或已中断
        auto st = robot.operationState(ec);
        auto info = robot.queryEventInfo(Event::moveExecution, ec);
        if (ec) {
            std::cout << "[ERROR] 查询运动状态失败: " << ec.message() << std::endl;
            return false;
        }
        auto id = std::any_cast<std::string>(info.at(ID));

        if (st == OperationState::idle || st == OperationState::unknown) {
            if (info.count(Error)) {
                if (auto move_ec = std::any_cast<error_code>(info.at(Error))) {
                    std::cout << "[ERROR] 路径 " << id << " 错误: " << move_ec.message() << std::endl;
                    return false;
                }
            }
            if (id == traj_id) {
                if (std::any_cast<bool>(info.at(ReachTarget))) {
                    std::cout << "[INFO] 路径 " << traj_id << " 已完成" << std::endl;
                    return true;
                }
                std::cout << "[ERROR] 路径 " << traj_id << " 未到达目标" << std::endl;
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <配置文件路径.yaml>" << std::endl;
        return 1;
    }

    DemoConfig config;
    if (!loadDemoConfig(argv[1], config)) {
        return 1;
    }

    std::cout << "[INFO] 躯干 MoveJ demo 启动" << std::endl;
    std::cout << "机器人IP: " << config.trunk_robot_ip << std::endl;
    std::cout << "速度: " << config.trunk_speed << std::endl;
    std::cout << "循环次数: " << config.loop_count << std::endl;
    std::cout << "角度单位: " << (config.angles_in_degree ? "deg(自动转rad)" : "rad") << std::endl;

    PCB4Robot robot(config.trunk_robot_ip);
    error_code ec;

    // 标准上电流程：自动模式 -> 上电 -> 设置非实时指令模式
    // 任一步失败都直接退出，避免在未知状态下继续动作
    robot.setOperateMode(OperateMode::automatic, ec);
    if (ec) { std::cout << "[ERROR] 设置操作模式失败: " << ec.message() << std::endl; return 1; }

    robot.setPowerState(true, ec);
    if (ec) { std::cout << "[ERROR] 上电失败: " << ec.message() << std::endl; return 1; }

    robot.setMotionControlMode(MotionControlMode::NrtCommand, ec);
    if (ec) { std::cout << "[ERROR] 设置运动控制模式失败: " << ec.message() << std::endl; return 1; }

    for (int loop_idx = 0; loop_idx < config.loop_count; ++loop_idx) {
        std::cout << "[INFO] 开始第 " << (loop_idx + 1) << "/" << config.loop_count << " 轮动作" << std::endl;

        for (size_t i = 0; i < config.trunks.size(); ++i) {
            std::cout << "[INFO] MoveJ 点位 " << i + 1 << " - " << config.names[i] << std::endl;

            // MoveAbsJ: 绝对关节目标（弧度）
            MoveAbsJCommand cmd(config.trunks[i]);
            cmd.target.external = config.heads[i];
            cmd.speed = config.trunk_speed;

            std::string traj_id;
            robot.moveAppend({cmd}, traj_id, ec);
            if (ec) { std::cout << "[ERROR] 运动命令失败: " << ec.message() << std::endl; return 1; }

            robot.moveStart(ec);
            if (ec) { std::cout << "[ERROR] 启动运动失败: " << ec.message() << std::endl; return 1; }

            if (!waitForFinish(robot, traj_id)) {
                std::cout << "[ERROR] 点位 " << i + 1 << " 执行失败，已停止后续运动" << std::endl;
                return 1;
            }
            if (config.wait_after_ms[i] > 0) {
                // 为下一个动作预留稳定时间，便于观察与录像演示
                std::this_thread::sleep_for(std::chrono::milliseconds(config.wait_after_ms[i]));
            }
        }
    }

    std::cout << "[INFO] MoveJ demo 完成" << std::endl;
    return 0;
}