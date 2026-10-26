import fs from 'node:fs';

import { Edit, Parser, Query, type Tree } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild';

await Parser.init();
const language = await loadCurrentWasmBuild();
const fixture = (name: string): string =>
  fs.readFileSync(new URL(`../fixtures/conditionalCpp/${name}`, import.meta.url), 'utf8');

test('preserves canonical C++ declarations after conditional annotation blocks', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '[(namespace_definition) (using_declaration) (export_declaration) (module_declaration) (import_declaration)] @declaration'
  );
  const cases: [string, string[]][] = [
    [
      'declarations.cppm',
      ['export module conditional_cpp;', 'namespace n { int z; }', 'using namespace n;', 'export int exported();'],
    ],
    ['implementation.cpp', ['module reserved_api;']],
    ['consumer.cpp', ['import reserved_api;']],
  ];
  try {
    for (const [file, expected] of cases) {
      const source = fixture(file);
      const tree = parser.parse(source)!;
      try {
        const captures = query.captures(tree.rootNode);
        expect(
          captures.map(({ node }) => node.text),
          file
        ).toEqual(expected);
        for (const { node } of captures) {
          expect(node.startIndex).toBe(source.indexOf(node.text));
          expect(node.endIndex).toBe(node.startIndex + node.text.length);
        }
        expect(tree.rootNode.descendantsOfType('function_definition').map((node) => node.type)).toHaveLength(
          file === 'declarations.cppm' ? 0 : 1
        );
      } finally {
        tree.delete();
      }
    }
  } finally {
    query.delete();
    parser.delete();
  }
});

test('retains return types, templates, and contextual module/import type names', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(function_definition type: (_) @type declarator: (_) @declarator body: (_) @body)'
  );
  const cases: [string, string[][]][] = [
    [
      'returnTypes.cpp',
      [
        ['int', 'primitive()'],
        ['Alias', 'opaque()'],
        ['NS::Type', 'qualified()'],
        ['NS::Box<int>', 'templated()'],
        ['class Object', 'elaborated()'],
        ['typename T::Type', 'dependent()'],
        ['int', 'method()'],
        ['int', 'immediate()'],
      ],
    ],
    ['templateClass.cpp', [['class Object', 'object()']]],
    [
      'contextualTypes.cpp',
      [
        ['::module', 'moduleValue()'],
        ['::import', 'importValue()'],
      ],
    ],
  ];
  try {
    for (const [file, expected] of cases) {
      const source = fixture(file);
      const tree = parser.parse(source)!;
      try {
        expect(
          tree.rootNode
            .descendantsOfType('function_definition')
            .map((node) => ['type', 'declarator'].map((field) => node.childForFieldName(field)?.text)),
          file
        ).toEqual(expected);
        const captures = query.captures(tree.rootNode);
        expect(captures).toHaveLength(expected.length * 3);
        for (const { node } of captures) {
          expect(source.slice(node.startIndex, node.endIndex)).toBe(node.text);
          expect(
            node.parent?.childForFieldName(
              node.type === 'compound_statement' ? 'body' : node.type === 'function_declarator' ? 'declarator' : 'type'
            )?.id
          ).toBe(node.id);
        }
        if (file === 'contextualTypes.cpp') {
          expect(
            tree.rootNode
              .descendantsOfType('field_declaration')
              .map((node) => ['type', 'declarator'].map((field) => node.childForFieldName(field)?.text))
          ).toEqual([
            ['module', 'value'],
            ['import', 'other'],
          ]);
        }
      } finally {
        tree.delete();
      }
    }
  } finally {
    query.delete();
    parser.delete();
  }
});

test('keeps namespace query ranges consistent after an edit and undo across a conditional marker', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, '(namespace_definition name: (_) @name)');
  let source = fixture('declarations.cppm');
  let tree = parser.parse(source)!;
  const captures = (parsed: Tree): [string, number, number][] =>
    query.captures(parsed.rootNode).map(({ node }) => [node.text, node.startIndex, node.endIndex]);
  const original = tree.rootNode.toString();
  const originalCaptures = captures(tree);
  const start = source.indexOf('namespace n') + 'namespace '.length;
  const preceding = source.slice(0, start).split('\n');
  const position = { row: preceding.length - 1, column: preceding.at(-1)!.length };
  try {
    for (const replacement of ['m', 'n']) {
      tree.edit(
        new Edit({
          startIndex: start,
          oldEndIndex: start + 1,
          newEndIndex: start + 1,
          startPosition: position,
          oldEndPosition: { ...position, column: position.column + 1 },
          newEndPosition: { ...position, column: position.column + 1 },
        })
      );
      source = source.slice(0, start) + replacement + source.slice(start + 1);
      const next = parser.parse(source, tree)!;
      tree.delete();
      tree = next;
      const fresh = parser.parse(source)!;
      try {
        expect(tree.rootNode.toString()).toBe(fresh.rootNode.toString());
        expect(captures(tree)).toEqual(captures(fresh));
        expect(captures(tree)).toEqual([[replacement, start, start + 1]]);
      } finally {
        fresh.delete();
      }
    }
    expect(tree.rootNode.toString()).toBe(original);
    expect(captures(tree)).toEqual(originalCaptures);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});
