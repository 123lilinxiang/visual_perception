// NOLINT: This file starts with a BOM since it contain non-ASCII characters
// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from msg_det:msg/DetectRes.idl
// generated code does not contain a copyright notice

#ifndef MSG_DET__MSG__DETAIL__DETECT_RES__STRUCT_H_
#define MSG_DET__MSG__DETAIL__DETECT_RES__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

// Include directives for member types
// Member 'pos'
#include "rosidl_runtime_c/primitives_sequence.h"

/// Struct defined in msg/DetectRes in the package msg_det.
typedef struct msg_det__msg__DetectRes
{
  /// 识别类型
  int16_t type;
  /// 识别结果
  rosidl_runtime_c__double__Sequence pos;
} msg_det__msg__DetectRes;

// Struct for a sequence of msg_det__msg__DetectRes.
typedef struct msg_det__msg__DetectRes__Sequence
{
  msg_det__msg__DetectRes * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} msg_det__msg__DetectRes__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // MSG_DET__MSG__DETAIL__DETECT_RES__STRUCT_H_
