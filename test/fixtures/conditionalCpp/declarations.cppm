export module conditional_cpp;
#define MARK
#define B
#ifdef B
MARK
#endif
namespace n { int z; }
#ifdef B
MARK
#endif
using namespace n;
#ifdef B
MARK
#endif
export int exported();
