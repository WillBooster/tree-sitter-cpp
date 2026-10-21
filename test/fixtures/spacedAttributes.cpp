namespace N { extern int value; }
int N::value [ /*left*/ [ maybe_unused ] /*right*/ ] = 0;
int &reference [ [maybe_unused] ] = N::value;
struct Holder { int &field [ [maybe_unused] ]; };
typedef int &Reference [ [maybe_unused] ];
[ [using gnu: unused] ] int qualified = 0;
[[using gnu:]] int emptyPrefix = 0;
[[]] int empty = 0;
[[using gnu: unused,]] int trailing = 0;
[[,]] int slots = 0;
int array[[maybe_unused]][3];
void bytes(unsigned char[20]);
int following() { return reference; }
int allocated() {
  auto values = new int * [[clang::annotate_type("ptr")]] [3];
  delete[] values;
  return 0;
}
int indexed(int *values) { return values[([] { return 0; }())]; }
