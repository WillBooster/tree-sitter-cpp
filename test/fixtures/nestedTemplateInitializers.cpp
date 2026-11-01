#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace sizes {
template<class T> constexpr std::size_t count() { return 3; }
}

enum class Entry { first, second, third };

int main() {
  auto values = std::to_array<std::pair<int, std::array<std::string_view, sizes::count<Entry>()>>>({
    {1, {"first"}},
    {2, {"second"}},
    {3, {"third"}},
  });
}
