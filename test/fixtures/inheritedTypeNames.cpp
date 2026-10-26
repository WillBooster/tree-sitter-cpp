namespace ns { template<class T> struct Box { T value; }; }
class Type {};
int static long global;
unsigned const long qualified = 1;
typedef unsigned const long int Alias;
struct Holder { int const long member; int static long shared; ns::Box<Type> nested; };
long contexts(int const long argument) {
  long static int local = argument;
  local += (unsigned const long)1;
  local += sizeof(int const long);
  local += alignof(unsigned const long);
  return local;
}
void cppTypes(Type value, ns::Box<Type> box) {
  (void)sizeof(class Type);
  (void)sizeof(ns::Box<Type>);
  (void)sizeof(decltype(value));
  (void)static_cast<ns::Box<Type>>(box);
  (void)static_cast<Type>(value);
}
template<class T> void dependent(T value) {
  (void)sizeof(typename T::value_type);
  (void)static_cast<typename T::value_type>(value.value);
}
int following() { return 0; }
