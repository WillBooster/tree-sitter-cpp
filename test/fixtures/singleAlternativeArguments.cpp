#define OP(...) ((void)0)
#define SOME(...) 1
#define CHECK(...) 1
void consume() {
  OP(and);
  OP(bitand);
  int y = SOME(and) + 1;
  if (CHECK(bitand)) { return; }
  int z = 1;
}
int following() { return 0; }
