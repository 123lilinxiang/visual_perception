// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from msg_det:msg/DetectRes.idl
// generated code does not contain a copyright notice

#ifndef MSG_DET__MSG__DETAIL__DETECT_RES__TRAITS_HPP_
#define MSG_DET__MSG__DETAIL__DETECT_RES__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "msg_det/msg/detail/detect_res__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace msg_det
{

namespace msg
{

inline void to_flow_style_yaml(
  const DetectRes & msg,
  std::ostream & out)
{
  out << "{";
  // member: type
  {
    out << "type: ";
    rosidl_generator_traits::value_to_yaml(msg.type, out);
    out << ", ";
  }

  // member: pos
  {
    if (msg.pos.size() == 0) {
      out << "pos: []";
    } else {
      out << "pos: [";
      size_t pending_items = msg.pos.size();
      for (auto item : msg.pos) {
        rosidl_generator_traits::value_to_yaml(item, out);
        if (--pending_items > 0) {
          out << ", ";
        }
      }
      out << "]";
    }
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const DetectRes & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: type
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "type: ";
    rosidl_generator_traits::value_to_yaml(msg.type, out);
    out << "\n";
  }

  // member: pos
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.pos.size() == 0) {
      out << "pos: []\n";
    } else {
      out << "pos:\n";
      for (auto item : msg.pos) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "- ";
        rosidl_generator_traits::value_to_yaml(item, out);
        out << "\n";
      }
    }
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const DetectRes & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace msg

}  // namespace msg_det

namespace rosidl_generator_traits
{

[[deprecated("use msg_det::msg::to_block_style_yaml() instead")]]
inline void to_yaml(
  const msg_det::msg::DetectRes & msg,
  std::ostream & out, size_t indentation = 0)
{
  msg_det::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use msg_det::msg::to_yaml() instead")]]
inline std::string to_yaml(const msg_det::msg::DetectRes & msg)
{
  return msg_det::msg::to_yaml(msg);
}

template<>
inline const char * data_type<msg_det::msg::DetectRes>()
{
  return "msg_det::msg::DetectRes";
}

template<>
inline const char * name<msg_det::msg::DetectRes>()
{
  return "msg_det/msg/DetectRes";
}

template<>
struct has_fixed_size<msg_det::msg::DetectRes>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<msg_det::msg::DetectRes>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<msg_det::msg::DetectRes>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // MSG_DET__MSG__DETAIL__DETECT_RES__TRAITS_HPP_
