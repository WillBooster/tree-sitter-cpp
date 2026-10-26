import fs from 'node:fs';

import { Edit, Parser, Query, type Tree } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { testCommand } from './run';
import { loadCurrentWasmBuild } from './wasmBuild';

await Parser.init();
const language = await loadCurrentWasmBuild();

test('keeps standalone macro calls and their neighboring C++ declarations', () => {
  const source = fs.readFileSync(new URL('../fixtures/macroLines.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(expression_statement (call_expression function: (_) @macro arguments: (_) @arguments))'
  );
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const captures = query.captures(tree.rootNode);
    expect(captures.filter(({ name }) => name === 'arguments').map(({ node }) => node.text)).toEqual([
      '(detail::Item)',
      '(detail::Item)',
      '(detail::Item)',
      '(Neighbour)',
      '(tag)',
      '(\n  detail::Item,\n  (1 + 2)\n)',
    ]);
    expect(captures.filter(({ name }) => name === 'macro').map(({ node }) => node.text)).toEqual([
      'REGISTER',
      'REGISTER',
      'REGISTER',
      'REGISTER',
      'ANNOTATE',
      'REGISTER',
    ]);
    let cursor = source.indexOf('REGISTER(detail::Item)');
    for (const { node } of captures) {
      const start = source.indexOf(node.text, cursor);
      expect(node.startIndex).toBe(start);
      expect(node.endIndex).toBe(start + node.text.length);
      cursor = node.endIndex;
    }
    expect(tree.rootNode.descendantsOfType('template_declaration').map((node) => node.text)).toEqual([
      'template<class T> void preserved();',
    ]);
    expect(
      tree.rootNode
        .descendantsOfType('function_definition')
        .map((node) => ['type', 'declarator', 'body'].map((field) => node.childForFieldName(field)?.text))
    ).toEqual([
      ['decltype(1)', 'hidden()', '{ return 1; }'],
      ['int', 'ordinary(int value)', '{ return value; }'],
    ]);
    expect(
      tree.rootNode.descendantsOfType('declaration').map((node) => node.childForFieldName('declarator')?.text)
    ).toEqual(['value', 'following', 'preserved()', 'annotated()', 'prefix()', 'multilineType()']);
    expect(
      tree.rootNode.descendantsOfType('class_specifier').map((node) => node.childForFieldName('name')?.text)
    ).toEqual(['Final']);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('does not split ordinary multiline declarations or calls at macro boundaries', () => {
  const sources = [
    'using Item = int;\nItem\nfactory();\nint following;',
    'using Item = int;\nItem\nobject;\nint following;',
    'int\nordinary(int value)\n{ return value; }\nint following;',
    'void function(int);\nfunction\n(1);\nint following;',
    'int (*\nfunctionPointer\n)(int);\nint following;',
  ];
  const parser = new Parser().setLanguage(language);
  try {
    for (const source of sources) {
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        expect(tree.rootNode.descendantsOfType('declaration').at(-1)?.text).toBe('int following;');
        expect(tree.rootNode.descendantsOfType('expression_statement').map((node) => node.text)).toEqual(
          source.includes('function\n(1)') ? ['function\n(1);'] : []
        );
        expect(tree.rootNode.descendantsOfType('function_definition')).toHaveLength(
          source.includes('ordinary') ? 1 : 0
        );
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

test('preserves macro EOF query ranges and fresh/incremental equivalence after inserting a semicolon and undoing it', () => {
  let source = '#define REGISTER(...)\nREGISTER(Item)';
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, '(expression_statement (call_expression) @call)');
  let tree = parser.parse(source)!;
  const captures = (parsed: Tree): [string, number, number][] =>
    query.captures(parsed.rootNode).map(({ node }) => [node.text, node.startIndex, node.endIndex]);
  const original = tree.rootNode.toString();
  const originalCaptures = captures(tree);
  const start = source.length;
  const position = { row: 1, column: 'REGISTER(Item)'.length };
  try {
    expect(tree.rootNode.hasError).toBe(false);
    expect(originalCaptures).toEqual([['REGISTER(Item)', source.indexOf('REGISTER(Item)'), source.length]]);
    for (const replacement of [';', '']) {
      const oldLength = replacement ? 0 : 1;
      tree.edit(
        new Edit({
          startIndex: start,
          oldEndIndex: start + oldLength,
          newEndIndex: start + replacement.length,
          startPosition: position,
          oldEndPosition: { ...position, column: position.column + oldLength },
          newEndPosition: { ...position, column: position.column + replacement.length },
        })
      );
      source = source.slice(0, start) + replacement;
      const next = parser.parse(source, tree)!;
      tree.delete();
      tree = next;
      const fresh = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError).toBe(false);
        expect(tree.rootNode.toString()).toBe(fresh.rootNode.toString());
        expect(captures(tree)).toEqual(captures(fresh));
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

testCommand(
  'keeps native macro call boundaries before Unicode identifiers',
  ['script/tree-sitter', 'parse', '--no-ranges', 'test/fixtures/unicodeMacroLines.cpp'],
  60_000,
  {
    check(output) {
      expect(output.match(/\(expression_statement\s/g)).toHaveLength(12);
      expect(output.match(/\(call_expression\s/g)).toHaveLength(12);
      expect(output.match(/\(declaration\s/g)).toHaveLength(1);
    },
  }
);

test('keeps Unicode neighboring calls and query ranges through identifier edits and undo', () => {
  let source = fs.readFileSync(new URL('../fixtures/unicodeMacroLines.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(expression_statement (call_expression function: (identifier) @function arguments: (argument_list) @arguments))'
  );
  let tree = parser.parse(source)!;
  const captures = (parsed: Tree): [string, string, number, number][] =>
    query.captures(parsed.rootNode).map(({ name, node }) => [name, node.text, node.startIndex, node.endIndex]);
  const original = tree.rootNode.toString();
  const originalCaptures = captures(tree);
  try {
    expect(tree.rootNode.hasError, original).toBe(false);
    expect(originalCaptures.filter(([name]) => name === 'function').map(([, text]) => text)).toEqual([
      'REGISTER',
      '本体',
      'REGISTER',
      'Ļentry',
      'REGISTER',
      'Ĩentry',
      'REGISTER',
      '𐐨entry',
      'REGISTER',
      '数',
      'REGISTER',
      'ordinary',
    ]);
    let cursor = source.indexOf('REGISTER(Item)');
    for (const [, text, start, end] of originalCaptures) {
      const expected = source.indexOf(text, cursor);
      expect([start, end]).toEqual([expected, expected + text.length]);
      cursor = end;
    }
    expect(tree.rootNode.descendantsOfType('declaration').map((node) => node.text)).toEqual(['int following;']);
    const start = source.indexOf('本体();');
    const row = source.slice(0, start).split('\n').length - 1;
    let previous = '本体';
    for (const replacement of ['数体', '𐐨entry', '本体']) {
      tree.edit(
        new Edit({
          startIndex: start,
          oldEndIndex: start + previous.length,
          newEndIndex: start + replacement.length,
          startPosition: { row, column: 0 },
          oldEndPosition: { row, column: previous.length },
          newEndPosition: { row, column: replacement.length },
        })
      );
      source = source.slice(0, start) + replacement + source.slice(start + previous.length);
      previous = replacement;
      const next = parser.parse(source, tree)!;
      tree.delete();
      tree = next;
      const fresh = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        expect(tree.rootNode.toString()).toBe(fresh.rootNode.toString());
        expect(captures(tree)).toEqual(captures(fresh));
        expect(captures(tree).filter(([name]) => name === 'function')[1]?.[1]).toBe(replacement);
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
