#define ACTIVE 1
#define PUSH_STATE
#define OPEN_SCOPE
#define OPEN_ABI
#define CLOSE_ABI
#define CLOSE_SCOPE
#if defined(ACTIVE)
PUSH_STATE
#include "stackedMacroLines.h"
OPEN_SCOPE
OPEN_ABI
template<class T, class U>
class Channel { T first; U second; };
#if defined(ACTIVE)
OPEN_SCOPE
OPEN_ABI
template<class T> T preserved(T value) { return value; }
CLOSE_ABI
CLOSE_SCOPE
#endif
CLOSE_ABI
CLOSE_SCOPE
#endif
int following;
