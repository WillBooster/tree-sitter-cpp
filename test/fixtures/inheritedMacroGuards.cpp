#define ANNOTATE(...)
extern ANNOTATE(int, (1 + 2), "comma,inside") int prototype();
#define MARKER
#ifdef ENABLE
MARKER
#endif
int conditional();
#define UNUSED __attribute__((unused))
struct Item { int value; };
void parameter(Item *UNUSED item) { (void)item; }
int following() { return 0; }
