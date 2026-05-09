#!/bin/bash

set -e

# 获取脚本所在目录，并切到该目录
WORK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$WORK_DIR"

echo "===== Source ROS2 environment ====="
source /opt/ros/humble/setup.bash

# source 当前工作目录下的 install
if [ -f "install/setup.bash" ]; then
    source install/setup.bash
else
    echo "错误：未找到 install/setup.bash，请先在当前工作目录编译。"
    exit 1
fi

echo "===== Starting RealSense camera ====="

ros2 launch realsense2_camera rs_launch.py \
  serial_no:=_337122071011 \
  enable_color:=true \
  enable_depth:=true \
  depth_module.depth_profile:=640x480x30 \
  rgb_camera.color_profile:=640x480x30 \
  align_depth.enable:=true &

REALSENSE_PID=$!

echo "RealSense PID: $REALSENSE_PID"

echo "===== Waiting for RealSense topics ====="

# 等待 aligned depth 话题出现，最多等 15 秒
for i in {1..15}; do
    if ros2 topic list | grep -q "/camera/camera/aligned_depth_to_color/image_raw"; then
        echo "RealSense aligned depth topic is ready."
        break
    fi

    echo "Waiting for RealSense topic... $i"
    sleep 1
done

echo "===== Starting YOLOv8 OBB node ====="

ros2 launch yolov8_obb yolov8_obb.launch.xml &

YOLO_PID=$!

echo "YOLOv8 OBB PID: $YOLO_PID"

trap "echo 'Stopping...'; kill $YOLO_PID $REALSENSE_PID 2>/dev/null; exit 0" SIGINT SIGTERM

wait