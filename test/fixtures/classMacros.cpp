#define API __attribute__((visibility("default")))
#define LOCAL
struct Base {};
class API Derived final : public Base { public: int value; };
class API Plain { int value; };
struct API LOCAL Payload { int value; };
union API Value { int value; };
struct Type {};
class Type object{};
class Type copied = {};
class Type *makeType();
class Forward;
int (parenthesized()) { return 0; }
int ((nested())) { return 0; }
int (*returnPointer())(int) { return nullptr; }
int (&returnReference())[3] { static int values[3]; return values; }
int (*returnArrayPointer())[3] { return nullptr; }
struct Member { int value; };
int Member::* memberPointer() { return &Member::value; }
