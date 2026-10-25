#include <cstdint>
#include <iostream>
#include <type_traits>
#include <vector>

struct Plain {};
struct C {};
template<class> struct Nested {};
template<class> struct A {};
namespace ns { template<class> struct A {}; }

int main() {
  std::cout << std::is_same<Plain, A<Nested<C>>>::value;
  std::cout << std::is_same<ns::A<std::vector<std::uint8_t>>, Plain>::value;
}
