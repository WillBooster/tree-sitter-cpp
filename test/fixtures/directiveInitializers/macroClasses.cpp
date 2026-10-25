#define API __attribute__((visibility("default")))
#define FLAG 1
class API Conditional {
#if FLAG
  int first;
#else
  int other;
#endif
};
struct API Nested {
#if FLAG
#ifdef FLAG
  int nested;
#else
  int alternate;
#endif
#elif 0
  int another;
#else
  int last;
#endif
};
class API Methods {
#if FLAG
  static int read() { return 1; }
#endif
};
struct API Mixed {
#include "empty.inc"
  int visible;
};
