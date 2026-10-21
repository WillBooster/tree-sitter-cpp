#define TAKE(...) ((void)0)
#define CHECK(...) 1
#if CHECK(and, or, bitand, bitor, xor, not_eq, and_eq, or_eq, xor_eq, not, compl)
int branch;
#endif
void consume() {
  TAKE(and, or, bitand, bitor, xor, not_eq, and_eq, or_eq, xor_eq, not, compl);
  TAKE(or);
  TAKE(bitor);
  TAKE(xor);
  TAKE(not_eq);
  TAKE(and_eq);
  TAKE(or_eq);
  TAKE(xor_eq);
}
int following() { return branch; }
