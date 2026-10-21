#define H(...) "empty.h"
#define EMPTY
int first[] = {
#include H(/*inside*/ R"(a"b)") EMPTY // raw tail
1
};
int second[] = {
#include H(R"tag(a"b
c)tag") EMPTY
2
};
int following() { return 3; }
