using typeof = int;
using typeof_unqual = int;
struct Box { typeof member; };
typeof echo(typeof argument) { return argument; }
auto other() -> typeof_unqual { return 1; }
template<class T> struct Holder { T value; };
Holder<typeof> holder{1};
int main() {
  Box box{1};
  typeof values[2]{1,2};
  typeof* pointer = values;
  return echo((typeof)box.member) + other() + holder.value + pointer[0] - 4;
}
