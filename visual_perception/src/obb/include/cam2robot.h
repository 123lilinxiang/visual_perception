#pragma once

#include <Eigen/Dense>
#include <vector>
#include <stdexcept>
#include <cmath>



class CamBaseTransformer
{
public:
    CamBaseTransformer(const Eigen::Vector3d& head_joint_yaw_offset,
                       const Eigen::Vector3d& head_joint_pitch_offset,
                       const Eigen::Vector3d& camera_offset,
                       double camera_tilt_rad)
        : head_joint_yaw_offset_(head_joint_yaw_offset),
          head_joint_pitch_offset_(head_joint_pitch_offset),
          camera_offset_(camera_offset),
          camera_tilt_(camera_tilt_rad)
          {}

    Eigen::Vector3d transformPoint(const Eigen::Vector3d& point_camera,
                                   double head_yaw_angle = 0.0,
                                   double head_pitch_angle = 0.0);


private:
    Eigen::Matrix4d buildTransform(double head_yaw_angle,
                                   double head_pitch_angle);

private:
    Eigen::Vector3d head_joint_yaw_offset_;
    Eigen::Vector3d head_joint_pitch_offset_;
    Eigen::Vector3d camera_offset_;
    double camera_tilt_;
};