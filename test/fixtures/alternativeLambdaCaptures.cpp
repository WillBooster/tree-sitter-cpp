int capture(int x) {
  auto defaultRef = [bitand] { return x; };
  auto mixed = [bitand, x] { return x; };
  auto named = [bitand x] { return x; };
  auto init = [bitand alias = x] { return alias; };
  auto valueDefault = [=, bitand x] { return x; };
  return defaultRef() + mixed() + named() + init() + valueDefault();
}
template<class... Ts> auto capturePacks(Ts... values) {
  auto named = [bitand values...] { return sizeof...(values); };
  auto init = [bitand... refs = values] { return sizeof...(refs); };
  return named() + init();
}
int following() { return capture(1) + capturePacks(2, 3); }
