#define EMPTY
#define HEADER "empty.h"
#define HEADER_OF(name) <name.h>
#define HEADER_PATH(name) <name>
#include "empty.h"
#include HEADER
#include HEADER_OF(empty)
#include HEADER EMPTY // trailing
#include EMPTY HEADER /* trailing */
#include HEADER_OF(empty) EMPTY
#include HEADER_PATH(empty.h) EMPTY
#include HEADER EMPTY _Pragma("GCC diagnostic push")
#include HEADER _Pragma("GCC diagnostic push") EMPTY
struct Pair { int value; };
void take(Pair);
void use(int);
int values[] = {
#include HEADER EMPTY
1, 2
};
void build() {
  take({
#include HEADER_PATH(empty.h) EMPTY
    .value = 3,
  });
  {
#include HEADER_OF(empty) EMPTY
    int local = 0; use(local);
  }
  int after;
}
int following() { return 1; }
