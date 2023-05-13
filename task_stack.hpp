#pragma once

#include <functional>
#include <vector>

namespace util {

class TaskStack {
 public:
  ~TaskStack();
  void Push(std::function<void()>&& function);
  void Flush();

 private:
  std::vector<std::function<void()>> tasks_;
};

}  // namespace util
