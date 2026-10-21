using Element = int;
namespace ns { using Element = int; }
template<class T> struct Box { T value; };
struct ReferenceOwner { int value; };

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

static_assert(sizeof(int*(bitand)) == sizeof(int*));
static_assert(sizeof(int(*(bitand))[3]) == sizeof(int(*)[3]));
static_assert(sizeof(int(*(and))[3]) == sizeof(int(*)[3]));
static_assert(sizeof(int(*(bitand))(int)) == sizeof(int(*)(int)));
static_assert(sizeof(int ReferenceOwner::*(bitand)) == sizeof(int ReferenceOwner::*));
static_assert(sizeof(int((and))[3]) == sizeof(int[3]));

#define SINGLE(...) ((void)0)
void macroUses() { SINGLE(and); SINGLE(bitand); }
int value = 1;
auto builtinValue = sizeof(int(value));
auto namedValue = sizeof(Element(value));
#define ID(...) 1
int directAnd(ID(and));
int directBitand(ID(bitand));
int localUses() { int localAnd(ID(and)); int localBitand(ID(bitand)); return localAnd + localBitand; }
struct Holder { int memberAnd = ID(and); int memberBitand = ID(bitand); };
int function();
int values[3];
auto callSize = sizeof(function());
auto subscriptSize = sizeof(values[0]);
int following() { return 0; }
