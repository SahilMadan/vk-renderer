#include "task_stack.hpp"

namespace util {

TaskStack::~TaskStack() { Flush(); }

void TaskStack::Push(std::function<void()>&& function) {
  tasks_.push_back(function);
}

void TaskStack::Flush() {
  for (auto it = tasks_.rbegin(); it != tasks_.rend(); it++) {
    (*it)();
  }
  tasks_.clear();
}

}  // namespace util
