#include "types.inc"
struct S emptyIfdef{
#ifdef FLAG
#endif
};
