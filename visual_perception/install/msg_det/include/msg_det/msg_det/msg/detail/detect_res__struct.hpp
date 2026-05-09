// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from msg_det:msg/DetectRes.idl
// generated code does not contain a copyright notice

#ifndef MSG_DET__MSG__DETAIL__DETECT_RES__STRUCT_HPP_
#define MSG_DET__MSG__DETAIL__DETECT_RES__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


#ifndef _WIN32
# define DEPRECATED__msg_det__msg__DetectRes __attribute__((deprecated))
#else
# define DEPRECATED__msg_det__msg__DetectRes __declspec(deprecated)
#endif

namespace msg_det
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct DetectRes_
{
  using Type = DetectRes_<ContainerAllocator>;

  explicit DetectRes_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->type = 0;
    }
  }

  explicit DetectRes_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    (void)_alloc;
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->type = 0;
    }
  }

  // field types and members
  using _type_type =
    int16_t;
  _type_type type;
  using _pos_type =
    std::vector<double, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<double>>;
  _pos_type pos;

  // setters for named parameter idiom
  Type & set__type(
    const int16_t & _arg)
  {
    this->type = _arg;
    return *this;
  }
  Type & set__pos(
    const std::vector<double, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<double>> & _arg)
  {
    this->pos = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    msg_det::msg::DetectRes_<ContainerAllocator> *;
  using ConstRawPtr =
    const msg_det::msg::DetectRes_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<msg_det::msg::DetectRes_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<msg_det::msg::DetectRes_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      msg_det::msg::DetectRes_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<msg_det::msg::DetectRes_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      msg_det::msg::DetectRes_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<msg_det::msg::DetectRes_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<msg_det::msg::DetectRes_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<msg_det::msg::DetectRes_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__msg_det__msg__DetectRes
    std::shared_ptr<msg_det::msg::DetectRes_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__msg_det__msg__DetectRes
    std::shared_ptr<msg_det::msg::DetectRes_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const DetectRes_ & other) const
  {
    if (this->type != other.type) {
      return false;
    }
    if (this->pos != other.pos) {
      return false;
    }
    return true;
  }
  bool operator!=(const DetectRes_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct DetectRes_

// alias to use template instance with default allocator
using DetectRes =
  msg_det::msg::DetectRes_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace msg_det

#endif  // MSG_DET__MSG__DETAIL__DETECT_RES__STRUCT_HPP_
