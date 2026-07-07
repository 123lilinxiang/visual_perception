#!/usr/bin/env bash
set -euo pipefail

CUDA_IMAGE="nvidia/cuda:12.6.0-base-ubuntu22.04"

echo "========== Docker + NVIDIA GPU environment setup =========="

# 必须 root 执行；如果不是 root，就自动用 sudo 重新执行
if [[ "${EUID}" -ne 0 ]]; then
  exec sudo -E bash "$0" "$@"
fi

# 找到真正要加入 docker 组的用户
TARGET_USER="${SUDO_USER:-}"
if [[ -z "${TARGET_USER}" || "${TARGET_USER}" == "root" ]]; then
  TARGET_USER="$(logname 2>/dev/null || echo "")"
fi

echo "[1/7] Checking NVIDIA driver..."
if ! command -v nvidia-smi >/dev/null 2>&1; then
  echo "ERROR: 当前系统找不到 nvidia-smi。"
  echo "请先安装 NVIDIA 显卡驱动，并确认宿主机能直接运行：nvidia-smi"
  exit 1
fi

nvidia-smi

echo "[2/7] Installing Docker and containerd..."
apt-get update
apt-get install -y \
  docker.io \
  containerd \
  ca-certificates \
  curl \
  gnupg2

echo "[3/7] Enabling Docker service..."
systemctl enable --now docker
systemctl enable --now containerd

echo "[4/7] Installing NVIDIA Container Toolkit..."

# 配置 NVIDIA Container Toolkit apt 源
install -d -m 0755 /usr/share/keyrings

curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey \
  | gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg

curl -s -L https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list \
  | sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' \
  > /etc/apt/sources.list.d/nvidia-container-toolkit.list

apt-get update
apt-get install -y nvidia-container-toolkit

echo "[5/7] Configuring NVIDIA runtime for Docker..."
nvidia-ctk runtime configure --runtime=docker

echo "[6/7] Restarting Docker..."
systemctl restart docker

echo "[7/7] Adding user to docker group..."
if [[ -n "${TARGET_USER}" && "${TARGET_USER}" != "root" ]]; then
  usermod -aG docker "${TARGET_USER}"
  echo "User '${TARGET_USER}' has been added to docker group."
  echo "注意：需要重新登录终端，docker 组权限才会对该用户生效。"
else
  echo "没有检测到普通用户，跳过加入 docker 组。"
fi

echo "========== Verifying Docker GPU =========="

docker --version

echo "Pulling CUDA image: ${CUDA_IMAGE}"
docker pull "${CUDA_IMAGE}"

echo "Running nvidia-smi inside Docker..."
docker run --rm --gpus all "${CUDA_IMAGE}" nvidia-smi

echo "Docker environment is ready."
echo "GPU verification passed."