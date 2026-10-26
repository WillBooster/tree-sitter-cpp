#define REGISTER(...)
#define BEGIN_SCOPE
#define END_SCOPE
#define HIDDEN
#define ANNOTATE(...)
#define API
namespace detail { struct Item {}; }
REGISTER(detail::Item)
namespace after { int value; }
REGISTER(detail::Item)
struct Neighbour {};
REGISTER(detail::Item)
REGISTER(Neighbour)
int following;
BEGIN_SCOPE
template<class T> void preserved();
END_SCOPE
namespace boundary {}
HIDDEN
decltype(1) hidden() { return 1; }
ANNOTATE(tag)
int annotated();
API
int prefix();
Neighbour
multilineType();
int
ordinary(int value)
{ return value; }
REGISTER(
  detail::Item,
  (1 + 2)
)
class Final {};
