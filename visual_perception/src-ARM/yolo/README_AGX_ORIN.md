# AGX Orin ARM64 适配说明

本包只包含 `visual_perception/src-obb&det/yolo` 目录。
基础源码：123lilinxiang/visual_perception，Grasp 分支，提交
`ce28690677d61821a5f904967e4074d5845fbddc`。
未修改该目录以外的相机驱动、自定义消息包或其他节点。

## 修改内容

- 移除 `/usr/local/TensorRT-8.6.1.6` 固定路径，自动查找本机 TensorRT。
  JetPack 的 `/usr/include/aarch64-linux-gnu` 与 `/usr/lib/aarch64-linux-gnu` 可被找到。
  可以用 `-DTensorRT_ROOT=/本机/SDK` 显式指定；切换 SDK 后需清理 CMake 缓存。
- 使用 `CUDA::cudart`；工程只有 C++ 文件，没有自定义 `.cu` 内核，
  因此不再强制开启 CUDA 编译器，也不再传递桌面 GPU 架构列表。
  NVIDIA 官方资料中 Orin 的计算能力为 8.7；以后新增 CUDA 内核时再设置 87。
- Detection、OBB 共用 `include/trt-runtime.hpp`，由 `NV_TENSORRT_MAJOR`
  自动选择 TRT8 binding API 或 TRT10 tensor-name API，无需手工定义 `TRT_10`。
- TRT10 为输入和输出设置 `setTensorAddress`，使用 `setInputShape` / `enqueueV3`。
  TRT8 根据真实 binding 索引构造 enqueueV2 数组，不假设输入一定是索引 0。
- 先设置输入形状，再解析输出形状；静态模型读取实际尺寸，动态模型默认使用
  profile 0 的 OPT 尺寸。调整尺寸时同步更新输出形状和所需内存。
- 使用普通 cudaMalloc，避免依赖 cudaMallocAsync 的可选内存池支持；
  输入采用自有 pinned staging buffer，避免异步拷贝引用已经销毁的 cv::Mat。
- 清理资源使用 delete（TensorRT 8 支持，TensorRT 10 已移除 destroy）。
  模型加载/输入形状/输出布局/数据类型不匹配时明确报错。
- 补上 `ament_index_cpp` 查找及 manifest 依赖、Eigen manifest 依赖。
- 主程序默认从引擎获取输入尺寸；新增可选 `input_size: [width, height]`。
  新增 `YOLO_CONFIG` 环境变量，方便指定设备上的 YAML。
- 保留原有 letterbox、Detection/OBB 后处理、坐标变换、深度处理、相机订阅、
  `/detect_res` 发布、阈值及 OBB 存图逻辑。

## 支持的模型约定

与原程序的后处理匹配：

- 一个输入：FP32、LINEAR、NCHW，形状 `[1,3,H,W]`，BGR8 原图预处理为 RGB/255。
- 一个输出：FP32、LINEAR，Detection `[1,4+类别数,N]`，OBB `[1,5+类别数,N]`。
- 只使用 profile 0、batch=1，支持固定或空间维度动态输入。
- TensorRT 内部 FP16 计算可用，但输入/输出仍需 FP32。
- 不支持带 NMS/end-to-end 输出、多输入、多输出、FP16 I/O、转置 `[1,N,C]`
  或依赖运行时数据才能确定输出大小的引擎；这些需要另外适配后处理。

类别数、Detection/OBB 模式必须与模型对应。原程序类别名称/绘制颜色默认只有一个类别；
若使用多类别模型，还需在 main.cpp 配置对应的 CLASS_NAMES / COLORS。
原相机内参、深度单位、外参未改动，必须与现场相机、分辨率和标定结果对应。

## 1. 环境确认

在 AGX Orin 本机执行（Ubuntu 22.04 / ROS2 Humble 示例）：

```bash
source /opt/ros/humble/setup.bash
bash /你的路径/yolo/scripts/check_orin_env.sh
```

需要：JetPack 匹配的 CUDA/TensorRT 开发包、CMake >=3.18、OpenCV、Eigen3、
yaml-cpp、ament_cmake、rclcpp、sensor_msgs、cv_bridge、message_filters、
ament_index_cpp，以及项目现有的 `msg_det` 包。

不要把桌面端的 x86 `.so` 拷贝到 Orin，也不要随意降级/替换 JetPack 的系统 CUDA。
OpenCV 必须与本机 cv_bridge 使用的版本一致。若同时存在 4.5 和自编译 4.8，
先解决混用；必要时通过 `-DOpenCV_DIR=/匹配版本的/cmake/opencv4` 指定。

## 2. 替换指定源码目录

解压后顶层是 `yolo/`。将其中内容替换进指定目录，其他目录不变。
备份应放到 colcon 工作空间之外，避免 colcon 扫描到两个同名 yolo 包。

