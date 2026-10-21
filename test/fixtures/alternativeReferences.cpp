int globalValue;
int bitand left = globalValue;
int and right = 1;
using LRef = int bitand;
typedef int and RRef;
using LArray = int(bitand)[3];
using RArray = int(and)[3];
int bitand returnReference() { return globalValue; }
auto trailingReference() -> int bitand { return globalValue; }
void takeAbstract(int bitand, int and);
void takeNamed(int bitand left, int and right);
int bitand (*callback)(int) = nullptr;
struct Box {
  int bitand field;
  int bitand get() bitand;
  int and move() and;
  operator int bitand() bitand;
};
int bitand Box::get() bitand { return globalValue; }
int and Box::move() and { return static_cast<int and>(globalValue); }
Box::operator int bitand() bitand { return globalValue; }
template<class... Ts> void packs(Ts bitand... values) {}
void direct(int value) {
  int bitand local(value);
  int bitand (grouped)(value);
  int and moved(1);
  int and (groupedMoved)(1);
  packs(value);
}
int following() { return 0; }
