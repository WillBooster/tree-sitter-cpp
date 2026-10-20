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
