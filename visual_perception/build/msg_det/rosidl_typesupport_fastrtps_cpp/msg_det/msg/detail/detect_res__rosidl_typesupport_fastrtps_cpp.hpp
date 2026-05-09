// generated from rosidl_typesupport_fastrtps_cpp/resource/idl__rosidl_typesupport_fastrtps_cpp.hpp.em
// with input from msg_det:msg/DetectRes.idl
// generated code does not contain a copyright notice

#ifndef MSG_DET__MSG__DETAIL__DETECT_RES__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_
#define MSG_DET__MSG__DETAIL__DETECT_RES__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_

#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_interface/macros.h"
#include "msg_det/msg/rosidl_typesupport_fastrtps_cpp__visibility_control.h"
#include "msg_det/msg/detail/detect_res__struct.hpp"

#ifndef _WIN32
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-parameter"
# ifdef __clang__
#  pragma clang diagnostic ignored "-Wdeprecated-register"
#  pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
# endif
#endif
#ifndef _WIN32
# pragma GCC diagnostic pop
#endif

#include "fastcdr/Cdr.h"

namespace msg_det
{

namespace msg
{

namespace typesupport_fastrtps_cpp
{

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_msg_det
cdr_serialize(
  const msg_det::msg::DetectRes & ros_message,
  eprosima::fastcdr::Cdr & cdr);

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_msg_det
cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  msg_det::msg::DetectRes & ros_message);

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_msg_det
get_serialized_size(
  const msg_det::msg::DetectRes & ros_message,
  size_t current_alignment);

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_msg_det
max_serialized_size_DetectRes(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

}  // namespace typesupport_fastrtps_cpp

}  // namespace msg

}  // namespace msg_det

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_msg_det
const rosidl_message_type_support_t *
  ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, msg_det, msg, DetectRes)();

#ifdef __cplusplus
}
#endif

#endif  // MSG_DET__MSG__DETAIL__DETECT_RES__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_
