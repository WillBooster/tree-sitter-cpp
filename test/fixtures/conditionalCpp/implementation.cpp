module;
#define MARK
#define B
#ifdef B
MARK
#endif
module reserved_api;
int local() { return imported(); }
