// generated from rosidl_typesupport_introspection_c/resource/idl__type_support.c.em
// with input from msg_det:msg/DetectRes.idl
// generated code does not contain a copyright notice

#include <stddef.h>
#include "msg_det/msg/detail/detect_res__rosidl_typesupport_introspection_c.h"
#include "msg_det/msg/rosidl_typesupport_introspection_c__visibility_control.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "msg_det/msg/detail/detect_res__functions.h"
#include "msg_det/msg/detail/detect_res__struct.h"


// Include directives for member types
// Member `pos`
#include "rosidl_runtime_c/primitives_sequence_functions.h"

#ifdef __cplusplus
extern "C"
{
#endif

void msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  msg_det__msg__DetectRes__init(message_memory);
}

void msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_fini_function(void * message_memory)
{
  msg_det__msg__DetectRes__fini(message_memory);
}

size_t msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__size_function__DetectRes__pos(
  const void * untyped_member)
{
  const rosidl_runtime_c__double__Sequence * member =
    (const rosidl_runtime_c__double__Sequence *)(untyped_member);
  return member->size;
}

const void * msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__get_const_function__DetectRes__pos(
  const void * untyped_member, size_t index)
{
  const rosidl_runtime_c__double__Sequence * member =
    (const rosidl_runtime_c__double__Sequence *)(untyped_member);
  return &member->data[index];
}

void * msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__get_function__DetectRes__pos(
  void * untyped_member, size_t index)
{
  rosidl_runtime_c__double__Sequence * member =
    (rosidl_runtime_c__double__Sequence *)(untyped_member);
  return &member->data[index];
}

void msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__fetch_function__DetectRes__pos(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const double * item =
    ((const double *)
    msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__get_const_function__DetectRes__pos(untyped_member, index));
  double * value =
    (double *)(untyped_value);
  *value = *item;
}

void msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__assign_function__DetectRes__pos(
  void * untyped_member, size_t index, const void * untyped_value)
{
  double * item =
    ((double *)
    msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__get_function__DetectRes__pos(untyped_member, index));
  const double * value =
    (const double *)(untyped_value);
  *item = *value;
}

bool msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__resize_function__DetectRes__pos(
  void * untyped_member, size_t size)
{
  rosidl_runtime_c__double__Sequence * member =
    (rosidl_runtime_c__double__Sequence *)(untyped_member);
  rosidl_runtime_c__double__Sequence__fini(member);
  return rosidl_runtime_c__double__Sequence__init(member, size);
}

static rosidl_typesupport_introspection_c__MessageMember msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_message_member_array[2] = {
  {
    "type",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_INT16,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(msg_det__msg__DetectRes, type),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "pos",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    true,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(msg_det__msg__DetectRes, pos),  // bytes offset in struct
    NULL,  // default value
    msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__size_function__DetectRes__pos,  // size() function pointer
    msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__get_const_function__DetectRes__pos,  // get_const(index) function pointer
    msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__get_function__DetectRes__pos,  // get(index) function pointer
    msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__fetch_function__DetectRes__pos,  // fetch(index, &value) function pointer
    msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__assign_function__DetectRes__pos,  // assign(index, value) function pointer
    msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__resize_function__DetectRes__pos  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_message_members = {
  "msg_det__msg",  // message namespace
  "DetectRes",  // message name
  2,  // number of fields
  sizeof(msg_det__msg__DetectRes),
  msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_message_member_array,  // message members
  msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_init_function,  // function to initialize message memory (memory has to be allocated)
  msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_message_type_support_handle = {
  0,
  &msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_message_members,
  get_message_typesupport_handle_function,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_msg_det
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, msg_det, msg, DetectRes)() {
  if (!msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_message_type_support_handle.typesupport_identifier) {
    msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &msg_det__msg__DetectRes__rosidl_typesupport_introspection_c__DetectRes_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif
