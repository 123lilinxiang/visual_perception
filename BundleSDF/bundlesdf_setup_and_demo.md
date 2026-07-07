# BundleSDF 环境配置与 Demo 运行指南（第 1–6 步）

本文档描述从宿主机配置、容器编译到跑通 milk 参考数据集的完整流程。  
适用环境：**Ubuntu 22.04 + RTX 4060 8GB + 社区 Docker 镜像 `zhiyuanc/bundlesdf:latest`**。

代码根目录默认为：

```text
/home/shao/code/BundleSDF
```

---

## 第 1 步：宿主机安装 Docker 并配置 GPU 透传

### 做什么

在**宿主机**（不是容器内）安装 Docker Engine，配置 NVIDIA Container Toolkit，使容器内能访问 GPU，并将当前用户加入 `docker` 组。

### 涉及文件

| 文件 | 作用 |
|------|------|
| `scripts/setup_docker_env.sh` | 主脚本：安装 `docker.io`、配置 `nvidia-ctk`、加用户到 `docker` 组、跑 GPU 验证 |
| `scripts/verify_docker_gpu.sh` | 独立验证脚本：pull 测试镜像并在容器内执行 `nvidia-smi` |

### 如何运行

```bash
cd /home/shao/code/BundleSDF

# 需要 root，脚本会自动 sudo
bash scripts/setup_docker_env.sh

# 若刚被加入 docker 组，使权限生效
newgrp docker

# 可选：单独再验一次 GPU
bash scripts/verify_docker_gpu.sh
```

### 脚本内容概要

**`scripts/setup_docker_env.sh`**

- `apt-get install docker.io containerd`
- `nvidia-ctk runtime configure --runtime=docker` 并重启 docker
- 将 `$SUDO_USER` 加入 `docker` 组
- 用 `nvidia/cuda:12.6.0-base-ubuntu22.04` 镜像跑 `nvidia-smi` 验证

**`scripts/verify_docker_gpu.sh`**

- 打印 Docker 版本
- `docker pull` + `docker run --rm --gpus all ... nvidia-smi`

### 成功标志

- 终端输出 `GPU verification passed.` 或 `Docker environment is ready.`
- `docker run --gpus all nvidia/cuda:12.6.0-base-ubuntu22.04 nvidia-smi` 能看到 RTX 4060

---

## 第 2 步：下载模型权重

### 做什么

BundleTrack 的特征匹配（LoFTR）和可选的分割网络（XMem）需要预训练权重。权重**不在** Docker 镜像里，需手动放到本地仓库对应路径。

### 涉及文件

| 文件 | 作用 |
|------|------|
| `BundleTrack/LoFTR/weights/outdoor_ds.ckpt` | LoFTR 户外场景匹配权重（**必需**） |
| `BundleTrack/XMem/saves/XMem-s012.pth` | XMem 分割权重（仅 `--use_segmenter 1` 时需要） |
| `readme.md` | 官方权重下载链接说明（第 27–31 行） |
| `loftr_wrapper.py` | 运行时加载 `outdoor_ds.ckpt` 的 Python 封装 |

### 如何下载与放置

参考 `readme.md` 中的 Google Drive 链接：

