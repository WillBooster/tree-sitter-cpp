int source = 3;
__typeof__(int*) pointer = nullptr;
__typeof__(source) value = 4;
using Number = __typeof__(1 + 2);
typedef __typeof__(int) Integer;
template<class T> struct Holder { T value; };
Holder<__typeof__(source)> holder{3};
struct Box { __typeof__(source) member; };
__typeof__(int) echo(__typeof__(int) argument) { return argument; }
auto other() -> __typeof__(source) { return source; }
int main() {
  Number copy = (__typeof__(value))value;
  Box box{source};
  return echo(copy) + other() + box.member + holder.value - 13
    + (pointer != nullptr) + (sizeof(__typeof__(source)) != sizeof(int));
}
