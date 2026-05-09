// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from msg_det:msg/DetectRes.idl
// generated code does not contain a copyright notice
#include "msg_det/msg/detail/detect_res__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


// Include directives for member types
// Member `pos`
#include "rosidl_runtime_c/primitives_sequence_functions.h"

bool
msg_det__msg__DetectRes__init(msg_det__msg__DetectRes * msg)
{
  if (!msg) {
    return false;
  }
  // type
  // pos
  if (!rosidl_runtime_c__double__Sequence__init(&msg->pos, 0)) {
    msg_det__msg__DetectRes__fini(msg);
    return false;
  }
  return true;
}

void
msg_det__msg__DetectRes__fini(msg_det__msg__DetectRes * msg)
{
  if (!msg) {
    return;
  }
  // type
  // pos
  rosidl_runtime_c__double__Sequence__fini(&msg->pos);
}

bool
msg_det__msg__DetectRes__are_equal(const msg_det__msg__DetectRes * lhs, const msg_det__msg__DetectRes * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // type
  if (lhs->type != rhs->type) {
    return false;
  }
  // pos
  if (!rosidl_runtime_c__double__Sequence__are_equal(
      &(lhs->pos), &(rhs->pos)))
  {
    return false;
  }
  return true;
}

bool
msg_det__msg__DetectRes__copy(
  const msg_det__msg__DetectRes * input,
  msg_det__msg__DetectRes * output)
{
  if (!input || !output) {
    return false;
  }
  // type
  output->type = input->type;
  // pos
  if (!rosidl_runtime_c__double__Sequence__copy(
      &(input->pos), &(output->pos)))
  {
    return false;
  }
  return true;
}

msg_det__msg__DetectRes *
msg_det__msg__DetectRes__create()
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  msg_det__msg__DetectRes * msg = (msg_det__msg__DetectRes *)allocator.allocate(sizeof(msg_det__msg__DetectRes), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(msg_det__msg__DetectRes));
  bool success = msg_det__msg__DetectRes__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
msg_det__msg__DetectRes__destroy(msg_det__msg__DetectRes * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    msg_det__msg__DetectRes__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
msg_det__msg__DetectRes__Sequence__init(msg_det__msg__DetectRes__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  msg_det__msg__DetectRes * data = NULL;

  if (size) {
    data = (msg_det__msg__DetectRes *)allocator.zero_allocate(size, sizeof(msg_det__msg__DetectRes), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = msg_det__msg__DetectRes__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        msg_det__msg__DetectRes__fini(&data[i - 1]);
      }
      allocator.deallocate(data, allocator.state);
      return false;
    }
  }
  array->data = data;
  array->size = size;
  array->capacity = size;
  return true;
}

void
msg_det__msg__DetectRes__Sequence__fini(msg_det__msg__DetectRes__Sequence * array)
{
  if (!array) {
    return;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();

  if (array->data) {
    // ensure that data and capacity values are consistent
    assert(array->capacity > 0);
    // finalize all array elements
    for (size_t i = 0; i < array->capacity; ++i) {
      msg_det__msg__DetectRes__fini(&array->data[i]);
    }
    allocator.deallocate(array->data, allocator.state);
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
  } else {
    // ensure that data, size, and capacity values are consistent
    assert(0 == array->size);
    assert(0 == array->capacity);
  }
}

msg_det__msg__DetectRes__Sequence *
msg_det__msg__DetectRes__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  msg_det__msg__DetectRes__Sequence * array = (msg_det__msg__DetectRes__Sequence *)allocator.allocate(sizeof(msg_det__msg__DetectRes__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = msg_det__msg__DetectRes__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
msg_det__msg__DetectRes__Sequence__destroy(msg_det__msg__DetectRes__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    msg_det__msg__DetectRes__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
msg_det__msg__DetectRes__Sequence__are_equal(const msg_det__msg__DetectRes__Sequence * lhs, const msg_det__msg__DetectRes__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!msg_det__msg__DetectRes__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
msg_det__msg__DetectRes__Sequence__copy(
  const msg_det__msg__DetectRes__Sequence * input,
  msg_det__msg__DetectRes__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    const size_t allocation_size =
      input->size * sizeof(msg_det__msg__DetectRes);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    msg_det__msg__DetectRes * data =
      (msg_det__msg__DetectRes *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!msg_det__msg__DetectRes__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          msg_det__msg__DetectRes__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!msg_det__msg__DetectRes__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
