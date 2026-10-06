namespace sample {
struct Target { int value = 42; };
inline Target target;
struct Scalar { operator int() const; operator bool() const; };
inline Scalar::operator int() const { return 42; }
[[nodiscard]] inline Scalar::operator bool() const { return true; }
struct Pointer { operator int*() const; };
inline Pointer::operator int*() const { return &target.value; }
struct Reference { operator const Target&() const; };
inline Reference::operator const Target&() const { return target; }
struct Peer { friend Scalar::operator int() const; friend Pointer::operator int*() const; };
struct Constant { constexpr operator int() const; };
constexpr Constant::operator int() const { return 42; }
struct Immediate { consteval operator int() const; };
consteval Immediate::operator int() const { return 42; }
struct Tried { operator int() const; };
inline Tried::operator int() const try { return 42; } catch (...) { return -1; }
struct Explicit { explicit operator bool() const; };
inline Explicit::operator bool() const { return true; }
struct Qualified { using Alias = int; operator Alias() const; };
inline Qualified::operator Alias() const { return 42; }
struct RefQualified { operator int() &; operator int() &&; };
inline RefQualified::operator int() & { return 42; }
inline RefQualified::operator int() && { return 42; }
template<class T> struct Box { operator T() const; };
template<class T> inline Box<T>::operator T() const { return T{}; }
struct Inline { inline operator int() const { return 42; } };
struct Virtual { virtual operator int() const { return 42; } };
struct Implementation : Virtual { operator int() const override { return 42; } };
struct Deleted { operator int() const = delete; };
int ordinary(int value) { return value + 1; }
}
int main() {
  sample::Scalar scalar;
  sample::RefQualified ref;
  return int(scalar) != 42 || !bool(scalar) || *static_cast<int*>(sample::Pointer{}) != 42 ||
    static_cast<const sample::Target&>(sample::Reference{}).value != 42 ||
    int(sample::Constant{}) != 42 || int(sample::Immediate{}) != 42 || int(sample::Tried{}) != 42 ||
    !bool(sample::Explicit{}) || int(sample::Qualified{}) != 42 || int(ref) != 42 ||
    int(sample::RefQualified{}) != 42 || int(sample::Box<int>{}) != 0 ||
    int(sample::Inline{}) != 42 || int(sample::Implementation{}) != 42 || sample::ordinary(41) != 42;
}
