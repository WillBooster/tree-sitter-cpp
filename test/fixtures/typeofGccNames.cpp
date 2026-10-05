using __typeof_unqual = int;
using __typeof_unqual__ = int;
struct Box { __typeof_unqual member; };
__typeof_unqual echo(__typeof_unqual argument) { return argument; }
auto other() -> __typeof_unqual__ { return 1; }
template<class T> struct Holder { T value; };
Holder<__typeof_unqual> holder{1};
int main() {
  Box box{1};
  __typeof_unqual values[2]{1,2};
  __typeof_unqual* pointer = values;
  {
    int __typeof_unqual = 1;
    __typeof_unqual += 1;
    goto __typeof_unqual;
    __typeof_unqual: pointer[0] += __typeof_unqual;
    pointer[0] -= __typeof_unqual;
  }
  return echo((__typeof_unqual)box.member) + other() + holder.value + pointer[0] - 4;
}
