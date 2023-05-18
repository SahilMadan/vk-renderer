#pragma once

#include <functional>
#include <vector>

namespace util {

class TaskStack {
 public:
  TaskStack() {}
  ~TaskStack();

  TaskStack(TaskStack&& other) noexcept : tasks_{std::move(other.tasks_)} {}

  TaskStack& operator=(TaskStack&& other) noexcept {
    tasks_ = std::move(other.tasks_);
  }

  void Push(std::function<void()>&& function);
  void Flush();

 private:
  std::vector<std::function<void()>> tasks_;
};

}  // namespace util
