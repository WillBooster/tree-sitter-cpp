#include "types.inc"
struct S emptyBranches{
#if FLAG
#ifdef SECONDARY
#include "value.inc"
#else
#define ARM 1
#endif
#elif OTHER_FLAG
#define GET_VALUE() 1
#else
#undef ARM
#endif
};
