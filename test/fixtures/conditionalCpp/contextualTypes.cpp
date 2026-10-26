#define MARK
#define B
using module = int;
using import = long;
#ifdef B
MARK
#endif
::module moduleValue() { return 0; }
#ifdef B
MARK
#endif
::import importValue() { return 0; }
struct Fields {
#ifdef B
MARK
#endif
  module value;
#ifdef B
MARK
#endif
  import other;
};
