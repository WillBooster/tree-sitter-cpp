import fs from 'node:fs';

import { Edit, Parser, Query, type Tree } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { testCommand } from './run';
import { loadCurrentWasmBuild } from './wasmBuild';

await Parser.init();
const language = await loadCurrentWasmBuild();

testCommand(
  'keeps native conditional/template structure around stacked macro lines',
  ['script/tree-sitter', 'parse', '--no-ranges', 'test/fixtures/stackedMacroLines.cpp'],
  60_000,
  {
    check(output) {
      expect(output.match(/\(preproc_if\s/g)).toHaveLength(2);
      expect(output.match(/\(template_declaration\s/g)).toHaveLength(2);
      expect(output.match(/\(class_specifier\s/g)).toHaveLength(1);
      expect(output.match(/\(function_definition\s/g)).toHaveLength(1);
      expect(output.match(/\(expression_statement\s/g)).toHaveLength(9);
    },
  }
);

test('keeps every marker, directive and declaration query range in nested conditionals', () => {
  const source = fs.readFileSync(new URL('../fixtures/stackedMacroLines.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    `(expression_statement (identifier) @marker)
(preproc_if condition: (preproc_defined (identifier) @condition))
(template_declaration parameters: (template_parameter_list) @parameters)
(class_specifier name: (type_identifier) @class)
(function_definition declarator: (function_declarator declarator: (identifier) @function))`
  );
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const captures = query.captures(tree.rootNode);
    expect(captures.filter(({ name }) => name === 'marker').map(({ node }) => node.text)).toEqual([
      'PUSH_STATE',
      'OPEN_SCOPE',
      'OPEN_ABI',
      'OPEN_SCOPE',
      'OPEN_ABI',
      'CLOSE_ABI',
      'CLOSE_SCOPE',
      'CLOSE_ABI',
      'CLOSE_SCOPE',
    ]);
    expect(captures.filter(({ name }) => name === 'parameters').map(({ node }) => node.text)).toEqual([
      '<class T, class U>',
      '<class T>',
    ]);
    expect(captures.filter(({ name }) => name === 'class' || name === 'function').map(({ node }) => node.text)).toEqual(
      ['Channel', 'preserved']
    );
    let cursor = source.indexOf('#if defined(ACTIVE)');
    for (const { node } of captures) {
      const start = source.indexOf(node.text, cursor);
      expect(node.startIndex).toBe(start);
      expect(node.endIndex).toBe(start + node.text.length);
      cursor = node.endIndex;
    }
    const directives = tree.rootNode.descendantsOfType('preproc_if');
    expect(directives).toHaveLength(2);
    expect(directives[0]!.descendantsOfType('class_specifier')).toHaveLength(1);
    expect(directives[1]!.descendantsOfType('function_definition')).toHaveLength(1);
    expect(tree.rootNode.descendantsOfType('declaration').at(-1)?.text).toBe('int following;');
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('invalidates a validated marker chain after middle and final-line edits and undo', () => {
  const originalSource = '#define marker\n' + 'marker\n'.repeat(12) + 'template<class T> void kept();\n';
  let source = originalSource;
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(expression_statement (identifier) @marker) (declaration type: (_) @type declarator: (_) @declarator)'
  );
  let tree = parser.parse(source)!;
  const captures = (parsed: Tree): [string, string, number, number][] =>
    query.captures(parsed.rootNode).map(({ name, node }) => [name, node.text, node.startIndex, node.endIndex]);
  const original = tree.rootNode.toString();
  const originalCaptures = captures(tree);
  const replace = (start: number, oldText: string, newText: string): void => {
    const preceding = source.slice(0, start).split('\n');
    const position = { row: preceding.length - 1, column: preceding.at(-1)!.length };
    tree.edit(
      new Edit({
        startIndex: start,
        oldEndIndex: start + oldText.length,
        newEndIndex: start + newText.length,
        startPosition: position,
        oldEndPosition: { ...position, column: position.column + oldText.length },
        newEndPosition: { ...position, column: position.column + newText.length },
      })
    );
    source = source.slice(0, start) + newText + source.slice(start + oldText.length);
    const next = parser.parse(source, tree)!;
    tree.delete();
    tree = next;
    const fresh = parser.parse(source)!;
    try {
      expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
      expect(tree.rootNode.toString()).toBe(fresh.rootNode.toString());
      expect(captures(tree)).toEqual(captures(fresh));
    } finally {
      fresh.delete();
    }
  };
  try {
    expect(tree.rootNode.hasError).toBe(false);
    expect(originalCaptures.filter(([name]) => name === 'marker')).toHaveLength(12);
    const middle = '#define marker\n'.length + 'marker\n'.repeat(5).length + 'marker'.length;
    replace(middle, '', ';');
    expect(captures(tree).filter(([name]) => name === 'marker')).toHaveLength(6);
    expect(tree.rootNode.descendantsOfType('template_declaration').map((node) => node.text)).toEqual([
      'template<class T> void kept();',
    ]);
    replace(middle, ';', '');
    expect(tree.rootNode.toString()).toBe(original);
    expect(captures(tree)).toEqual(originalCaptures);
    const final = source.indexOf('template');
    replace(final, 'template<class T> void kept();', 'int following;');
    expect(captures(tree).filter(([name]) => name === 'marker')).toEqual([]);
    expect(tree.rootNode.descendantsOfType('declaration').at(-1)?.childForFieldName('declarator')?.text).toBe(
      'following'
    );
    replace(final, 'int following;', 'template<class T> void kept();');
    expect(source).toBe(originalSource);
    expect(tree.rootNode.toString()).toBe(original);
    expect(captures(tree)).toEqual(originalCaptures);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});
