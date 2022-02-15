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
#include <gtest/gtest.h>

#include <memory>

#include "rclcpp/executors/detail/events_executor_event_types.hpp"
#include "rclcpp/executors/detail/simple_events_queue.hpp"

using namespace std::chrono_literals;

TEST(TestEventsQueue, SimpleQueueTest)
{
  // Create a SimpleEventsQueue and a local queue
  auto simple_queue = std::make_unique<rclcpp::executors::detail::SimpleEventsQueue>();
  rclcpp::executors::detail::ExecutorEvent event {};
  bool ret = false;

  // Make sure the queue is empty at startup
  EXPECT_TRUE(simple_queue->empty());
  EXPECT_EQ(simple_queue->size(), 0u);

  // Push 11 messages
  for (uint32_t i = 1; i < 11; i++) {
    rclcpp::executors::detail::ExecutorEvent stub_event {};
    stub_event.num_events = 1;
    simple_queue->enqueue(stub_event);

    EXPECT_FALSE(simple_queue->empty());
    EXPECT_EQ(simple_queue->size(), i);
  }

  // Pop one message
  ret = simple_queue->dequeue(event);
  EXPECT_TRUE(ret);
  EXPECT_FALSE(simple_queue->empty());
  EXPECT_EQ(simple_queue->size(), 9u);

  // Pop one message
  ret = simple_queue->dequeue(event, std::chrono::nanoseconds(0));
  EXPECT_TRUE(ret);
  EXPECT_FALSE(simple_queue->empty());
  EXPECT_EQ(simple_queue->size(), 8u);

  while (!simple_queue->empty()) {
    ret = simple_queue->dequeue(event);
    EXPECT_TRUE(ret);
  }

  EXPECT_TRUE(simple_queue->empty());
  EXPECT_EQ(simple_queue->size(), 0u);

  ret = simple_queue->dequeue(event, std::chrono::nanoseconds(0));
  EXPECT_FALSE(ret);

  // Lets push an event into the queue and get it back
  rclcpp::executors::detail::ExecutorEvent push_event = {
    simple_queue.get(),
    99,
    rclcpp::executors::detail::ExecutorEventType::SUBSCRIPTION_EVENT,
    1};

  simple_queue->enqueue(push_event);
  ret = simple_queue->dequeue(event);
  EXPECT_TRUE(ret);
  EXPECT_EQ(push_event.exec_entity_id, event.exec_entity_id);
  EXPECT_EQ(push_event.gen_entity_id, event.gen_entity_id);
  EXPECT_EQ(push_event.type, event.type);
  EXPECT_EQ(push_event.num_events, event.num_events);
}
