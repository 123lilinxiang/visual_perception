#include "cam2robot.h"


Eigen::Vector3d CamBaseTransformer::transformPoint(const Eigen::Vector3d& point_camera,
                                double head_yaw_angle,
                                double head_pitch_angle) 
{
    Eigen::Matrix4d T_camera_to_base =
        buildTransform(head_yaw_angle, head_pitch_angle);

    Eigen::Vector4d p_camera_homo;
    p_camera_homo << point_camera.x(),
                        point_camera.y(),
                        point_camera.z(),
                        1.0;

    Eigen::Vector4d p_base_homo = T_camera_to_base * p_camera_homo;

    return p_base_homo.head<3>();
}

Eigen::Matrix4d CamBaseTransformer::buildTransform(double head_yaw_angle,
                                double head_pitch_angle) 
{
    Eigen::Matrix4d T_head_yaw_to_base = Eigen::Matrix4d::Identity();

    Eigen::Matrix3d R_head_yaw_to_base =
        Eigen::AngleAxisd(head_yaw_angle, Eigen::Vector3d::UnitZ())
            .toRotationMatrix();

    T_head_yaw_to_base.block<3, 3>(0, 0) = R_head_yaw_to_base;
    T_head_yaw_to_base.block<3, 1>(0, 3) = head_joint_yaw_offset_;

    Eigen::Matrix4d T_head_pitch_to_head_yaw = Eigen::Matrix4d::Identity();

    Eigen::Matrix3d R_head_pitch_to_head_yaw =
        Eigen::AngleAxisd(head_pitch_angle, Eigen::Vector3d::UnitY())
            .toRotationMatrix();

    T_head_pitch_to_head_yaw.block<3, 3>(0, 0) =
        R_head_pitch_to_head_yaw;
    T_head_pitch_to_head_yaw.block<3, 1>(0, 3) =
        head_joint_pitch_offset_;

    Eigen::Matrix4d T_camera_to_head_pitch = Eigen::Matrix4d::Identity();

    Eigen::Matrix3d R_camera_optical_to_head_pitch;
    R_camera_optical_to_head_pitch <<  0,  0,  1,
                                        -1,  0,  0,
                                        0, -1,  0;

    Eigen::Matrix3d R_camera_tilt =
        Eigen::AngleAxisd(camera_tilt_, Eigen::Vector3d::UnitY())
            .toRotationMatrix()
        * R_camera_optical_to_head_pitch;

    T_camera_to_head_pitch.block<3, 3>(0, 0) = R_camera_tilt;
    T_camera_to_head_pitch.block<3, 1>(0, 3) = camera_offset_;

    return T_head_yaw_to_base
            * T_head_pitch_to_head_yaw
            * T_camera_to_head_pitch;
}

