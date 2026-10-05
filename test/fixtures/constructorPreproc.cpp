struct Leading {
  int a, b, c;
  explicit Leading(int n) : a(n)
#if SELECT == 0
    , b(n + 1)
#elif SELECT == 1
    , b{n + 2}
#else
    , b(n + 3)
#endif
    , c(n + 4) {}
};
struct Trailing {
  int a, b, c;
  explicit Trailing(int n) :
#if SELECT == 0
    a(n),
#elifdef FEATURE
    a{n + 1},
#elifndef FEATURE
    a(n + 2),
#else
    a(n + 3),
#endif
    b(n + 3), c(n + 4) {}
};
struct AllGuarded {
  int a;
  AllGuarded() :
#if SELECT == 0
    a(1)
#else
    a{2}
#endif
  {}
};
struct Nested {
  int a, b, c;
  explicit Nested(int n) : a(n)
#ifndef FEATURE
#if SELECT == 0
    , b(n + 1)
#else
    , b{n + 2}
#endif
#else
    , b(n + 3)
#endif
    , c(n + 4) {}
};
struct Base { explicit Base(int) {} };
struct Out : Base {
  int a, b;
  explicit Out(int);
};
Out::Out(int n) try :
#ifdef FEATURE
  Base(n),
#else
  Base{n + 1},
#endif
  a(n),
#if SELECT == 0
  b(n + 1)
#else
  b{n + 2}
#endif
{} catch (...) { throw; }
int after() { Leading l(1); Trailing t(2); AllGuarded a; Nested n(3); Out o(4); return l.a + t.a + a.a + n.a + o.a; }
int main() { return after() < 0; }
