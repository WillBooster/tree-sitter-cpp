import { Parser, Query } from '@willbooster/web-tree-sitter';
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
