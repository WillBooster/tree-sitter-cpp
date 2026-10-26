#include <coroutine>

void ordinary(bool enabled, int (&values)[2]) {
  if (enabled) [[likely]] throw 1;
  if (enabled) [[likely]] for (int value : values) { (void)value; }
  while (enabled) [[unlikely]] throw 2;
  do [[unlikely]] throw 3; while (enabled);
}

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    std::suspend_never yield_value(int) { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

Task coroutine(bool enabled) {
  if (enabled) [[likely]] co_yield 1;
  if (enabled) [[unlikely]] co_return;
  co_return;
}
