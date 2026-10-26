#define MARK
#define B
#ifdef B
MARK
#endif
import reserved_api;
int read() { return imported(); }