1. **LoFTR**（必需）  
   - 下载：[outdoor_ds.ckpt](https://drive.google.com/drive/folders/1xu2Pq6mZT5hmFgiYMBT9Zt8h1yO-3SIp)  
   - 放置：`BundleTrack/LoFTR/weights/outdoor_ds.ckpt`

2. **XMem**（可选，跑 milk demo 不需要）  
   - 下载：[XMem-s012.pth](https://drive.google.com/file/d/1MEZvjbBdNAOF7pXcq6XPQduHeXB50VTc/view?usp=share_link)  
   - 放置：`BundleTrack/XMem/saves/XMem-s012.pth`

### 验证

```bash
ls -lh BundleTrack/LoFTR/weights/outdoor_ds.ckpt
# 文件应存在且大小约数百 MB
```

---

## 第 3 步：启动 Docker 容器

### 做什么

拉取社区预构建镜像，挂载本地代码目录，以交互式 bash 进入容器。镜像提供 PyTorch 1.11 + CUDA 11.3 + conda `py38` 环境，**不包含**预编译的 BundleSDF 原生模块。

### 涉及文件

| 文件 | 作用 |
|------|------|
| `docker/run_container.sh` | 主入口：pull 镜像、删除旧容器、启动新容器 |
| `docker/dockerfile` | 本地自建镜像用（当前流程用社区镜像，一般不需要 build） |
| `scripts/find_bundlesdf_in_image.sh` | 辅助脚本：检查镜像内是否自带 BundleSDF 代码/二进制（结论：需挂载本地代码） |

### 如何运行

```bash
cd /home/shao/code/BundleSDF/docker
bash run_container.sh
```

可选：切换镜像

```bash
BUNDLESDF_IMAGE=ddfl0417/bundlesdf:latest bash run_container.sh
```

### 脚本内容概要

**`docker/run_container.sh`**

- 默认镜像：`zhiyuanc/bundlesdf:latest`（约 12.5GB，首次 pull 较久）
- 挂载：`-v /home:/home`、`-v ${DIR}:${DIR}`（代码目录）
- GPU：`--gpus all`
- 容器名：`bundlesdf`，入口为 `bash`

### 成功标志

- 进入容器提示符，例如：`(py38) root@sly:/home/shao/code/BundleSDF#`
- 容器内 `nvidia-smi` 可见 GPU

---

## 第 4 步：容器内激活 Python 环境

### 做什么

社区镜像的 PyTorch 安装在 conda 环境 **`py38`** 中。必须使用此环境的 Python，**不要**用系统 `/usr/bin/python3`（没有 PyTorch）。

### 涉及文件

| 文件 | 作用 |
|------|------|
| `/opt/conda/envs/py38/bin/python3` | 镜像内带 PyTorch 1.11 的 Python（运行时实际使用） |
| `setup_runtime_env.py` | 设置 `LD_LIBRARY_PATH`、OpenCV/conda lib 路径；由 `run_custom.py` / `bundlesdf.py` 自动调用 |
| `build.sh` | 内置 `detect_python_with_torch()`，优先选 conda `py38` |

### 如何运行

```bash
# 在容器内
conda activate py38
cd /home/shao/code/BundleSDF

# 验证环境
python3 -c "import torch; print(torch.__version__, torch.cuda.is_available())"
# 期望输出类似：1.11.0+cu113 True
```

### 注意事项

- 每次新开容器都要 `conda activate py38`
- 运行 BundleSDF 请通过 `python3 run_custom.py ...` 或 `bash scripts/run_milk_demo.sh`，不要裸 `import my_cpp`（需先配置运行时库路径）

---

## 第 5 步：编译原生模块（build.sh）

### 做什么

在容器内对**挂载的本地代码**编译 CUDA/C++ 扩展，生成 Python 可调用的 `.so` 文件。社区镜像只提供依赖环境，**必须**执行此步才能运行 tracking / NeRF。

编译内容：

1. **`mycuda`** — NeRF 相关 CUDA 算子（`pip install -e .`）
2. **`BundleTrack/build/my_cpp.*.so`** — BundleTrack C++ 核心（CMake + make）

若缺少带 CUDA 的 OpenCV，会先触发 OpenCV 编译（约 30–60 分钟）。

### 涉及文件

| 文件 | 作用 |
|------|------|
| `build.sh` | **主编译脚本**：检测 Python/CUDA/OpenCV，编译 mycuda + BundleTrack |
| `mycuda/pyproject.toml` | mycuda 包配置（torch 版本约束已放宽以兼容 PyTorch 1.11） |
| `mycuda/setup.py` | mycuda CUDA 扩展编译入口 |
| `BundleTrack/CMakeLists.txt` | BundleTrack CMake 配置（CUDA 标准、yaml-cpp 链接等） |
| `BundleTrack/src/*.cpp` | BundleTrack C++ 源码 |
| `scripts/find_opencv_cuda.sh` | 检测是否已安装 OpenCV CUDA |
| `scripts/build_opencv_cuda.sh` | 从源码编译 OpenCV 4.11 + CUDA 到 `~/opt/opencv-cuda-4.11.0` |
| `setup_runtime_env.py` | 编译完成后运行时加载 `my_cpp.so` 所需的库路径配置 |

### 如何运行

```bash
# 在容器内，已 conda activate py38
cd /home/shao/code/BundleSDF
bash build.sh
```

若 OpenCV CUDA 不存在，`build.sh` 会报错并提示先执行：

```bash
bash scripts/build_opencv_cuda.sh   # 首次约 30–60 分钟
bash build.sh                       # 再编译 BundleSDF
```

### 脚本内容概要

**`build.sh` 主要流程**

1. `detect_python_with_torch()` — 选 conda `py38` 的 Python
2. `configure_torch_cuda_arch_list()` — PyTorch 1.x 下设 `TORCH_CUDA_ARCH_LIST=7.5;8.0;8.6`（兼容 RTX 4060）
3. 设置 `CUDA_HOME`、`LD_LIBRARY_PATH`
4. `cd mycuda && pip install -e .`
5. `cd BundleTrack/build && cmake .. && make -j$(nproc)`
6. 依赖检测：OpenCV CUDA（含 `cudaimgproc.hpp`）、yaml-cpp、PCL、pybind11

### 成功标志

```bash
# my_cpp 已生成
ls BundleTrack/build/my_cpp.cpython-*.so

# mycuda 可导入
python3 -c "from mycuda import common; print('mycuda OK')"

# 通过 run_custom 间接验证 my_cpp（推荐）
python3 -c "
from setup_runtime_env import ensure_runtime_env
ensure_runtime_env('/home/shao/code/BundleSDF')
import my_cpp
print('my_cpp OK')
"
```

---

## 步骤关系一览

```text
宿主机                          容器内 (py38)
────────                        ──────────────
[1] setup_docker_env.sh
    verify_docker_gpu.sh
         │
[2] 下载权重到本地仓库
         │
[3] docker/run_container.sh ──► 进入 bash
                                     │
                                [4] conda activate py38
                                     │
                                [5] bash build.sh
                                     │
                                [6] run_milk_demo.sh
                                     │
                                输出 mesh / 位姿 / 可视化
```

---

## 第 6 步：运行 milk Demo

### 做什么

用官方参考数据集 **milk** 跑通 BundleSDF 完整流程：

1. **Tracking + 在线 NeRF** — 逐帧跟踪物体位姿，同步重建 SDF
2. **Global refine** — 全局 NeRF 优化 + 网格贴纹理
3. **Draw pose**（可选）— 在 RGB 图上绘制 3D 包围盒可视化位姿

### 涉及文件

| 文件 | 作用 |
|------|------|
| `scripts/run_milk_demo.sh` | **一键 Demo 脚本**：检查编译、设置低显存参数、依次跑三步 |
| `run_custom.py` | Demo 主入口，支持 `--mode run_video / global_refine / draw_pose` |
| `bundlesdf.py` | BundleSdf 核心：tracking、LoFTR 匹配、NeRF 子进程、全局 refine |
| `loftr_wrapper.py` | LoFTR 特征匹配封装（加载权重、batch、cuDNN 开关） |
| `nerf_runner.py` | NeRF 训练与 mesh / 纹理导出 |
| `config.yml` | NeRF 默认配置模板 |
| `BundleTrack/config_ho3d.yml` | BundleTrack 默认配置模板 |
| `BundleTrack/scripts/data_reader.py` | `YcbineoatReader`：读取 RGB-D 序列 |
| `debug/2022-11-18-15-10-24_milk/` | milk 参考数据集（输入） |
| `debug/bundlesdf_milk_out/` | Demo 默认输出目录 |

### 输入数据集结构

milk 数据集位于 `debug/2022-11-18-15-10-24_milk/`，格式与 `readme.md` 一致：

```text
debug/2022-11-18-15-10-24_milk/
├── cam_K.txt          # 3×3 相机内参
├── rgb/*.png          # 彩色图
├── depth/*.png        # 深度图（16-bit mm）
└── masks/*.png        # 物体 mask（demo 用 GT mask，不用分割网络）
```

`run_milk_demo.sh` 会检查 `cam_K.txt` 是否存在。

### 如何运行

**推荐：一键脚本（在容器内，已完成第 4–5 步）**

```bash
conda activate py38
cd /home/shao/code/BundleSDF
bash scripts/run_milk_demo.sh
```

**自定义路径 / 参数**

```bash
VIDEO_DIR=/path/to/your/data \
OUT_DIR=/path/to/output \
STRIDE=10 \
bash scripts/run_milk_demo.sh
```

**手动分步运行（等价于脚本三步）**

```bash
conda activate py38
cd /home/shao/code/BundleSDF

# 8GB 显存建议的环境变量
export LOFTR_BATCH_SIZE=1
export LOFTR_ONE_PAIR_AT_A_TIME=1
export BUNDLESDF_DISABLE_CUDNN=1

# Step 6a: tracking + 在线 NeRF（结束时会自动再跑一次 global refine）
python3 run_custom.py \
  --mode run_video \
  --video_dir debug/2022-11-18-15-10-24_milk \
  --out_folder debug/bundlesdf_milk_out \
  --use_segmenter 0 \
  --use_gui 0 \
  --debug_level 2 \
  --stride 10

# Step 6b: 全局 NeRF refine + 贴纹理
python3 run_custom.py \
  --mode global_refine \
  --video_dir debug/2022-11-18-15-10-24_milk \
  --out_folder debug/bundlesdf_milk_out

# Step 6c: 位姿可视化（可选）
python3 run_custom.py \
  --mode draw_pose \
  --out_folder debug/bundlesdf_milk_out
```

### 脚本与命令参数说明

**`scripts/run_milk_demo.sh` 流程**

1. 自动检测带 PyTorch 的 Python（优先 conda `py38`）
2. 若 `my_cpp.so` 或 `mycuda` 未编译，自动触发 `build.sh`
3. 设置低显存环境变量（见下表）
4. 依次调用 `run_custom.py` 三个 mode

**`run_custom.py` 常用参数**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--mode` | `run_video` | `run_video` / `global_refine` / `draw_pose` |
| `--video_dir` | `debug/2022-11-18-15-10-24_milk` | 输入 RGB-D 序列目录 |
| `--out_folder` | `debug/bundlesdf_2022-11-18-15-10-24_milk` | 输出目录（脚本覆盖为 `bundlesdf_milk_out`） |
| `--stride` | `1` | 帧间隔；demo 脚本默认 `10`，减少帧数与显存占用 |
| `--use_segmenter` | `0` | `1` 启用 XMem 分割（需 XMem 权重） |
| `--use_gui` | `1` | `0` 关闭 GUI（Docker 内建议关） |
| `--debug_level` | `2` | 日志详细程度 |

**8GB 显存相关环境变量（`run_milk_demo.sh` 默认已设置）**

| 变量 | 默认值 | 作用 |
|------|--------|------|
| `LOFTR_BATCH_SIZE` | `1` | LoFTR 每次 forward 的 batch 大小 |
| `LOFTR_ONE_PAIR_AT_A_TIME` | `1` | 多 frame pair 时逐对匹配，避免 OOM |
| `BUNDLESDF_DISABLE_CUDNN` | `1` | 禁用 cuDNN（RTX 40xx + torch 1.11 兼容） |
| `STRIDE` | `10` | 传给 `--stride`，每隔 10 帧处理一帧 |

**`run_custom.py` 内针对 8GB 的 NeRF 参数（`run_video` 模式）**

- `start_nerf_keyframes=20` — 累积 20 个 keyframe 后再启动 NeRF 子进程
- `N_rand=1024`、`netchunk=3276800` — 降低 NeRF 采样显存

### 各 mode 做什么

**`--mode run_video`（对应 `run_one_video()`）**

1. 清空并创建 `--out_folder`
2. 从 `BundleTrack/config_ho3d.yml` 和 `config.yml` 生成 tracking / NeRF 配置
3. 创建 `BundleSdf` tracker，按 `--stride` 逐帧读入 RGB-D + mask
4. 每帧：LoFTR 特征匹配 → Bundle adjustment → 在线 NeRF 更新
5. 结束后调用 `run_one_video_global_nerf()` 做第一次全局 refine

**`--mode global_refine`（对应 `run_one_video_global_nerf()`）**

1. 读取上一步生成的 `config_bundletrack.yml`、`config_nerf.yml`
2. 提高 NeRF 训练步数（`n_step=2000`）和 mesh 精度
3. 全局 NeRF 优化，导出带纹理 mesh（`textured_mesh.obj`）
4. 终端打印 `Done`

**`--mode draw_pose`（对应 `draw_pose()`）**

1. 读取 `out_folder/textured_mesh.obj`、 `cam_K.txt`、`color/*.png`、`ob_in_cam/*.txt`
2. 在每帧 RGB 上绘制 3D 包围盒
3. 保存到 `out_folder/pose_vis/`

### 输出目录结构

运行完成后，`debug/bundlesdf_milk_out/` 主要包含：

```text
debug/bundlesdf_milk_out/
├── textured_mesh.obj          # 带纹理 mesh（主要结果）
├── material.mtl               # 纹理材质
├── mesh_cleaned.obj           # 清理后的 mesh
├── cam_K.txt                  # 相机内参（tracking 时写入）
├── config_bundletrack.yml     # tracking 配置
├── config_nerf.yml            # NeRF 配置
├── color/                     # 各帧 RGB（Bundler 保存）
├── ob_in_cam/                 # 各帧物体位姿（4×4 txt）
├── pose_vis/                  # 位姿可视化图（跑 draw_pose 后）
├── nerf_with_bundletrack_online/  # 在线 NeRF 中间结果
├── final/nerf/                # 全局 refine 结果
└── <frame_id>/                # 每帧 tracking 调试输出
```

### 成功标志

- 终端最后出现 **`Done`**，并回到 shell 提示符（无 Traceback）
- 存在 **`debug/bundlesdf_milk_out/textured_mesh.obj`**
- 存在 **`debug/bundlesdf_milk_out/ob_in_cam/*.txt`**

以下日志**不是崩溃**：

| 日志 | 含义 |
|------|------|
| `RuntimeWarning: invalid value encountered in cast`（`nerf_runner.py`） | 贴纹理时少量 NaN 像素，警告可忽略 |
| `[Bundler.cpp:64] Destructor` | C++ 对象正常析构 |
| `after ransac ... too few matches` | 部分帧 pair RANSAC 内点不足，会自动换 ref 或跳过 |

### 常见问题（Demo 阶段）

| 问题 | 处理 |
|------|------|
| `CUDA out of memory` | 保持 `LOFTR_BATCH_SIZE=1`；增大 `STRIDE=20`；确认 `start_nerf_keyframes=20` |
| 程序停在 `(Pdb)` | 代码中有调试断点，输入 `c` 继续；当前仓库已移除 `pdb.set_trace()` |
| `draw_pose` 找不到 `color/` | 需先跑完 `run_video`；`color/` 由 BundleTrack 在 tracking 时写入 |
| 运行很慢 | `--stride 10` 约处理 1/10 帧；全帧可设 `STRIDE=1`（8GB 易 OOM） |
| 再次跑 demo 覆盖旧结果 | `run_video` 开头会 `rm -rf out_folder`，注意备份 |

### 预计耗时（RTX 4060 8GB，stride=10）

- Step 6a（run_video）：约数十分钟到 1+ 小时（视帧数而定）
- Step 6b（global_refine）：约 20–40 分钟
- Step 6c（draw_pose）：几分钟

---

## 完整流程速查

```bash
# ── 宿主机 ──
cd /home/zxl/project/BundleSDF
bash scripts/setup_docker_env.sh
newgrp docker

# 下载权重到 BundleTrack/LoFTR/weights/outdoor_ds.ckpt

cd docker && bash run_container.sh

# ── 容器内 ──
conda activate py38
cd /home/zxl/project/BundleSDF
bash build.sh
bash scripts/run_milk_demo.sh
```
cd ~/project/BundleSDF/BundleTrack

rm -rf build
mkdir build
cd build

cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DOpenCV_DIR=/usr/local/share/opencv-4.8.0/lib/cmake/opencv4 \
  -DCMAKE_CXX_FLAGS="-O0 -g -fno-omit-frame-pointer"

make -j8