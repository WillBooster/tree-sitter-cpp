using Element = int;
namespace ns { using Element = int; }
template<class T> struct Box { T value; };

static_assert(sizeof(int(bitand)[3]) == sizeof(int[3]));
static_assert(sizeof(int(and)[3]) == sizeof(int[3]));
static_assert(sizeof(long(bitand)[3]) == sizeof(long[3]));
static_assert(sizeof(long(and)[3]) == sizeof(long[3]));
static_assert(sizeof(const int(bitand)[3]) == sizeof(const int[3]));
static_assert(sizeof(const int(and)[3]) == sizeof(const int[3]));
static_assert(sizeof(Element(bitand)[3]) == sizeof(Element[3]));
static_assert(sizeof(Element(and)[3]) == sizeof(Element[3]));
static_assert(sizeof(ns::Element(bitand)[3]) == sizeof(ns::Element[3]));
static_assert(sizeof(ns::Element(and)[3]) == sizeof(ns::Element[3]));
static_assert(sizeof(Box<int>(bitand)[3]) == sizeof(Box<int>[3]));
static_assert(sizeof(Box<int>(and)[3]) == sizeof(Box<int>[3]));

static_assert(sizeof(int(bitand)) == sizeof(int));
static_assert(sizeof(int(and)) == sizeof(int));
static_assert(sizeof(Element(bitand)[2][3]) == sizeof(Element[2][3]));
static_assert(sizeof(Box<int>(and)[2][3]) == sizeof(Box<int>[2][3]));

#define SINGLE(...) ((void)0)
void macroUses() { SINGLE(and); SINGLE(bitand); }
int value = 1;
auto builtinValue = sizeof(int(value));
auto namedValue = sizeof(Element(value));
int following() { return 0; }
