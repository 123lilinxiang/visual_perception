#!/usr/bin/env bash
set -e

ROOT=$(pwd)

# 必须先激活 conda 环境
source ~/anaconda3/etc/profile.d/conda.sh
conda activate bundlesdf

# 不要用系统 Python / ROS / usr/local 里的 Python 包
unset PYTHONPATH
unset TORCH_LIBRARIES

# CUDA 统一用 /usr/local/cuda-12
export CUDA_HOME=/usr/local/cuda-12
export CUDA_PATH=/usr/local/cuda-12
export PATH="$CUDA_HOME/bin:$CONDA_PREFIX/bin:/usr/bin:/bin"
export LD_LIBRARY_PATH="$CONDA_PREFIX/lib:$CUDA_HOME/lib64"

# RTX 4090 用 8.9，不要写 9.0
export TORCH_CUDA_ARCH_LIST="8.9"
export FORCE_CUDA=1
export TORCH_EXTENSIONS_DIR="/tmp/torch_extensions"

echo "================ Python ================"
which python
python -V

echo "================ Torch ================"
python - <<'PY'
import torch
print("torch file:", torch.__file__)
print("torch version:", torch.__version__)
print("torch cuda:", torch.version.cuda)
print("cuda available:", torch.cuda.is_available())
PY

echo "================ CUDA ================"
echo "CUDA_HOME=$CUDA_HOME"
nvcc --version

echo "================ NumPy / cv2 ================"
python -m pip install "numpy==1.26.4" ninja wheel setuptools
python - <<'PY'
import numpy as np
print("numpy:", np.__version__)
try:
    import cv2
    print("cv2:", cv2.__version__)
except Exception as e:
    print("cv2 import failed:", repr(e))
PY

echo "================ Build mycuda ================"
cd "$ROOT/mycuda"

rm -rf build *.egg-info
find . -name "*.so" -delete

# 避免 pyproject.toml 触发 pip 隔离构建，偷偷装 CUDA 13.0 的 torch
if [ -f pyproject.toml ]; then
  mv pyproject.toml pyproject.toml.bak
fi

python setup.py develop

echo "================ Test mycuda ================"
cd "$ROOT"
python - <<'PY'
import torch
print("torch:", torch.__version__, torch.version.cuda)

try:
    import gridencoder
    print("gridencoder OK")
except Exception as e:
    print("gridencoder import failed:", repr(e))

try:
    from mycuda import common
    print("mycuda.common OK")
except Exception as e:
    print("mycuda.common import failed:", repr(e))
PY

echo "================ Build BundleTrack ================"
cd "$ROOT/BundleTrack"

rm -rf build
mkdir build
cd build

cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DOpenCV_DIR=/usr/local/share/opencv-4.8.0/lib/cmake/opencv4 \
  -DCMAKE_CXX_FLAGS="-O0 -g -fno-omit-frame-pointer"

make -j1

echo "================ Test my_cpp ================"
cd "$ROOT"
python - <<'PY'
import sys, os
sys.path.append(os.path.join(os.getcwd(), "BundleTrack/build"))
import my_cpp
print("my_cpp:", my_cpp.__file__)
print("my_cpp OK")
PY

echo "================ Done ================"