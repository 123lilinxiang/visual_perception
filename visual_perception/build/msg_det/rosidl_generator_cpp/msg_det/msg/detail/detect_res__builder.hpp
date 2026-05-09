// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from msg_det:msg/DetectRes.idl
// generated code does not contain a copyright notice

#ifndef MSG_DET__MSG__DETAIL__DETECT_RES__BUILDER_HPP_
#define MSG_DET__MSG__DETAIL__DETECT_RES__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "msg_det/msg/detail/detect_res__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace msg_det
{

namespace msg
{

namespace builder
{

class Init_DetectRes_pos
{
public:
  explicit Init_DetectRes_pos(::msg_det::msg::DetectRes & msg)
  : msg_(msg)
  {}
  ::msg_det::msg::DetectRes pos(::msg_det::msg::DetectRes::_pos_type arg)
  {
    msg_.pos = std::move(arg);
    return std::move(msg_);
  }

private:
  ::msg_det::msg::DetectRes msg_;
};

class Init_DetectRes_type
{
public:
  Init_DetectRes_type()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_DetectRes_pos type(::msg_det::msg::DetectRes::_type_type arg)
  {
    msg_.type = std::move(arg);
    return Init_DetectRes_pos(msg_);
  }

private:
  ::msg_det::msg::DetectRes msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::msg_det::msg::DetectRes>()
{
  return msg_det::msg::builder::Init_DetectRes_type();
}

}  // namespace msg_det

#endif  // MSG_DET__MSG__DETAIL__DETECT_RES__BUILDER_HPP_
