#!/usr/bin/env bash
# CPU-only logic tests; no real TensorRT, CUDA, ROS or model is exercised.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf -- "$test_dir"' EXIT
for version in 8 10; do
    "${CXX:-g++}" -std=c++17 -g -DNV_TENSORRT_MAJOR="$version" \
        -I"$here/mock" -I"$here/../include" \
        "$here/mock_runtime_test.cpp" -o "$test_dir/runtime_test_$version"
    "$test_dir/runtime_test_$version" "$test_dir/mock.engine"
done
