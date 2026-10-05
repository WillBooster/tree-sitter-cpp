using typeof = int;
using typeof_unqual = long;
struct Box { typeof member; };
typeof echo(typeof argument) { return argument; }
auto other() -> typeof_unqual { return 1; }
template<class T> struct Holder { T value; };
Holder<typeof> holder{1};
struct Conversion {
  operator typeof() const noexcept { return 1; }
  explicit operator typeof_unqual() const { return 2; }
};
namespace Calls {
int typeof(int x) { return x; }
int typeof_unqual(int x) { return x; }
void operations(int x, int y) {
  typeof(x) * y;
  typeof(x) & y;
  typeof_unqual(x) * y;
  typeof_unqual(x) & y;
}
}
int main() {
  Box box{1};
  typeof values[2]{1,2};
  typeof* pointer = values;
  return echo((typeof)box.member) + other() + holder.value + pointer[0] - 4;
}
