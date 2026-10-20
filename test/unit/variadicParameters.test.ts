import { Edit, Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';
import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();
const language = await loadCurrentWasmBuild();

test('exposes terminal variadic parameters while preserving template packs', () => {
  const source = `void first(...);
void second(int value, ...);
void third(int value...);
void unnamed(int...);
void sized(unsigned long...);
void pointers(int*...);
void wide(wchar_t...);
void after(int value, unsigned long...);
void keywordPrefixes(int intensity, double doubleValue);
template<class... Ts> void packed(Ts... values);
template<class... size_t> void libraryNamedPack(size_t...);
`;
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  let query: Query | undefined;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    query = new Query(language, '(parameter_list (variadic_parameter) @ellipsis)');
    const captures = query.captures(tree.rootNode);
    expect(captures.map(({ node }) => node.parent?.parent?.childForFieldName('declarator')?.text)).toEqual([
      'first',
      'second',
      'third',
      'unnamed',
      'sized',
      'pointers',
      'wide',
      'after',
    ]);
    for (const { node } of captures) {
      expect(node.text).toBe('...');
      expect(node.parent?.namedChildren.at(-1)?.startIndex).toBe(node.startIndex);
    }
    const prefixControl = tree.rootNode
      .descendantsOfType('function_declarator')
      .find((node) => node.childForFieldName('declarator')?.text === 'keywordPrefixes')!;
    expect(
      prefixControl
        .childForFieldName('parameters')
        ?.namedChildren.map((node) => node.childForFieldName('declarator')?.text)
    ).toEqual(['intensity', 'doubleValue']);
    expect(tree.rootNode.descendantsOfType('variadic_parameter_declaration').map((node) => node.text)).toEqual([
      'Ts... values',
      'size_t...',
    ]);
  } finally {
    query?.delete();
    tree.delete();
    parser.delete();
  }
});

test('rejects parameters after a variadic ellipsis and a leading comma', () => {
  const parser = new Parser().setLanguage(language);
  try {
    for (const source of ['void f(..., int value);', 'void f(int, ..., ...);', 'void f(, ...);']) {
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, source).toBe(true);
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

test('keeps function varargs separate from exception declarations', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, '(catch_clause parameters: (parameter_list (variadic_parameter) @catch_all))');
  const source = `void handled() {
  try {}
  catch (int) {}
  catch ([[maybe_unused]] double value) {}
  catch (int (*callback)(int, ...)) {}
  catch (...) {}
}`;
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(tree.rootNode.descendantsOfType('catch_clause')).toHaveLength(4);
    expect(query.captures(tree.rootNode).map(({ node }) => node.text)).toEqual(['...']);
    for (const parameters of [
      'int value...',
      'int...',
      'int value, ...',
      '..., int value',
      'int first, int second',
      '',
    ]) {
      const invalid = parser.parse(`void f() { try {} catch (${parameters}) {} }`)!;
      try {
        expect(invalid.rootNode.hasError, parameters).toBe(true);
      } finally {
        invalid.delete();
      }
    }
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('recovers conditional parameter lists without consuming following definitions', () => {
  const parser = new Parser().setLanguage(language);
  const following = 'int after1(int value) { return value; }\nint after2(int value) { return value; }\n';
  try {
    for (const conditional of [
      '#ifdef A\n int second\n#else\n long second\n#endif\n',
      '#ifdef VARARGS\n ...\n#else\n int second\n#endif\n',
    ]) {
      const prefix = 'void conditional(int first,\n';
      const suffix = ');\n' + following;
      let previous = conditional;
      let tree = parser.parse(prefix + previous + suffix)!;
      try {
        for (const parameters of [conditional, 'int second\n', conditional]) {
          tree.edit(
            new Edit({
              startIndex: prefix.length,
              oldEndIndex: prefix.length + previous.length,
              newEndIndex: prefix.length + parameters.length,
              startPosition: { row: 1, column: 0 },
              oldEndPosition: { row: 1 + previous.split('\n').length - 1, column: 0 },
              newEndPosition: { row: 1 + parameters.split('\n').length - 1, column: 0 },
            })
          );
          const source = prefix + parameters + suffix;
          const next = parser.parse(source, tree)!;
          tree.delete();
          tree = next;
          const fresh = parser.parse(source)!;
          try {
            expect(tree.rootNode.toString()).toBe(fresh.rootNode.toString());
            expect(
              tree.rootNode
                .descendantsOfType('function_definition')
                .map((node) => [node.text, node.startIndex, node.endIndex])
            ).toEqual([
              [
                'int after1(int value) { return value; }',
                source.indexOf('int after1'),
                source.indexOf('int after1') + 'int after1(int value) { return value; }'.length,
              ],
              [
                'int after2(int value) { return value; }',
                source.indexOf('int after2'),
                source.indexOf('int after2') + 'int after2(int value) { return value; }'.length,
              ],
            ]);
          } finally {
            fresh.delete();
          }
          previous = parameters;
        }
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});
