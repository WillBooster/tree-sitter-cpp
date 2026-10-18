import { Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';
import { loadCurrentWasmBuild } from './wasmBuild';
import fs from 'node:fs';

await Parser.init();
const language = await loadCurrentWasmBuild();

test('distinguishes built-in casts and type identification from calls', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(`auto a = static_cast<int>(first(), second());
auto b = dynamic_cast<Base *>(ptr);
auto c = reinterpret_cast<char *>(ptr);
auto d = const_cast<const int *>(ptr);
auto &e = typeid(unsigned long long);
auto &f = typeid(struct X);
auto &g = typeid(first(), second());
`)!;
  const query = new Query(language, fs.readFileSync(new URL('../../queries/highlights.scm', import.meta.url), 'utf8'));
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const casts = tree.rootNode.descendantsOfType('named_cast_expression');
    expect(casts).toHaveLength(4);
    expect(casts[0]!.childForFieldName('value')!.type).toBe('comma_expression');
    expect(casts[0]!.childForFieldName('type')!.text).toBe('int');
    const typeids = tree.rootNode.descendantsOfType('typeid_expression');
    expect(typeids.map((node) => node.childForFieldName('type')?.text)).toEqual([
      'unsigned long long',
      'struct X',
      undefined,
    ]);
    expect(typeids[2]!.childForFieldName('value')!.type).toBe('comma_expression');
    expect(tree.rootNode.descendantsOfType('call_expression').map((node) => node.text)).toEqual([
      'first()',
      'second()',
      'first()',
      'second()',
    ]);
    expect(
      query
        .captures(tree.rootNode)
        .filter((capture) => capture.name === 'keyword')
        .map((capture) => capture.node.text)
    ).toEqual(['static_cast', 'dynamic_cast', 'reinterpret_cast', 'const_cast', 'typeid', 'typeid', 'typeid']);
  } finally {
    query.delete();
    tree.delete();
    parser.delete();
  }
});

test('rejects argument-list syntax in built-in expression operands', () => {
  const parser = new Parser().setLanguage(language);
  try {
    for (const expression of [
      'typeid()',
      'typeid({42})',
      'typeid(args...)',
      'static_cast<int>()',
      'static_cast<int>({42})',
      'static_cast<int>(args...)',
      'static_cast<int>(1 + args...)',
      'static_cast<int, long>(value)',
    ]) {
      const tree = parser.parse(`auto value = ${expression};`)!;
      try {
        expect(tree.rootNode.hasError, expression).toBe(true);
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

test('retains pack expansions in lists and variadic array parameters', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(`template<class... T> void arrays(T... []);
template<class... T> void f(T... args) {
  call(args...);
  auto values = {args...};
  call<sizeof(args)...>();
  auto value = object[args...];
}`)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(tree.rootNode.descendantsOfType('parameter_pack_expansion').map((node) => node.text)).toEqual([
      'args...',
      'args...',
      'sizeof(args)...',
      'args...',
    ]);
    expect(
      tree.rootNode.descendantsOfType('variadic_parameter_declaration')[0]!.childForFieldName('declarator')!.type
    ).toBe('array_declarator');
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('keeps ambiguous typeid names as expressions regardless of qualification', () => {
  const parser = new Parser().setLanguage(language);
  try {
    for (const operand of ['value', 'ns::value', '::value', 'a::b::value', 'std::runtime_error', 'ns::T<int>']) {
      const tree = parser.parse(`auto &result = typeid(${operand});`)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        const node = tree.rootNode.descendantsOfType('typeid_expression')[0]!;
        expect(node.childForFieldName('value')?.text, operand).toBe(operand);
        expect(node.childForFieldName('type'), operand).toBeNull();
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

test('retains alignment and annotation pack expansions', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(`template<class... T> struct alignas(sizeof(T)...) S {
  alignas(T...) char storage[8];
  alignas(T*...) char pointers[8];
  [[=T...]] int annotated;
};
template<int... N> struct U { alignas(N...) char storage[8]; };`)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(tree.rootNode.descendantsOfType('alignas_qualifier').map((node) => node.text)).toEqual([
      'alignas(sizeof(T)...)',
      'alignas(T...)',
      'alignas(T*...)',
      'alignas(N...)',
    ]);
    expect(tree.rootNode.descendantsOfType('annotation')[0]!.firstNamedChild!.type).toBe('parameter_pack_expansion');
  } finally {
    tree.delete();
    parser.delete();
  }
});
