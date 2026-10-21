#define FEATURE 1
#define andrew 1
#if 1 or 0 and 0
int logical() { return 1; }
#endif
#if (1 bitor 2 xor 3 bitand 4) not_eq 0
int bitwise() { return 2; }
#endif
#if not 0 and compl 0 not_eq 0
int unary() { return 3; }
#endif
#if 0
int first_branch();
#elif defined(FEATURE) and not defined(MISSING)
int second_branch();
#endif
#if andrew
int ordinary_identifier();
#endif
#define F(...) 1
#if F(not, compl, and, or, bitand, bitor, xor, not_eq, and_eq, or_eq, xor_eq, !, ~, *, &, &&, <=>) and not 0
int macro_branch();
#endif
int macro_value = F(not) + F(compl);
int macro_following;
int following() { return 4; }
