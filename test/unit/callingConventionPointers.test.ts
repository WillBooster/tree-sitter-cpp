import { expect, test } from 'vitest';

import { Edit, Parser, Query, type Node, type Tree } from '@willbooster/web-tree-sitter';

import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();

test('parses calling conventions before pointers across C++ declarator contexts', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, '(ms_call_modifier) @modifier');
  const tree = parser.parse(`typedef int __stdcall *NumberPointer;
typedef int (__stdcall *CallbackPointer)(int);
struct Holder { int __stdcall *field; };
int __stdcall *function(int __stdcall *input) { return input; }
`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    expect(tree.rootNode.namedChildren.map((node) => node.type)).toEqual([
      'type_definition',
      'type_definition',
      'struct_specifier',
      'function_definition',
    ]);
    const modifiers = query.captures(tree.rootNode).map(({ node }) => node);
    expect(modifiers).toHaveLength(5);
    expect(modifiers.every((node) => node.parent?.type === 'pointer_declarator')).toBe(true);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('keeps calling-convention pointers inside trailing return types', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, '(trailing_return_type (type_descriptor declarator: (_) @return))');
  const tree = parser.parse(`auto pointer() -> int __stdcall *;
auto callback() -> int (__stdcall *)(int);
auto cdecl_callback() -> int (__cdecl *)(int);
auto nested_callback() -> int (__stdcall (*))(int);
auto reference_callback() -> int (__stdcall &)(int);
struct __stdcall_type {};
auto named_type() -> __stdcall_type *;
auto plain_pointer() -> int *;
auto plain_callback() -> int (*)(int);
`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    const returns = query.captures(tree.rootNode).map(({ node }) => node);
    expect(returns.map((node) => node.text)).toEqual([
      '__stdcall *',
      '(__stdcall *)(int)',
      '(__cdecl *)(int)',
      '(__stdcall (*))(int)',
      '(__stdcall &)(int)',
      '*',
      '*',
      '(*)(int)',
    ]);
    expect(returns.map((node) => node.type)).toEqual([
      'abstract_pointer_declarator',
      'abstract_function_declarator',
      'abstract_function_declarator',
      'abstract_function_declarator',
      'abstract_function_declarator',
      'abstract_pointer_declarator',
      'abstract_pointer_declarator',
      'abstract_function_declarator',
    ]);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('parses conventions on allocated pointers and pointers to C++ members', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    `
    (new_expression declarator: (_) @allocation)
    (alias_declaration type: (type_descriptor declarator: (_) @member))
    (trailing_return_type (type_descriptor declarator: (_) @callback))
  `
  );
  const tree = parser.parse(`struct Holder { int method(int); };
using MemberPointer = int __stdcall Holder::*;
using PlainMember = int Holder::*;
auto member_callback() -> int (__stdcall Holder::*)(int);
auto create() { return new int __stdcall *[2]; }
`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    expect(query.captures(tree.rootNode).map(({ name, node }) => [name, node.type, node.text])).toEqual([
      ['member', 'qualified_identifier', '__stdcall Holder::*'],
      ['member', 'qualified_identifier', 'Holder::*'],
      ['callback', 'abstract_function_declarator', '(__stdcall Holder::*)(int)'],
      ['allocation', 'abstract_pointer_declarator', '__stdcall *[2]'],
    ]);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('retains convention placement before nested pointers and references', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, '(ms_call_modifier) @modifier');
  const tree = parser.parse(`int (__stdcall (*pointer))(int);
int (__stdcall &reference)(int) = *pointer;
int (__stdcall (&nested_reference))(int) = *pointer;
`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    const modifiers = query.captures(tree.rootNode).map(({ node }) => node);
    expect(modifiers.map((node) => node.parent?.type)).toEqual([
      'parenthesized_declarator',
      'parenthesized_declarator',
      'parenthesized_declarator',
    ]);
    expect(tree.rootNode.namedChildren).toHaveLength(3);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test.each([
  ['*', 'nullptr', 'pointer_declarator'],
  ['&', 'storage', 'reference_declarator'],
  ['&&', 'static_cast<int&&>(storage)', 'reference_declarator'],
])('retains functions and fields with conventions after %s through edits', async (operator, value, declaratorType) => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, '(function_declarator declarator: (_) @name) (ms_call_modifier) @modifier');
  const original = `using Value = int;
int storage;
int ${operator} __stdcall defined() { return ${value}; }
int ${operator} __stdcall declared();
Value ${operator} __stdcall Alloc(int n);
struct S { int ${operator} __stdcall method(); };
int *after() { return nullptr; }
`;
  let source = original;
  let tree = parser.parse(source)!;
  try {
    for (const replacement of ['__cdecl', '__stdcall']) {
      expect(tree.rootNode.hasError).toBe(false);
      const captures = query.captures(tree.rootNode);
      expect(captures.filter(({ name }) => name === 'name').map(({ node }) => node.text)).toEqual([
        'defined',
        'declared',
        'Alloc',
        'method',
        'after',
      ]);
      const modifiers = captures.filter(({ name }) => name === 'modifier').map(({ node }) => node);
      expect(modifiers).toHaveLength(4);
      expect(modifiers.every((node) => node.parent?.type === declaratorType)).toBe(true);
      const definitions = tree.rootNode.descendantsOfType('function_definition');
      expect(definitions.map((node) => node.text)).toEqual([
        `int ${operator} ${modifiers[0]!.text} defined() { return ${value}; }`,
        'int *after() { return nullptr; }',
      ]);
      for (const node of [...definitions, ...modifiers]) {
        expect(source.slice(node.startIndex, node.endIndex)).toBe(node.text);
        expect([node.startPosition, node.endPosition]).toEqual([
          point(source, node.startIndex),
          point(source, node.endIndex),
        ]);
      }
      const start = source.indexOf(modifiers[0]!.text);
      const end = start + modifiers[0]!.text.length;
      const next = source.slice(0, start) + replacement + source.slice(end);
      tree.edit(
        new Edit({
          startIndex: start,
          oldEndIndex: end,
          newEndIndex: start + replacement.length,
          startPosition: point(source, start),
          oldEndPosition: point(source, end),
          newEndPosition: point(next, start + replacement.length),
        })
      );
      let incremental: Tree | undefined;
      const fresh = parser.parse(next)!;
      try {
        incremental = parser.parse(next, tree)!;
        expect(incremental.rootNode.hasError).toBe(false);
        expect(snapshot(incremental.rootNode)).toEqual(snapshot(fresh.rootNode));
        expect(query.captures(incremental.rootNode).map(({ name, node }) => [name, snapshot(node)])).toEqual(
          query.captures(fresh.rootNode).map(({ name, node }) => [name, snapshot(node)])
        );
        tree.delete();
        tree = incremental;
        incremental = undefined;
        source = next;
      } finally {
        incremental?.delete();
        fresh.delete();
      }
    }
    expect(source).toBe(original);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

function point(source: string, index: number): { row: number; column: number } {
  const prefix = source.slice(0, index);
  return { row: prefix.split('\n').length - 1, column: prefix.length - prefix.lastIndexOf('\n') - 1 };
}

function snapshot(node: Node): unknown {
  return [
    node.type,
    node.isNamed,
    node.isExtra,
    node.isMissing,
    node.hasError,
    node.startIndex,
    node.endIndex,
    node.startPosition,
    node.endPosition,
    node.children.map((child, index) => [node.fieldNameForChild(index), snapshot(child)]),
  ];
}
