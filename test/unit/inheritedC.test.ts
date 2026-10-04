import { Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';
import { loadCurrentWasmBuild } from './wasmBuild';

await Parser.init();
const language = await loadCurrentWasmBuild();

test('preserves inherited C preprocessing and type operands alongside C++ raw strings', () => {
  const source = `#define ADD(x) x/**/+1
int typedef Alias;
_Pragma("GCC diagnostic push") Alias next;
int read(__builtin_va_list args) { return __builtin_va_arg(args, int); }
const char *raw = R"tag(_Pragma("not a directive"))tag";
`;
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  const query = new Query(
    language,
    '(preproc_function_def value: (preproc_arg) @body) (va_arg_expression type: (type_descriptor) @type)'
  );
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(query.captures(tree.rootNode).map(({ name, node }) => [name, node.text])).toEqual([
      ['body', 'x/**/+1'],
      ['type', 'int'],
    ]);
    expect(tree.rootNode.descendantsOfType('type_definition')[0]?.childForFieldName('declarator')?.text).toBe('Alias');
    expect(tree.rootNode.descendantsOfType('pragma_operator').map((n) => n.text)).toEqual([
      '_Pragma("GCC diagnostic push")',
    ]);
    expect(tree.rootNode.descendantsOfType('raw_string_content').map((n) => n.text)).toEqual([
      '_Pragma("not a directive")',
    ]);
    expect(tree.rootNode.lastNamedChild?.childForFieldName('declarator')?.childForFieldName('declarator')?.text).toBe(
      '*raw'
    );
  } finally {
    query.delete();
    tree.delete();
    parser.delete();
  }
});

test('preserves va_arg identifiers in C++ namespace and contextual positions', () => {
  const source = `namespace va_arg { int x; }
namespace alias = va_arg;
auto value = va_arg::x;
struct S { int va_arg; };
namespace E { enum E { va_arg = 1 }; }
bool check() { int va_arg = 0; return not va_arg; }
template<class va_arg> struct Box { va_arg value; };
`;
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(tree.rootNode.descendantsOfType('qualified_identifier').map((n) => n.text)).toContain('va_arg::x');
    expect(tree.rootNode.descendantsOfType('namespace_identifier').map((n) => n.text)).toContain('va_arg');
  } finally {
    tree.delete();
    parser.delete();
  }
});
