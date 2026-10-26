#define MARK
#define B
#ifdef B
MARK
#endif
template<class T> struct Box { T value; };
#ifdef B
MARK
#endif
class Object { public: int value; };
#ifdef B
MARK
#endif
class Object object() { return {}; }
