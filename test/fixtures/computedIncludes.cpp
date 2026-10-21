#define EMPTY
#define HEADER "empty.h"
#define HEADER_OF(name) <name.h>
#define HEADER_PATH(name) <name>
#include "empty.h"
#include HEADER
#include HEADER_OF(empty)
#include HEADER EMPTY
#include EMPTY HEADER
#include HEADER_OF(empty) EMPTY
#include HEADER_PATH(empty.h) EMPTY
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
