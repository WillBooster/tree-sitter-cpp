import { Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { childrenOfType, collectCaptures, fieldChild, pointAt, trailingReturnDeclarators } from '../helpers/ast.js';
import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();

test('parses calling conventions before pointers across C++ declarator contexts', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
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
    const modifiers = tree.rootNode.descendantsOfType('ms_call_modifier');
    expect(modifiers).toHaveLength(5);
    expect(modifiers.every((node) => node.parent?.type === 'pointer_declarator')).toBe(true);
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('keeps calling-convention pointers inside trailing return types', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
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
    const returns = trailingReturnDeclarators(tree.rootNode);
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
    parser.delete();
  }
});

test('parses conventions on allocated pointers and pointers to C++ members', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(`struct Holder { int method(int); };
using MemberPointer = int __stdcall Holder::*;
using PlainMember = int Holder::*;
auto member_callback() -> int (__stdcall Holder::*)(int);
auto create() { return new int __stdcall *[2]; }
`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    const captures = collectCaptures(tree.rootNode, (node, emit) => {
      if (node.type === 'new_expression') emit('allocation', fieldChild(node, 'declarator'));
      if (node.type === 'alias_declaration') {
        emit('member', fieldChild(fieldChild(node, 'type', 'type_descriptor'), 'declarator'));
      }
      if (node.type === 'trailing_return_type') {
        for (const descriptor of childrenOfType(node, 'type_descriptor')) {
          emit('callback', fieldChild(descriptor, 'declarator'));
        }
      }
    });
    expect(captures.map(({ name, node }) => [name, node.type, node.text])).toEqual([
      ['member', 'qualified_identifier', '__stdcall Holder::*'],
      ['member', 'qualified_identifier', 'Holder::*'],
      ['callback', 'abstract_function_declarator', '(__stdcall Holder::*)(int)'],
      ['allocation', 'abstract_pointer_declarator', '__stdcall *[2]'],
    ]);
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('retains convention placement before nested pointers and references', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(`int (__stdcall (*pointer))(int);
int (__stdcall &reference)(int) = *pointer;
int (__stdcall (&nested_reference))(int) = *pointer;
`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    const modifiers = tree.rootNode.descendantsOfType('ms_call_modifier');
    expect(modifiers.map((node) => node.parent?.type)).toEqual([
      'parenthesized_declarator',
      'parenthesized_declarator',
      'parenthesized_declarator',
    ]);
    expect(tree.rootNode.namedChildren).toHaveLength(3);
    for (const node of modifiers) {
      const enclosed = node.parent!.childForFieldName('declarator');
      expect(enclosed).not.toBeNull();
      expect(enclosed!.equals(node)).toBe(false);
      expect(enclosed!.type).not.toBe('ms_call_modifier');
    }
  } finally {
    tree.delete();
    parser.delete();
  }
});

test.each([
  ['* __stdcall', 'nullptr', 'pointer_declarator'],
  ['& __stdcall', 'storage', 'reference_declarator'],
  ['&& __stdcall', 'static_cast<int&&>(storage)', 'reference_declarator'],
  ['* __stdcall const', 'nullptr', 'pointer_declarator'],
  ['* __stdcall volatile', 'nullptr', 'pointer_declarator'],
  ['* __stdcall __ptr32', 'nullptr', 'pointer_declarator'],
  ['* const __stdcall __ptr32 volatile', 'nullptr', 'pointer_declarator'],
])('retains functions and fields with %s', async (prefix, value, declaratorType) => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const original = `using Value = int;
int storage;
int ${prefix} defined() { return ${value}; }
int ${prefix} declared();
Value ${prefix} Alloc(int n);
struct S { int ${prefix} method(); };
int *after() { return nullptr; }
`;
  try {
    for (const convention of ['__stdcall', '__cdecl']) {
      const source = original.replace('__stdcall', convention);
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError).toBe(false);
        const names = tree.rootNode
          .descendantsOfType('function_declarator')
          .flatMap((node) => fieldChild(node, 'declarator') ?? []);
        expect(names.map((node) => node.text)).toEqual(['defined', 'declared', 'Alloc', 'method', 'after']);
        const modifiers = tree.rootNode.descendantsOfType('ms_call_modifier');
        expect(modifiers).toHaveLength(4);
        expect(modifiers.every((node) => node.parent?.type === declaratorType)).toBe(true);
        const definitions = tree.rootNode.descendantsOfType('function_definition');
        expect(definitions.map((node) => node.text)).toEqual([
          `int ${prefix.replace('__stdcall', convention)} defined() { return ${value}; }`,
          'int *after() { return nullptr; }',
        ]);
        for (const node of [...definitions, ...modifiers]) {
          expect(source.slice(node.startIndex, node.endIndex)).toBe(node.text);
          expect([node.startPosition, node.endPosition]).toEqual([
            pointAt(source, node.startIndex),
            pointAt(source, node.endIndex),
          ]);
        }
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});
