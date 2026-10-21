#define OPERATOR_WORDS and and_eq bitand bitor compl not not_eq or or_eq xor xor_eq
#define IGNORE(name, expression)

struct WordOperators {
  bool operator and(const WordOperators&) const;
  bool operator or(const WordOperators&) const;
  bool operator not() const;
  bool operator not_eq(const WordOperators&) const;
  WordOperators operator bitand(const WordOperators&) const;
  WordOperators operator bitor(const WordOperators&) const;
  WordOperators operator xor(const WordOperators&) const;
  WordOperators operator compl() const;
  WordOperators& operator and_eq(const WordOperators&);
  WordOperators& operator or_eq(const WordOperators&);
  WordOperators& operator xor_eq(const WordOperators&);
};

bool logic(bool left, bool right) { return (left and right) or not left; }
int bits(int left, int right) { return compl left bitor right xor left bitand right; }
bool difference(int left, int right) { return left not_eq right; }
int assignment(int left, int right) {
  left and_eq right;
  left or_eq right;
  left xor_eq right;
  return left;
}
void macroWords(int left, int right) {
  IGNORE(NAME, left and right);
  IGNORE(NAME, left or right);
  IGNORE(NAME, not left);
  IGNORE(NAME, left not_eq right);
  IGNORE(NAME, left bitand right);
  IGNORE(NAME, left bitor right);
  IGNORE(NAME, left xor right);
  IGNORE(NAME, compl left);
  IGNORE(NAME, left and_eq right);
  IGNORE(NAME, left or_eq right);
  IGNORE(NAME, left xor_eq right);
}
int anderson, notable, orchid;
const char* quoted = "and and_eq bitand bitor compl not not_eq or or_eq xor xor_eq";
const char* raw = R"(and and_eq bitand bitor compl not not_eq or or_eq xor xor_eq)";
// and and_eq bitand bitor compl not not_eq or or_eq xor xor_eq
