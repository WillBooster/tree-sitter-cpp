import fs from 'node:fs';

import { type Node, Parser, type Tree } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { type AstCapture, captureNodes, childrenOfType, collectCaptures, fieldChild } from '../helpers/ast';
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

test('keeps every marker, directive and declaration source range in nested conditionals', () => {
  const source = fs.readFileSync(new URL('../fixtures/stackedMacroLines.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const captures = stackedMacroCaptures(tree.rootNode);
    expect(captureNodes(captures, 'marker').map((node) => node.text)).toEqual([
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
    expect(captureNodes(captures, 'parameters').map((node) => node.text)).toEqual(['<class T, class U>', '<class T>']);
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
    parser.delete();
  }
});

test('recognizes stacked marker lines around a middle terminator and a final declaration', () => {
  const original = '#define marker\n' + 'marker\n'.repeat(12) + 'template<class T> void kept();\n';
  const middle = '#define marker\n'.length + 'marker\n'.repeat(5).length + 'marker'.length;
  const terminated = original.slice(0, middle) + ';' + original.slice(middle);
  const declared = original.replace('template<class T> void kept();', 'int following;');
  const parser = new Parser().setLanguage(language);
  const trees = [original, terminated, declared].map((source) => parser.parse(source)!);
  try {
    for (const tree of trees) expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const [originalTree, terminatedTree, declaredTree] = trees as [Tree, Tree, Tree];
    expect(countMarkerStatements(originalTree)).toBe(12);
    expect(countMarkerStatements(terminatedTree)).toBe(6);
    expect(terminatedTree.rootNode.descendantsOfType('template_declaration').map((node) => node.text)).toEqual([
      'template<class T> void kept();',
    ]);
    expect(countMarkerStatements(declaredTree)).toBe(0);
    expect(declaredTree.rootNode.descendantsOfType('declaration').at(-1)?.childForFieldName('declarator')?.text).toBe(
      'following'
    );
  } finally {
    for (const tree of trees) tree.delete();
    parser.delete();
  }
});

function countMarkerStatements(tree: Tree): number {
  return tree.rootNode.descendantsOfType('expression_statement').flatMap((node) => childrenOfType(node, 'identifier'))
    .length;
}

function stackedMacroCaptures(root: Node): AstCapture[] {
  return collectCaptures(root, (node, emit) => {
    if (node.type === 'expression_statement') {
      for (const marker of childrenOfType(node, 'identifier')) emit('marker', marker);
    } else if (node.type === 'preproc_if') {
      const defined = fieldChild(node, 'condition', 'preproc_defined');
      for (const condition of childrenOfType(defined, 'identifier')) emit('condition', condition);
    } else if (node.type === 'template_declaration') {
      emit('parameters', fieldChild(node, 'parameters', 'template_parameter_list'));
    } else if (node.type === 'class_specifier') {
      emit('class', fieldChild(node, 'name', 'type_identifier'));
    } else if (node.type === 'function_definition') {
      emit('function', fieldChild(fieldChild(node, 'declarator', 'function_declarator'), 'declarator', 'identifier'));
    }
  });
}
