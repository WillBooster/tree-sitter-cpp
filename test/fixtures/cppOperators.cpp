#include <compare>
#define OPERATOR_TEXT .* ->* <=>
#define HELPER(name, op)

struct Value {
  int member;
  auto operator<=>(const Value&) const = default;
};
int Value::*member = &Value::member;
int read(Value value, Value* pointer) {
  HELPER(NAME, .*);
  HELPER(NAME, ->*);
  HELPER(NAME, <=>);
  return value.*member + pointer->*member;
}
auto compare(int left, int right) { return left <=> right; }
const char* literal = ".* ->* <=>";
// .* ->* <=>