- 仓库位置：`visual_perception/src-obb&det/yolo`
- 你日志中的部署位置：`/home/admin/HK/yolo/src/yolo`

路径包含 `&` 时，终端命令必须加引号，例如：

```bash
cd '/你的仓库/visual_perception/src-obb&det/yolo'
```

## 3. 在 Orin 上重新生成 engine

没有提供模型文件，本包不包含 ONNX 或 engine。
不能把桌面显卡生成的 TensorRT engine 当作可移植模型使用。
请取得训练模型导出的 ONNX，再在目标 Orin 上用本机 TensorRT 构建原始 engine。
推荐 trtexec 输出，避免某些框架在 `.engine` 前面附加元数据导致反序列化失败。

固定形状 ONNX 示例：

```bash
mkdir -p /home/admin/HK/models
/usr/src/tensorrt/bin/trtexec \
  --onnx=/home/admin/HK/models/best.onnx \
  --saveEngine=/home/admin/HK/models/best_orin.engine \
  --fp16
```

OBB 对其 ONNX 同样执行，输出例如 `best_obb_orin.engine`。
如果本机 trtexec 位于 PATH，直接使用 `trtexec` 即可。

动态输入需要明确 profile；下面假设真实输入名称为 `images`：

```bash
/usr/src/tensorrt/bin/trtexec \
  --onnx=/home/admin/HK/models/best.onnx \
  --saveEngine=/home/admin/HK/models/best_orin.engine \
  --minShapes=images:1x3x320x320 \
  --optShapes=images:1x3x640x640 \
  --maxShapes=images:1x3x1280x1280 \
  --fp16
```

名称、尺寸范围必须以 ONNX 和模型支持情况为准；固定形状模型不要照搬动态参数。
模型应导出原始 YOLOv8 输出，关闭 NMS/end-to-end 导出。

## 4. 修改配置并编译

编辑 `yolo/config/vision_config.yaml`，至少修改当前模式下的：

```yaml
detector:
  detect_mode: "detection"   # 或 obb
  detection_predictor:
    engine_path: "/home/admin/HK/models/best_orin.engine"
    num_labels: 1
```

这是需修改字段的示意，不能拿它覆盖整个配置；保留已有阈值、offset0 和 coord_trans。
OBB 改 `obb_predictor.engine_path` 和对应类别数。
原配置中的 `/home/zxl/...` 路径被保留以避免猜测模型位置，必须自行改成设备实际路径。

在已有工作空间编译（按你日志中的路径）：

```bash
cd /home/admin/HK/yolo
source /opt/ros/humble/setup.bash
colcon build --packages-up-to yolo --cmake-clean-cache \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DYOLO_BUILD_SMOKE_TEST=ON
source install/setup.bash
```

`--packages-up-to` 会构建工作空间已有的 msg_det 依赖，不会修改其源码。
若 msg_det 在其他工作空间已编译，则先 source 该工作空间的 install/setup.bash。
如果本工作空间还有另一个 yolo 包，应先消除重复包。

## 5. 先验证引擎，再启动相机节点

以下测试不订阅相机、不发布机器人消息，只运行灰色测试图：

```bash
ros2 run yolo yolo_trt_smoke /home/admin/HK/models/best_orin.engine detection 1
# OBB 示例：
ros2 run yolo yolo_trt_smoke /home/admin/HK/models/best_obb_orin.engine obb 1
```

可在末尾追加 `640 640` 测试指定输入尺寸。出现 PASS 表示加载、预热、
预处理、推理、拷贝和后处理已执行；灰色图测试不能验证识别准确率。

启动原节点（通过 SSH / 无显示器时关闭窗口）：

```bash
ros2 run yolo yolo --ros-args -p show_image:=false
```

相机话题不同可指定原有参数：

```bash
ros2 run yolo yolo --ros-args -p show_image:=false \
  -p color_topic:=/你的彩色话题 -p depth_topic:=/你的对齐深度话题
```

默认仍读安装目录的 YAML。也可以不重编译而显式使用源码配置：

```bash
export YOLO_CONFIG=/home/admin/HK/yolo/src/yolo/config/vision_config.yaml
ros2 run yolo yolo --ros-args -p show_image:=false
```

## 验证范围

当前修改环境没有 AGX Orin、ROS2 和你的 engine，因此没有完成目标机链接、
相机联调、实际 GPU 推理或性能测试。已完成的源码检查见 `VALIDATION.md`。
代码出现新的构建或模型错误时，请提供最早的错误、环境报告和引擎 I/O 形状。

## 依据

- TensorRT 8 → 10 C++ 迁移：
  https://docs.nvidia.com/deeplearning/tensorrt/10.x.x/api/tensorrt-8x-to-10x-c-api.html
- Engine 可移植性：
  https://docs.nvidia.com/deeplearning/tensorrt/archives/tensorrt-861/developer-guide/index.html
- Orin GPU 计算能力：
  https://developer.nvidia.com/cuda/gpus
