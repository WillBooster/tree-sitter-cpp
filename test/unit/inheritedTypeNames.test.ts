import fs from 'node:fs';

import { Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild';

await Parser.init();
const language = await loadCurrentWasmBuild();

test('retains complete interleaved type operands and C++ type kinds through inherited C rules', () => {
  const source = fs.readFileSync(new URL('../fixtures/inheritedTypeNames.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  const query = new Query(language, '(type_descriptor type: (type_specifier) @type)');
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(
      tree.rootNode
        .descendantsOfType('field_declaration')
        .map((node) => [node.childForFieldName('type')?.text, node.childForFieldName('declarator')?.text])
    ).toEqual([
      ['T', 'value'],
      ['int const long', 'member'],
      ['int static long', 'shared'],
      ['ns::Box<Type>', 'nested'],
    ]);
    expect(tree.rootNode.descendantsOfType('storage_class_specifier').map((node) => node.text)).toEqual([
      'static',
      'static',
      'static',
    ]);
    const captures = query.captures(tree.rootNode).map(({ node }) => [node.type, node.text]);
    expect(captures).toEqual(
      expect.arrayContaining([
        ['sized_type_specifier', 'unsigned const long'],
        ['sized_type_specifier', 'int const long'],
        ['class_specifier', 'class Type'],
        ['qualified_identifier', 'ns::Box<Type>'],
        ['decltype', 'decltype(value)'],
        ['dependent_type', 'typename T::value_type'],
      ])
    );
    for (const { node } of query.captures(tree.rootNode)) {
      expect(source.slice(node.startIndex, node.endIndex)).toBe(node.text);
      expect(node.parent?.childForFieldName('type')?.id).toBe(node.id);
    }
    expect(tree.rootNode.descendantsOfType('function_definition').at(-1)?.text).toBe('int following() { return 0; }');
  } finally {
    query.delete();
    tree.delete();
    parser.delete();
  }
});

test('rejects storage in type operands', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, '(type_descriptor type: (sized_type_specifier) @type)');
  try {
    for (const expression of ['sizeof(int const long)', 'alignof(int const long)', 'static_cast<int const long>(1)']) {
      const source = `void f() { (void)${expression}; }\nint following() { return 0; }`;
      const start = source.indexOf('const');
      for (const replacement of ['const', 'static']) {
        const replaced = source.slice(0, start) + replacement + source.slice(start + 'const'.length);
        const tree = parser.parse(replaced)!;
        try {
          expect(tree.rootNode.hasError, replaced).toBe(replacement === 'static');
          expect(tree.rootNode.descendantsOfType('function_definition').at(-1)?.text).toBe(
            'int following() { return 0; }'
          );
          if (replacement === 'const') {
            expect(
              query.captures(tree.rootNode).map(({ node }) => [node.text, node.startIndex, node.endIndex])
            ).toEqual([['int const long', source.indexOf('int const long'), source.indexOf('int const long') + 14]]);
          }
        } finally {
          tree.delete();
        }
      }
    }
  } finally {
    query.delete();
    parser.delete();
  }
});

test('dispatches inherited macro guards without consuming C++ declarators', () => {
  const source = fs.readFileSync(new URL('../fixtures/inheritedMacroGuards.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  const query = new Query(
    language,
    `(attribute_specifier name: (identifier) @attribute)
(preproc_ifdef (macro_specifier name: (identifier) @marker))
(pointer_declarator (macro_specifier name: (identifier) @pointer))`
  );
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(
      query.captures(tree.rootNode).map(({ name, node }) => [name, node.text, node.startIndex, node.endIndex])
    ).toEqual([
      ['attribute', 'ANNOTATE', source.indexOf('ANNOTATE(int'), source.indexOf('ANNOTATE(int') + 8],
      ['marker', 'MARKER', source.indexOf('MARKER\n#endif'), source.indexOf('MARKER\n#endif') + 6],
      ['pointer', 'UNUSED', source.indexOf('UNUSED item'), source.indexOf('UNUSED item') + 6],
    ]);
    expect(
      tree.rootNode.descendantsOfType('declaration').map((node) => node.childForFieldName('declarator')?.text)
    ).toEqual(['prototype()', 'conditional()']);
    expect(
      tree.rootNode.descendantsOfType('function_definition').map((node) => node.childForFieldName('declarator')?.text)
    ).toEqual(['parameter(Item *UNUSED item)', 'following()']);
  } finally {
    query.delete();
    tree.delete();
    parser.delete();
  }
});
