#!/usr/bin/env bash
# Read-only environment report. Does not install or reconfigure anything.
set -u
printf '\nCPU architecture\n'
uname -m
printf '\nJetson Linux\n'
if [[ -r /etc/nv_tegra_release ]]; then head -n 1 /etc/nv_tegra_release; fi
printf '\nTensorRT packages\n'
dpkg-query -W 'libnvinfer*' 'libnvonnxparsers*' 2>/dev/null || true
printf '\nCUDA compiler\n'
if command -v nvcc >/dev/null; then nvcc --version
elif [[ -x /usr/local/cuda/bin/nvcc ]]; then /usr/local/cuda/bin/nvcc --version; fi
printf '\nTensorRT library architecture\n'
for lib in /usr/lib/aarch64-linux-gnu/libnvinfer.so /usr/lib/aarch64-linux-gnu/libnvinfer_plugin.so; do
    if [[ -e "$lib" ]]; then file -L "$lib"; fi
done
printf '\nROS distribution: %s\n' "${ROS_DISTRO:-not sourced}"
printf '\ntrtexec\n'
command -v trtexec || ls /usr/src/tensorrt/bin/trtexec 2>/dev/null || true
