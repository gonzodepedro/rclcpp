// Copyright 2021 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RCLCPP__EXPERIMENTAL__SUBSCRIPTION_INTRA_PROCESS_BUFFER_HPP_
#define RCLCPP__EXPERIMENTAL__SUBSCRIPTION_INTRA_PROCESS_BUFFER_HPP_

#include <rmw/rmw.h>

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "rcl/error_handling.h"

#include "rclcpp/any_subscription_callback.hpp"
#include "rclcpp/experimental/buffers/intra_process_buffer.hpp"
#include "rclcpp/experimental/create_intra_process_buffer.hpp"
#include "rclcpp/experimental/subscription_intra_process_base.hpp"
#include "rclcpp/experimental/ros_message_intra_process_buffer.hpp"
#include "rclcpp/qos.hpp"
#include "rclcpp/type_support_decl.hpp"
#include "rclcpp/waitable.hpp"
#include "tracetools/tracetools.h"

namespace rclcpp
{
namespace experimental
{

template<
  typename MessageT,
  typename Alloc = std::allocator<void>,
  typename Deleter = std::default_delete<MessageT>,
  /// MessageT::custom_type if MessageT is a TypeAdapter,
  /// otherwise just MessageT.
  typename SubscribedT = typename rclcpp::TypeAdapter<MessageT>::custom_type,
  /// MessageT::ros_message_type if MessageT is a TypeAdapter,
  /// otherwise just MessageT.
  typename ROSMessageT = typename rclcpp::TypeAdapter<MessageT>::ros_message_type
>
class SubscriptionIntraProcessBuffer : public ROSMessageIntraProcessBuffer<ROSMessageT, Alloc, Deleter>
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(SubscriptionIntraProcessBuffer)

  using SubscribedType = SubscribedT;
  using ROSMessageType = ROSMessageT;

  using SubscribedTypeAllocatorTraits = allocator::AllocRebind<SubscribedType, Alloc>;
  using SubscribedTypeAllocator = typename SubscribedTypeAllocatorTraits::allocator_type;
  using SubscribedTypeDeleter = allocator::Deleter<SubscribedTypeAllocator, SubscribedType>;

  using ROSMessageTypeAllocatorTraits = allocator::AllocRebind<ROSMessageType, Alloc>;
  using ROSMessageTypeAllocator = typename ROSMessageTypeAllocatorTraits::allocator_type;
  using ROSMessageTypeDeleter = allocator::Deleter<ROSMessageTypeAllocator, ROSMessageType>;

  using ConstMessageSharedPtr = std::shared_ptr<const ROSMessageType>;
  using MessageUniquePtr = std::unique_ptr<ROSMessageType, ROSMessageTypeDeleter>;

  using ConstDataSharedPtr = std::shared_ptr<const SubscribedType>;
  using DataUniquePtr = std::unique_ptr<SubscribedType, SubscribedTypeDeleter>;

  using BufferUniquePtr = typename rclcpp::experimental::buffers::IntraProcessBuffer<
    SubscribedType,
    SubscribedTypeAllocator,
    SubscribedTypeDeleter
    >::UniquePtr;

  SubscriptionIntraProcessBuffer(
    std::shared_ptr<SubscribedTypeAllocator> allocator,
    rclcpp::Context::SharedPtr context,
    const std::string & topic_name,
    const rclcpp::QoS & qos_profile,
    rclcpp::IntraProcessBufferType buffer_type)
  : ROSMessageIntraProcessBuffer<ROSMessageT, ROSMessageTypeAllocator, ROSMessageTypeDeleter>(topic_name, qos_profile)
  {
    // Create the intra-process buffer.
    buffer_ = rclcpp::experimental::create_intra_process_buffer<SubscribedType, SubscribedTypeAllocator, SubscribedTypeDeleter>(
      buffer_type,
      qos_profile,
      allocator);

    // Create the guard condition.
    rcl_guard_condition_options_t guard_condition_options =
      rcl_guard_condition_get_default_options();

    this->gc_ = rcl_get_zero_initialized_guard_condition();
    rcl_ret_t ret = rcl_guard_condition_init(
      &this->gc_, context->get_rcl_context().get(), guard_condition_options);

    if (RCL_RET_OK != ret) {
      throw std::runtime_error(
              "SubscriptionIntraProcessBuffer init error initializing guard condition");
    }
  }

  virtual ~SubscriptionIntraProcessBuffer()
  {
    if (rcl_guard_condition_fini(&this->gc_) != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED(
        "rclcpp",
        "Failed to destroy guard condition: %s",
        rcutils_get_error_string().str);
    }
  }

  bool
  is_ready(rcl_wait_set_t * wait_set)
  {
    (void) wait_set;
    return buffer_->has_data();
  }

  void
  provide_intra_process_message(ConstMessageSharedPtr message)
  {

    if constexpr (!rclcpp::TypeAdapter<MessageT>::is_specialized::value) {
      buffer_->add_shared(std::move(message));
      trigger_guard_condition();
    } else {
      // auto ptr = SubscribedTypeAllocatorTraits::allocate(subscribed_type_allocator_, 1);
      // SubscribedTypeAllocatorTraits::construct(subscribed_type_allocator_, ptr, *message);
      // buffer_->add_shared(std::unique_ptr<SubscribedType, SubscribedTypeDeleter>(ptr, subscribed_type_deleter_));
      // trigger_guard_condition();
    }
  }

  void
  provide_intra_process_message(MessageUniquePtr message)
  {

    if constexpr (!rclcpp::TypeAdapter<MessageT>::is_specialized::value) {
      buffer_->add_unique(std::move(message));
      trigger_guard_condition();
    } else {
      // auto ptr = SubscribedTypeAllocatorTraits::allocate(subscribed_type_allocator_, 1);
      // SubscribedTypeAllocatorTraits::construct(subscribed_type_allocator_, ptr);
      // rclcpp::TypeAdapter<MessageT>::convert_to_custom(*message, *ptr);
      // buffer_->add_unique(std::unique_ptr<SubscribedType, SubscribedTypeDeleter>(ptr, subscribed_type_deleter_));
      // trigger_guard_condition();
    }

  }

  void
  provide_intra_process_data(ConstDataSharedPtr message)
  {
    buffer_->add_shared(std::move(message));
    trigger_guard_condition();
  }

  void
  provide_intra_process_data(DataUniquePtr message)
  {
    buffer_->add_unique(std::move(message));
    trigger_guard_condition();
  }

  bool
  use_take_shared_method() const
  {
    return buffer_->use_take_shared_method();
  }

protected:
  void
  trigger_guard_condition()
  {
    rcl_ret_t ret = rcl_trigger_guard_condition(&this->gc_);
    (void)ret;
  }

  BufferUniquePtr buffer_;
};

}  // namespace experimental
}  // namespace rclcpp

#endif  // RCLCPP__EXPERIMENTAL__SUBSCRIPTION_INTRA_PROCESS_BUFFER_HPP_
