# Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.


from bundlesdf import *
import argparse
import os,sys
code_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.append(f'{code_dir}/BundleTrack/scripts')
from data_reader import *
import numpy as np


def run_one_video(video_dir,out_dir):
  set_seed(0)

  reader = Ho3dReader(video_dir)
  video_name = reader.get_video_name()
  out_folder = f'{out_dir}/{video_name}/'   #!NOTE there has to be a / in the end
  if os.path.exists(f'{out_folder}/ob_in_cam'):
    pose_files = sorted(glob.glob(f'{out_folder}/ob_in_cam/*.txt'))
    if len(pose_files)==len(reader.color_files):
      print(f"{out_folder} done before, skip")
      return

  os.system(f"rm -rf {out_folder} && mkdir -p {out_folder}")

  code_dir = os.path.dirname(os.path.realpath(__file__))
  cfg_bundletrack = yaml.load(open(f"{code_dir}/BundleTrack/config_ho3d.yml",'r'))
  cfg_bundletrack['data_dir'] = video_dir
  cfg_bundletrack['SPDLOG'] = 2
  cfg_bundletrack['depth_processing']["zfar"] = 1
  cfg_bundletrack['debug_dir'] = out_folder
  cfg_track_dir = f'{out_folder}/config_bundletrack.yml'
  yaml.dump(cfg_bundletrack, open(cfg_track_dir,'w'))

  cfg_nerf = yaml.load(open(f"{code_dir}/config.yml",'r'))
  cfg_nerf['trunc_start'] = 0.01
  cfg_nerf['trunc'] = 0.01
  cfg_nerf['down_scale_ratio'] = 1
  cfg_nerf['far'] = cfg_bundletrack['depth_processing']["zfar"]
  cfg_nerf['datadir'] = f"{out_folder}/nerf_with_bundletrack_online"
  cfg_nerf['save_dir'] = copy.deepcopy(cfg_nerf['datadir'])
  cfg_nerf_dir = f'{out_folder}/config_nerf.yml'
  yaml.dump(cfg_nerf, open(cfg_nerf_dir,'w'))

  tracker = BundleSdf(cfg_track_dir=cfg_track_dir, cfg_nerf_dir=cfg_nerf_dir, start_nerf_keyframes=5, use_gui=args.use_gui)

  for i, color_file in enumerate(reader.color_files):
    color = cv2.imread(color_file, cv2.IMREAD_COLOR)
    if color is None:
      raise FileNotFoundError(f"Failed to read color file: {color_file}")

    H, W = color.shape[:2]

    depth = reader.get_depth(i)
    if depth is None:
      raise RuntimeError(f"reader.get_depth({i}) returned None")

    if i == 0:
      mask = reader.get_mask(0)
    else:
      mask = reader.get_mask(i)

    if mask is None:
      raise RuntimeError(f"reader.get_mask({i}) returned None")

    mask = cv2.resize(mask, (W, H), interpolation=cv2.INTER_NEAREST)

    if mask.ndim == 3:
      mask = mask[..., 0]

    color = np.ascontiguousarray(color, dtype=np.uint8)
    depth = np.ascontiguousarray(depth, dtype=np.float32)
    mask = np.ascontiguousarray((mask > 0).astype(np.uint8) * 255)
    K = np.ascontiguousarray(reader.K, dtype=np.float32).reshape(3, 3)

    id_str = reader.id_strs[i]

    print(
      f"[frame {i}] "
      f"color={color.shape},{color.dtype},{color.flags['C_CONTIGUOUS']} "
      f"depth={depth.shape},{depth.dtype},{depth.flags['C_CONTIGUOUS']},min={depth.min():.4f},max={depth.max():.4f} "
      f"mask={mask.shape},{mask.dtype},{mask.flags['C_CONTIGUOUS']},sum={mask.sum()} "
      f"K={K.shape},{K.dtype},{K.flags['C_CONTIGUOUS']} "
      f"id={id_str}",
      flush=True
    )

    if mask.sum() == 0:
      print(f"[WARN] frame {i} mask is empty, skip this frame", flush=True)
      continue

    tracker.run(color, depth, K, id_str, mask=mask, occ_mask=None)

  tracker.on_finish()
  print(f"Done {video_dir}")


def run_one_video_global_nerf(video_dir,out_dir):
  set_seed(0)

  reader = Ho3dReader(video_dir)
  video_name = reader.get_video_name()
  out_folder = f'{out_dir}/{video_name}/'   #!NOTE there has to be a / in the end

  tracker = BundleSdf(cfg_track_dir=f"{out_folder}/config_bundletrack.yml", cfg_nerf_dir=f"{out_folder}/config_nerf.yml", start_nerf_keyframes=5, use_gui=False)
  tracker.cfg_nerf['n_step'] = 2000
  tracker.cfg_nerf['N_samples'] = 256
  tracker.cfg_nerf['N_samples'] = 128
  tracker.cfg_nerf['down_scale_ratio'] = 1
  tracker.cfg_nerf['finest_res'] = 512
  tracker.cfg_nerf['num_levels'] = 16
  tracker.cfg_nerf['mesh_resolution'] = 0.003

  tracker.cfg_nerf['i_img'] = 500
  tracker.cfg_nerf['i_mesh'] = tracker.cfg_nerf['i_img']
  tracker.cfg_nerf['i_nerf_normals'] = tracker.cfg_nerf['i_img']
  tracker.cfg_nerf['i_save_ray'] = tracker.cfg_nerf['i_img']

  tracker.debug_dir = f'{out_folder}'
  tracker.cfg_nerf['datadir'] = f"{tracker.debug_dir}/nerf_with_bundletrack_online"
  tracker.cfg_nerf['save_dir'] = copy.deepcopy(tracker.cfg_nerf['datadir'])

  tracker.run_global_nerf(reader=reader, get_texture=True, tex_res=1024)

  print(f"Done {video_dir}")


def run_all():
  video_dirs = sorted(glob.glob('BundleTrack/HO3D_v3/evaluation/*'))

  for video_dir in video_dirs:
    run_one_video_global_nerf(video_dir, args.out_dir)


if __name__=="__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument('--video_dirs', type=str, default="BundleTrack/HO3D_v3/evaluation/SM1")
  parser.add_argument('--out_dir', type=str, default="output")
  parser.add_argument('--use_segmenter', type=int, default=0)
  parser.add_argument('--use_gui', type=int, default=0)

  # track：正常跑 BundleSDF tracking
  # texture：在已经跑完 tracking 的结果上，生成 textured_mesh.obj + 纹理 PNG
  # both：先 tracking，再 texture
  parser.add_argument('--mode', type=str, default="track", choices=["track", "texture", "both"])

  args = parser.parse_args()

  use_segmenter = args.use_segmenter
  video_dirs = args.video_dirs.split(',')

  print("video_dirs:\n", video_dirs)
  print("mode:", args.mode)

  for video_dir in video_dirs:
    if args.mode == "track":
      run_one_video(video_dir, args.out_dir)

    elif args.mode == "texture":
      run_one_video_global_nerf(video_dir, args.out_dir)

    elif args.mode == "both":
      run_one_video(video_dir, args.out_dir)
      run_one_video_global_nerf(video_dir, args.out_dir)


# python run_ho3d.py --video_dirs BundleTrack/HO3D_v3/evaluation/SM1 --out_dir output1
# gdb -q --args python run_ho3d.py --video_dirs BundleTrack/HO3D_v3/evaluation/SM1 --out_dir output
# python run_ho3d.py \
  # --video_dirs BundleTrack/HO3D_v3/evaluation/SM1 \
  # --out_dir output1 \
  # --mode texture