#define OPERATOR_TEXT operator
struct Value {
  int value;
  constexpr Value operator+(Value other) const { return {value + other.value}; }
  constexpr bool operator==(const Value& other) const { return value == other.value; }
  constexpr explicit operator bool() const { return value != 0; }
  constexpr int& operator[](int) { return value; }
  constexpr int operator()(int offset) const { return value + offset; }
  Value& operator++() { ++value; return *this; }
};
constexpr Value operator-(Value left, Value right) { return {left.value - right.value}; }
namespace factory { struct Pointer { Value value; Value* operator->() { return &value; } }; }
constexpr unsigned long long operator""_distance(unsigned long long value) { return value; }
int main() {
  Value a{2}, b{3};
  Value sum = a + b;
  ++a;
  Value difference = sum - a;
  sum[0] = 5;
  factory::Pointer pointer{{5}};
  const char* text = "operator + and operator bool";
  // operator + and operator bool are comment text.
  return sum(1) != 6 || difference.value != 2 || !static_cast<bool>(sum) ||
    !(sum == pointer.value) || pointer->value != 5 || 3_distance != 3 || text[0] != 'o';
}
