import { expect, test } from 'vitest';

import { Parser, Query } from '@willbooster/web-tree-sitter';

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
