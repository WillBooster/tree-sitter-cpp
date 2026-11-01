int first =
#if 1
#define VALUE(value) value
  1
#undef VALUE
#else
#error unsupported
#endif
;

int second =
#if 0
#error unsupported
#elif 0
#pragma clang diagnostic ignored "-Wunused"
#elifdef UNUSED
#define OMITTED 1
#else
#line 100 "conditionalInitializerDirectives.cpp"
  2
#undef OMITTED
#endif
;

int third =
#if 1
#if 0
#include <missing/*>
#define EMPTY(value) value /* multiline
#elif is not a directive here */
#else
#pragma clang diagnostic ignored "-Wunused"
#endif
  3
#if 0
#include "missing/*"
#error unsupported
#endif
#else
#error unsupported
#endif
;

int after() { return first + second + third; }
