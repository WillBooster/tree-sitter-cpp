#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace baz { struct qux {}; }
namespace foo { template<class T> struct bar {}; }

void nestedTemplateStatements() {
  std::vector<std::vector<std::string>> vectors;
  std::vector<std::set<std::string>> sets;
  std::vector<std::unordered_set<std::string>> unordered;
  std::vector<foo::bar<baz::qux>> custom;
}
