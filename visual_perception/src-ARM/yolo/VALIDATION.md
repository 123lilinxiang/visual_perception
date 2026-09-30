# 验证记录

基础提交：ce28690677d61821a5f904967e4074d5845fbddc（Grasp 分支）。
修改范围：visual_perception/src-obb&det/yolo。

## 已执行

1. 使用 NVIDIA 官方 release/8.5 和 release/10.3 的 TensorRT C++ 头文件，
   配合 NVIDIA CUDA 12 开发头文件、OpenCV 4.5.4 官方头文件，分别执行
   g++ -std=c++17 -fsyntax-only：
   - src/yolov8-detection.cpp：两个版本均通过。
   - src/yolov8-obb.cpp：两个版本均通过。
   - tests/trt_smoke.cpp：两个版本均通过。
   这是 x86 主机上的语法/API 编译检查，不包含链接，不等于 ARM64 实机编译。
2. 用独立的模拟 TensorRT/CUDA 测试执行 TRT8、TRT10 两条分支：
   - 输出在输入之前的 I/O 枚举顺序；按名称/实际索引绑定。
   - TRT8 多 profile 槽位，使用 profile 0。
   - 固定形状和动态形状；扩大/缩小输出尺寸并检查拷贝边界。
   - 预热、重复 make_pipe、未分配时调用 infer、输入字节数错误。
   - 模型缺失、非法数据类型/维度、超出 profile、类别数不匹配、enqueue 失败。
   - 正常析构及构造失败的资源回收计数。
   两个分支通过 AddressSanitizer/UndefinedBehaviorSanitizer 检查。
   受执行环境 /proc 限制，LeakSanitizer 未运行；模拟分配表及对象计数归零。
   这些是模拟逻辑测试，不是 GPU 推理结果。
   可复现命令：bash tests/run_mock_tests.sh（默认不带 sanitizer）。
3. CMake 独立查找测试：在 aarch64-linux-gnu 布局的临时 SDK 夹具中，
   成功找到头文件、nvinfer、nvinfer_plugin，并解析版本 10.3.0。
   夹具只验证查找规则，不含可链接的真实 ARM 库。
4. shell 语法检查、package.xml 解析、git diff --check、修改路径范围检查。

## 仍需在 AGX Orin 上执行

- colcon 完整构建和链接（包括原 ROS2 / cv_bridge / msg_det 依赖）。
- 用本机 TensorRT 从 ONNX 构建 engine。
- README 中的 yolo_trt_smoke 实际 GPU 测试。
- 相机图像、深度、标定、ROS2 消息、准确率和帧率检查。

当前环境没有 Orin、ROS2 或用户模型，未声称以上实机验证已通过。
