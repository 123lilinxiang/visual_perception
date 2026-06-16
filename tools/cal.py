#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Eye-to-hand hand-eye calibration for a fixed camera and a chessboard rigidly
mounted on the robot end-effector.

Coordinate convention used by this program
------------------------------------------
base_T_end    : end/tool frame -> robot base frame
camera_T_board: chessboard frame -> camera frame (returned by solvePnP)
base_T_camera : camera frame -> robot base frame (final result)

The rigid-chain equation is:
    base_T_end(i) @ end_T_board
        = base_T_camera @ camera_T_board(i)

OpenCV calibrateHandEye is reused for eye-to-hand by feeding end_T_base(i)
(the inverse of base_T_end(i)). Its output is then base_T_camera.

Default ROKAE pose format per line:
    X Y Z Rx Ry Rz
where X/Y/Z are metres, Rx/Ry/Rz are radians, and
    R = Rz(Rz) @ Ry(Ry) @ Rx(Rx)
which matches ROKAE Utils::postureToTransArray().
"""

from __future__ import annotations

import argparse
import glob
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

import cv2
import numpy as np


# Camera intrinsics previously read from the Gemini camera at 1280x720.
DEFAULT_FX = 608.5936
DEFAULT_FY = 608.7183
DEFAULT_CX = 640.2278
DEFAULT_CY = 360.9166
DEFAULT_DIST = [
    -0.02925739,
    0.03408520,
    0.00013774,
    -0.00010824,
    -0.01204987,
    0.0,
    0.0,
    0.0,
]

FLOAT_PATTERN = re.compile(
    r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
)


@dataclass
class Observation:
    index: int
    image_path: Path
    base_T_end: np.ndarray
    camera_T_board: np.ndarray
    reprojection_rmse_px: float


@dataclass
class MethodResult:
    name: str
    base_T_camera: np.ndarray
    translation_rms_mm: float
    rotation_rms_deg: float
    consistency_score: float
    end_T_board_mean: np.ndarray


def natural_key(path: str | Path) -> list[object]:
    """Natural sort: rgb_2.png comes before rgb_10.png."""
    text = str(path)
    return [int(x) if x.isdigit() else x.lower() for x in re.split(r"(\d+)", text)]


def make_transform(R: np.ndarray, t: Sequence[float]) -> np.ndarray:
    T = np.eye(4, dtype=np.float64)
    T[:3, :3] = np.asarray(R, dtype=np.float64).reshape(3, 3)
    T[:3, 3] = np.asarray(t, dtype=np.float64).reshape(3)
    return T


def invert_transform(T: np.ndarray) -> np.ndarray:
    R = T[:3, :3]
    t = T[:3, 3]
    T_inv = np.eye(4, dtype=np.float64)
    T_inv[:3, :3] = R.T
    T_inv[:3, 3] = -R.T @ t
    return T_inv


def rot_x(a: float) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]], dtype=np.float64)


def rot_y(a: float) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], dtype=np.float64)


def rot_z(a: float) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]], dtype=np.float64)


def rokae_rpy_to_rotation(rx: float, ry: float, rz: float) -> np.ndarray:
    """ROKAE posture: R = Rz(rz) * Ry(ry) * Rx(rx)."""
    return rot_z(rz) @ rot_y(ry) @ rot_x(rx)


def project_to_so3(M: np.ndarray) -> np.ndarray:
    """Project a near-rotation matrix onto SO(3)."""
    U, _, Vt = np.linalg.svd(M)
    R = U @ Vt
    if np.linalg.det(R) < 0:
        U[:, -1] *= -1
        R = U @ Vt
    return R


def rotation_angle_deg(R: np.ndarray) -> float:
    cos_angle = (np.trace(R) - 1.0) / 2.0
    return math.degrees(math.acos(float(np.clip(cos_angle, -1.0, 1.0))))


def parse_named_pose(line: str) -> list[float] | None:
    """Parse strings such as x=..., y=..., z=..., rx=..., ry=..., rz=...."""
    result: dict[str, float] = {}
    for key in ("x", "y", "z", "rx", "ry", "rz"):
        match = re.search(
            rf"(?<![a-zA-Z]){key}\s*[:=]\s*({FLOAT_PATTERN.pattern})",
            line,
            flags=re.IGNORECASE,
        )
        if match:
            result[key] = float(match.group(1))
    if len(result) == 6:
        return [result[k] for k in ("x", "y", "z", "rx", "ry", "rz")]
    return None


def read_pose_file(
    pose_file: Path,
    length_unit: str,
    angle_unit: str,
    orientation_format: str,
) -> list[np.ndarray]:
    """
    Read one pose per non-empty line.

    Accepted line forms:
      1) X Y Z Rx Ry Rz
      2) X,Y,Z,Rx,Ry,Rz
      3) x=... y=... z=... rx=... ry=... rz=...
      4) timestamp followed by pose values; the last six numbers are used.
    """
    try:
        raw = pose_file.read_bytes()
    except OSError as exc:
        raise RuntimeError(f"无法读取末端位姿文件: {pose_file}\n{exc}") from exc

    nul_ratio = raw.count(b"\x00") / max(len(raw), 1)
    if nul_ratio > 0.01:
        raise RuntimeError(
            f"末端位姿文件不是可解析的纯文本：{pose_file}\n"
            f"文件大小 {len(raw)} 字节，其中 NUL 字节比例为 {nul_ratio:.1%}。\n"
            "请重新保存成 UTF-8 文本，每行 6 个数：X Y Z Rx Ry Rz。"
        )

    text: str | None = None
    for encoding in ("utf-8-sig", "utf-8", "gb18030"):
        try:
            text = raw.decode(encoding)
            break
        except UnicodeDecodeError:
            continue
    if text is None:
        raise RuntimeError(
            f"末端位姿文件无法按 UTF-8/GB18030 解码：{pose_file}"
        )

    length_scale = 1.0 if length_unit == "m" else 0.001
    angle_scale = 1.0 if angle_unit == "rad" else math.pi / 180.0

    transforms: list[np.ndarray] = []
    for line_number, original_line in enumerate(text.splitlines(), start=1):
        line = original_line.strip()
        if not line or line.startswith(("#", "//", ";")):
            continue

        # Remove trailing comments.
        line = line.split("#", 1)[0].split("//", 1)[0].strip()
        values = parse_named_pose(line)
        if values is None:
            numbers = [float(x) for x in FLOAT_PATTERN.findall(line)]
            if len(numbers) < 6:
                print(
                    f"[警告] 第 {line_number} 行少于 6 个数，已跳过: {original_line}",
                    file=sys.stderr,
                )
                continue
            values = numbers[-6:]

        x, y, z, a, b, c = values
        t = np.array([x, y, z], dtype=np.float64) * length_scale
        angles = np.array([a, b, c], dtype=np.float64) * angle_scale

        if orientation_format == "rokae_rpy":
            R = rokae_rpy_to_rotation(*angles)
        elif orientation_format == "rotvec":
            R, _ = cv2.Rodrigues(angles.reshape(3, 1))
        else:
            raise AssertionError(f"未知姿态格式: {orientation_format}")

        transforms.append(make_transform(R, t))

    if not transforms:
        raise RuntimeError(
            f"没有从 {pose_file} 中读到任何有效位姿。\n"
            "正确示例：0.135976 -0.633666 0.264208 -2.5656 1.2566 -0.9599"
        )
    return transforms


def collect_images(input_value: str) -> list[Path]:
    path = Path(input_value).expanduser()
    if path.is_dir():
        candidates: list[Path] = []
        for pattern in ("*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tif", "*.tiff"):
            candidates.extend(path.glob(pattern))
    else:
        candidates = [Path(p) for p in glob.glob(input_value)]
    return sorted({p.resolve() for p in candidates}, key=natural_key)


def make_board_points(cols: int, rows: int, square_size_m: float) -> np.ndarray:
    obj = np.zeros((rows * cols, 3), dtype=np.float64)
    grid = np.mgrid[0:cols, 0:rows].T.reshape(-1, 2)
    obj[:, :2] = grid * square_size_m
    return obj


def detect_chessboard(
    gray: np.ndarray,
    pattern_size: tuple[int, int],
    detector: str,
) -> tuple[bool, np.ndarray | None]:
    if detector == "sb" and hasattr(cv2, "findChessboardCornersSB"):
        flags = (
            cv2.CALIB_CB_NORMALIZE_IMAGE
            | cv2.CALIB_CB_EXHAUSTIVE
            | cv2.CALIB_CB_ACCURACY
        )
        ok, corners = cv2.findChessboardCornersSB(gray, pattern_size, flags=flags)
        if ok:
            return True, np.ascontiguousarray(corners, dtype=np.float64)

    flags = (
        cv2.CALIB_CB_ADAPTIVE_THRESH
        | cv2.CALIB_CB_NORMALIZE_IMAGE
        | cv2.CALIB_CB_FILTER_QUADS
    )
    ok, corners = cv2.findChessboardCorners(gray, pattern_size, flags=flags)
    if not ok:
        return False, None

    criteria = (
        cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_MAX_ITER,
        60,
        1e-4,
    )
    refined = cv2.cornerSubPix(gray, corners, (11, 11), (-1, -1), criteria)
    return True, np.ascontiguousarray(refined, dtype=np.float64)


def scale_camera_matrix(
    K_ref: np.ndarray,
    image_size: tuple[int, int],
    reference_size: tuple[int, int],
) -> np.ndarray:
    width, height = image_size
    ref_width, ref_height = reference_size
    sx = width / ref_width
    sy = height / ref_height
    if abs(sx - sy) > 0.02:
        print(
            "[警告] 当前图像与内参标定分辨率的宽高缩放比例不同；"
            "请确认图像没有被裁剪。",
            file=sys.stderr,
        )
    K = K_ref.copy()
    K[0, 0] *= sx
    K[0, 2] *= sx
    K[1, 1] *= sy
    K[1, 2] *= sy
    return K


def reprojection_rmse(
    object_points: np.ndarray,
    image_points: np.ndarray,
    rvec: np.ndarray,
    tvec: np.ndarray,
    K: np.ndarray,
    dist: np.ndarray,
) -> float:
    projected, _ = cv2.projectPoints(object_points, rvec, tvec, K, dist)
    delta = projected.reshape(-1, 2) - image_points.reshape(-1, 2)
    return float(np.sqrt(np.mean(np.sum(delta * delta, axis=1))))


def acquire_observations(
    images: Sequence[Path],
    poses: Sequence[np.ndarray],
    object_points: np.ndarray,
    pattern_size: tuple[int, int],
    K_ref: np.ndarray,
    dist: np.ndarray,
    reference_size: tuple[int, int],
    detector: str,
    debug_dir: Path,
    max_reprojection_error_px: float,
) -> tuple[list[Observation], np.ndarray, tuple[int, int]]:
    observations: list[Observation] = []
    debug_dir.mkdir(parents=True, exist_ok=True)

    first_size: tuple[int, int] | None = None
    K_used: np.ndarray | None = None

    for index, (image_path, base_T_end) in enumerate(zip(images, poses), start=1):
        image = cv2.imread(str(image_path), cv2.IMREAD_COLOR)
        if image is None:
            print(f"[跳过] 无法读取图像: {image_path}", file=sys.stderr)
            continue

        height, width = image.shape[:2]
        current_size = (width, height)
        if first_size is None:
            first_size = current_size
            K_used = scale_camera_matrix(K_ref, current_size, reference_size)
        elif current_size != first_size:
            raise RuntimeError(
                f"图像分辨率不一致：第一张是 {first_size}，{image_path.name} 是 {current_size}"
            )

        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        found, corners = detect_chessboard(gray, pattern_size, detector)
        if not found or corners is None:
            print(f"[跳过] 第 {index:02d} 组未检测到棋盘格: {image_path.name}")
            cv2.imwrite(str(debug_dir / f"{index:03d}_not_found.jpg"), image)
            continue

        assert K_used is not None
        ok, rvec, tvec = cv2.solvePnP(
            np.ascontiguousarray(object_points, dtype=np.float64),
            np.ascontiguousarray(corners, dtype=np.float64),
            K_used,
            dist,
            flags=cv2.SOLVEPNP_ITERATIVE,
        )
        if not ok:
            print(f"[跳过] 第 {index:02d} 组 solvePnP 失败: {image_path.name}")
            continue

        rmse = reprojection_rmse(object_points, corners, rvec, tvec, K_used, dist)
        if rmse > max_reprojection_error_px:
            print(
                f"[跳过] 第 {index:02d} 组重投影误差过大: "
                f"{rmse:.3f} px > {max_reprojection_error_px:.3f} px"
            )
            continue

        R_camera_board, _ = cv2.Rodrigues(rvec)
        camera_T_board = make_transform(R_camera_board, tvec.reshape(3))

        annotated = image.copy()
        expected_corner_count = pattern_size[0] * pattern_size[1]

        if corners is not None:
            corners_draw = np.asarray(corners, dtype=np.float32)

            # 统一转换为 OpenCV 要求的 (N, 1, 2)
            if corners_draw.size == expected_corner_count * 2:
                corners_draw = corners_draw.reshape(expected_corner_count, 1, 2)

                # 防止 np.flip、[::-1] 等操作产生负步长或非连续内存
                corners_draw = np.ascontiguousarray(corners_draw)

                if np.all(np.isfinite(corners_draw)):
                    cv2.drawChessboardCorners(
                        annotated,
                        pattern_size,
                        corners_draw,
                        True,
                    )
                else:
                    print("警告：角点中包含 NaN 或 Inf，跳过角点绘制")
            else:
                print(
                    f"警告：角点数量不正确，"
                    f"期望 {expected_corner_count} 个，"
                    f"实际数组形状={corners_draw.shape}，"
                    f"元素数量={corners_draw.size}"
                )
        else:
            print("警告：corners 为 None，跳过角点绘制")

        cv2.drawFrameAxes(
            annotated,
            K_used,
            dist,
            rvec,
            tvec,
            float(np.linalg.norm(object_points[1] - object_points[0]) * 3.0),
            3,
        )
        cv2.putText(
            annotated,
            f"#{index} reproj={rmse:.3f}px",
            (20, 35),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.9,
            (0, 255, 0),
            2,
            cv2.LINE_AA,
        )
        # Mark the detector's corner 0, useful for spotting 180-degree ordering flips.
        p0 = tuple(np.round(corners.reshape(-1, 2)[0]).astype(int))
        cv2.circle(annotated, p0, 10, (0, 0, 255), -1)
        cv2.putText(
            annotated,
            "corner 0",
            (p0[0] + 12, p0[1]),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.6,
            (0, 0, 255),
            2,
            cv2.LINE_AA,
        )
        cv2.imwrite(str(debug_dir / f"{index:03d}_ok.jpg"), annotated)

        observations.append(
            Observation(
                index=index,
                image_path=image_path,
                base_T_end=base_T_end,
                camera_T_board=camera_T_board,
                reprojection_rmse_px=rmse,
            )
        )
        print(
            f"[有效] 第 {index:02d} 组 {image_path.name}: "
            f"重投影误差 {rmse:.3f} px, 棋盘距离 {tvec[2, 0]:.4f} m"
        )

    if first_size is None or K_used is None:
        raise RuntimeError("没有成功读取任何图像。")
    return observations, K_used, first_size


def mean_transform(transforms: Sequence[np.ndarray]) -> np.ndarray:
    R_sum = np.zeros((3, 3), dtype=np.float64)
    translations = []
    for T in transforms:
        R_sum += T[:3, :3]
        translations.append(T[:3, 3])
    R_mean = project_to_so3(R_sum)
    t_mean = np.mean(np.stack(translations), axis=0)
    return make_transform(R_mean, t_mean)


def evaluate_result(
    base_T_camera: np.ndarray,
    observations: Sequence[Observation],
) -> tuple[float, float, float, np.ndarray]:
    # Since the board is rigidly attached to the end frame, every end_T_board
    # computed below should be identical.
    end_T_boards = [
        invert_transform(obs.base_T_end)
        @ base_T_camera
        @ obs.camera_T_board
        for obs in observations
    ]
    mean_T = mean_transform(end_T_boards)

    translation_errors_m = []
    rotation_errors_deg = []
    for T in end_T_boards:
        delta = invert_transform(mean_T) @ T
        translation_errors_m.append(float(np.linalg.norm(delta[:3, 3])))
        rotation_errors_deg.append(rotation_angle_deg(delta[:3, :3]))

    translation_rms_mm = 1000.0 * float(
        np.sqrt(np.mean(np.square(translation_errors_m)))
    )
    rotation_rms_deg = float(np.sqrt(np.mean(np.square(rotation_errors_deg))))

    # Heuristic only for selecting among OpenCV methods. Both raw values are
    # printed and saved, so the user can make the final engineering judgement.
    score = translation_rms_mm + 10.0 * rotation_rms_deg
    return translation_rms_mm, rotation_rms_deg, score, mean_T


def calibrate_eye_to_hand(observations: Sequence[Observation]) -> list[MethodResult]:
    # For the eye-to-hand rearrangement, pass end_T_base instead of base_T_end.
    end_T_bases = [invert_transform(obs.base_T_end) for obs in observations]

    R_end2base = [T[:3, :3].copy() for T in end_T_bases]
    t_end2base = [T[:3, 3].reshape(3, 1).copy() for T in end_T_bases]
    R_board2camera = [obs.camera_T_board[:3, :3].copy() for obs in observations]
    t_board2camera = [
        obs.camera_T_board[:3, 3].reshape(3, 1).copy() for obs in observations
    ]

    methods = {
        "TSAI": cv2.CALIB_HAND_EYE_TSAI,
        "PARK": cv2.CALIB_HAND_EYE_PARK,
        "HORAUD": cv2.CALIB_HAND_EYE_HORAUD,
        "ANDREFF": cv2.CALIB_HAND_EYE_ANDREFF,
        "DANIILIDIS": cv2.CALIB_HAND_EYE_DANIILIDIS,
    }

    results: list[MethodResult] = []
    for name, method in methods.items():
        try:
            R_base_camera, t_base_camera = cv2.calibrateHandEye(
                R_end2base,
                t_end2base,
                R_board2camera,
                t_board2camera,
                method=method,
            )
        except cv2.error as exc:
            print(f"[警告] {name} 标定失败: {exc}", file=sys.stderr)
            continue

        base_T_camera = make_transform(R_base_camera, t_base_camera.reshape(3))
        if not np.all(np.isfinite(base_T_camera)):
            print(f"[警告] {name} 返回 NaN/Inf，已忽略。", file=sys.stderr)
            continue
        if abs(np.linalg.det(base_T_camera[:3, :3]) - 1.0) > 1e-3:
            print(f"[警告] {name} 返回的旋转矩阵不合法，已忽略。", file=sys.stderr)
            continue

        t_rms, r_rms, score, end_T_board_mean = evaluate_result(
            base_T_camera, observations
        )
        results.append(
            MethodResult(
                name=name,
                base_T_camera=base_T_camera,
                translation_rms_mm=t_rms,
                rotation_rms_deg=r_rms,
                consistency_score=score,
                end_T_board_mean=end_T_board_mean,
            )
        )

    if not results:
        raise RuntimeError("所有 OpenCV 手眼标定方法都失败了。")
    return sorted(results, key=lambda r: r.consistency_score)


def matrix_to_list(T: np.ndarray) -> list[list[float]]:
    return [[float(v) for v in row] for row in T]


def save_results(
    output_dir: Path,
    best: MethodResult,
    all_results: Sequence[MethodResult],
    observations: Sequence[Observation],
    K: np.ndarray,
    dist: np.ndarray,
    image_size: tuple[int, int],
    args: argparse.Namespace,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    base_T_camera_path = output_dir / "T_base_camera.txt"
    camera_T_base_path = output_dir / "T_camera_base.txt"
    np.savetxt(base_T_camera_path, best.base_T_camera, fmt="%.12f")
    np.savetxt(
        camera_T_base_path,
        invert_transform(best.base_T_camera),
        fmt="%.12f",
    )

    payload = {
        "meaning": {
            "T_base_camera": "camera frame -> robot base frame",
            "point_transform": "P_base = R_base_camera @ P_camera + t_base_camera",
            "T_camera_base": "robot base frame -> camera frame",
        },
        "selected_method": best.name,
        "board": {
            "inner_corners_columns": args.board_cols,
            "inner_corners_rows": args.board_rows,
            "square_size_m": args.square_size_mm / 1000.0,
        },
        "robot_pose": {
            "source_file": str(Path(args.pose_file).expanduser()),
            "meaning": "base_T_end",
            "length_unit_in_file": args.pose_length_unit,
            "angle_unit_in_file": args.pose_angle_unit,
            "orientation_format": args.orientation_format,
            "rokae_rotation_formula": "Rz(Rz) @ Ry(Ry) @ Rx(Rx)",
        },
        "camera": {
            "image_width": image_size[0],
            "image_height": image_size[1],
            "K_used": matrix_to_list(K),
            "distortion": [float(v) for v in dist.reshape(-1)],
        },
        "best_result": {
            "translation_rms_mm": best.translation_rms_mm,
            "rotation_rms_deg": best.rotation_rms_deg,
            "consistency_score": best.consistency_score,
            "T_base_camera": matrix_to_list(best.base_T_camera),
            "T_camera_base": matrix_to_list(invert_transform(best.base_T_camera)),
            "T_end_board_mean": matrix_to_list(best.end_T_board_mean),
        },
        "method_comparison": [
            {
                "method": result.name,
                "translation_rms_mm": result.translation_rms_mm,
                "rotation_rms_deg": result.rotation_rms_deg,
                "consistency_score": result.consistency_score,
                "T_base_camera": matrix_to_list(result.base_T_camera),
            }
            for result in all_results
        ],
        "observations": [
            {
                "source_index": obs.index,
                "image": str(obs.image_path),
                "reprojection_rmse_px": obs.reprojection_rmse_px,
                "T_base_end": matrix_to_list(obs.base_T_end),
                "T_camera_board": matrix_to_list(obs.camera_T_board),
            }
            for obs in observations
        ],
    }
    (output_dir / "eye_to_hand_result.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Eye-to-hand 标定：固定相机 + 标定板固定在机械臂末端"
    )

    parser.add_argument(
        "--images",
        default=r"E:\luoshi\caldata3\data\rgb\*.png",
        help=r"图像目录或 glob，例如 E:\luoshi\caldata3\data\rgb\*.png",
    )

    parser.add_argument(
        "--pose-file",
        default=r"E:\luoshi\caldata3\rokae_saved_pose.txt",
        help="机械臂末端位姿文本，一行对应一张图像",
    )

    parser.add_argument(
        "--board-cols",
        type=int,
        default=11,
        help="每行内角点数",
    )

    parser.add_argument(
        "--board-rows",
        type=int,
        default=8,
        help="每列内角点数",
    )

    parser.add_argument(
        "--square-size-mm",
        type=float,
        default=15.0,
        help="相邻角点间距，毫米",
    )

    parser.add_argument(
        "--pose-length-unit",
        choices=("m", "mm"),
        default="m",
        help="机械臂位姿中 XYZ 的长度单位",
    )

    parser.add_argument(
        "--pose-angle-unit",
        choices=("rad", "deg"),
        default="rad",
        help="机械臂姿态角单位",
    )

    parser.add_argument(
        "--orientation-format",
        choices=("rokae_rpy", "rotvec"),
        default="rokae_rpy",
        help=(
            "机械臂姿态格式。"
            "rokae_rpy 表示最后三个数为 Rx、Ry、Rz，"
            "旋转矩阵按照 Rz(rz) @ Ry(ry) @ Rx(rx) 计算；"
            "rotvec 表示 Rodrigues 旋转向量"
        ),
    )

    parser.add_argument(
        "--detector",
        choices=("classic", "sb"),
        default="classic",
        help="classic 更容易检查角点顺序；sb 在复杂图像中可能更强",
    )

    parser.add_argument("--fx", type=float, default=DEFAULT_FX)
    parser.add_argument("--fy", type=float, default=DEFAULT_FY)
    parser.add_argument("--cx", type=float, default=DEFAULT_CX)
    parser.add_argument("--cy", type=float, default=DEFAULT_CY)

    parser.add_argument(
        "--dist",
        type=float,
        nargs="+",
        default=DEFAULT_DIST,
        help="OpenCV 畸变参数 k1 k2 p1 p2 k3 [k4 k5 k6]",
    )

    parser.add_argument(
        "--intrinsics-width",
        type=int,
        default=1280,
        help="以上内参对应的图像宽度",
    )

    parser.add_argument(
        "--intrinsics-height",
        type=int,
        default=720,
        help="以上内参对应的图像高度",
    )

    parser.add_argument(
        "--max-reprojection-error-px",
        type=float,
        default=1.0,
        help="超过该单帧重投影 RMSE 的样本将被剔除",
    )

    parser.add_argument(
        "--output-dir",
        default=r"E:\luoshi\caldata4\output",
        help="结果和角点检查图输出目录",
    )

    return parser


def main() -> int:
    args = build_arg_parser().parse_args()

    pose_file = Path(args.pose_file).expanduser().resolve()
    output_dir = Path(args.output_dir).expanduser().resolve()
    debug_dir = output_dir / "debug_corners"

    images = collect_images(args.images)
    if not images:
        raise RuntimeError(f"没有找到图像: {args.images}")

    poses = read_pose_file(
        pose_file,
        args.pose_length_unit,
        args.pose_angle_unit,
        args.orientation_format,
    )

    print(f"找到图像 {len(images)} 张，读取末端位姿 {len(poses)} 组。")
    if len(images) != len(poses):
        raise RuntimeError(
            "图像数量和位姿数量不一致，无法保证一一对应：\n"
            f"  图像: {len(images)}\n"
            f"  位姿: {len(poses)}\n"
            "请确保第 N 行位姿就是第 N 张按文件名自然排序后的图像。"
        )

    K_ref = np.array(
        [[args.fx, 0.0, args.cx], [0.0, args.fy, args.cy], [0.0, 0.0, 1.0]],
        dtype=np.float64,
    )
    dist = np.asarray(args.dist, dtype=np.float64).reshape(-1, 1)
    object_points = make_board_points(
        args.board_cols, args.board_rows, args.square_size_mm / 1000.0
    )

    observations, K_used, image_size = acquire_observations(
        images=images,
        poses=poses,
        object_points=object_points,
        pattern_size=(args.board_cols, args.board_rows),
        K_ref=K_ref,
        dist=dist,
        reference_size=(args.intrinsics_width, args.intrinsics_height),
        detector=args.detector,
        debug_dir=debug_dir,
        max_reprojection_error_px=args.max_reprojection_error_px,
    )

    print(f"\n有效样本：{len(observations)} / {len(images)}")
    if len(observations) < 5:
        raise RuntimeError("有效样本少于 5 组，无法可靠标定。建议采集 12~20 组。")
    if len(observations) < 10:
        print("[警告] 有效样本少于 10 组，建议继续采集不同倾角和距离的数据。")

    results = calibrate_eye_to_hand(observations)

    print("\n========== 各算法刚性一致性 ==========")
    for result in results:
        print(
            f"{result.name:10s}: "
            f"平移RMS={result.translation_rms_mm:9.3f} mm, "
            f"旋转RMS={result.rotation_rms_deg:8.3f} deg, "
            f"score={result.consistency_score:10.3f}"
        )

    best = results[0]
    print(f"\n自动选择：{best.name}")
    print("T_base_camera（相机坐标 -> 机器人基座坐标）=")
    print(np.array2string(best.base_T_camera, precision=9, suppress_small=True))
    print("\n用法：P_base = R_base_camera @ P_camera + t_base_camera")

    save_results(
        output_dir,
        best,
        results,
        observations,
        K_used,
        dist,
        image_size,
        args,
    )
    print(f"\n结果已保存到：{output_dir}")
    print(f"角点顺序检查图：{debug_dir}")
    print(
        "请检查每张 debug 图中红色 corner 0 是否始终对应标定板同一个物理角点；"
        "若发生 180° 翻转，普通无标记棋盘格无法唯一确定板坐标原点。"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError) as exc:
        print(f"\n[错误] {exc}", file=sys.stderr)
        raise SystemExit(1)



# python3 eye_to_hand_calibrate.py \
#   --images r"e:\luoshi\caldata4\data\rgb\*.png" \
#   --pose-file r"e:\luoshi\caldata4\rokae_saved_pose.txt" \
#   --board-cols 11 \
#   --board-rows 8 \
#   --square-size-mm 15 \
#   --pose-length-unit m \
#   --pose-angle-unit rad \
#   --orientation-format rokae_rpy \
#   --output-dir "E:\luoshi\caldata\output"
