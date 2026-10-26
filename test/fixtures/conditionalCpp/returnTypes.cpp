#define MARK
#define B
using Alias = int;
namespace NS { using Type = int; template<class T> struct Box {}; }
struct Object {};
template<class T> struct Carrier { using Type = int; };
#ifdef B
MARK
#endif
int primitive() { return 0; }
#ifdef B
MARK
#endif
Alias opaque() { return 0; }
#ifdef B
MARK
#endif
NS::Type qualified() { return 0; }
#ifdef B
MARK
#endif
NS::Box<int> templated() { return {}; }
#ifdef B
MARK
#endif
class Object elaborated() { return {}; }
template<class T>
#ifdef B
MARK
#endif
typename T::Type dependent() { return 0; }
struct Virtual {
#ifdef B
MARK
#endif
virtual int method() { return 0; }
};
#ifdef B
MARK
#endif
consteval int immediate() { return 0; }
