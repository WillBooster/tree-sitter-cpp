struct Pair { int first; int second; };

int global =
#if SELECT
  11
#elif ALTERNATE
  12
#else
  13
#endif
;

Pair coordinates =
#if SELECT
  {1, 2}
#else
  {3, 4}
#endif
;

struct Holder {
  int member =
#ifdef MEMBER_VALUE
    21
#else
    22
#endif
  ;
};

Holder designated{
  .member =
#ifndef OTHER
    23
#else
    24
#endif
};

int alternatives =
#if SELECT
  31
#elif ALTERNATE
  32
#elifdef FLAG
  33
#elifndef OMITTED
  34
#else
  35
#endif
;

int emptyPrimary =
#if 0
#else
  41
#endif
;

int emptyAlternative =
#if 1
  42
#else
#endif
;

int delayed =
#if 0
#elif 0
#elifndef OMITTED
  43
#else
#endif
;

int read() {
  int (parenthesized) =
#if SELECT
    51
#else
    52
#endif
  ;
  int nested =
#if SELECT
#if ALTERNATE
    61
#else
    62
#endif
#else
    63
#endif
  ;
  return parenthesized + nested;
}

int emptyNestedPrimary =
#if 0
#if NESTED_EMPTY
#endif
#else
44
#endif
;

int emptyNestedAlternative =
#if 1
#if BEFORE_VALUE
#else
#ifdef NESTED_EMPTY
#endif
#endif
45
#ifndef AFTER_VALUE
#endif
#else
#if UNUSED
#endif
#endif
;

int literalConditions =
#if 1
#if '/' == '/' && '*' == '*'
#endif
#if __has_include("missing/*")
#endif
#if __has_include(<missing/*>)
#endif
#if 1 < 2 /* multiline
comment */
#endif
46
#else
47
#endif
;

int after() { return 0; }
