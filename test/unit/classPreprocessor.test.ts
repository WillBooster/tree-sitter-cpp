import fs from 'node:fs';

import { Edit, Parser, Query, type Tree } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild';

await Parser.init();
const language = await loadCurrentWasmBuild();
const fixtures = new URL('../fixtures/directiveInitializers/', import.meta.url);

test('retains directive-only elaborated initializers as variable declarations', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(declaration type: (struct_specifier name: (type_identifier) @type) declarator: (init_declarator declarator: (identifier) @variable value: (initializer_list) @initializer)) @declaration'
  );
  try {
    for (const name of ['includeValue', 'includeEmpty', 'emptyIf', 'emptyIfdef', 'onlyDefine', 'emptyBranches']) {
      const input = fs.readFileSync(new URL(`${name}.cpp`, fixtures), 'utf8');
      const tree = parser.parse(input)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        expect(query.matches(tree.rootNode)).toHaveLength(1);
        const captures = query.captures(tree.rootNode);
        const variable = captures.find((capture) => capture.name === 'variable')!.node;
        const initializer = captures.find((capture) => capture.name === 'initializer')!.node;
        expect(captures.find((capture) => capture.name === 'type')!.node.text).toBe('S');
        expect(variable.text).toBe(name);
        expect(variable.startIndex).toBe(input.indexOf(name, input.indexOf('struct S')));
        expect(variable.endIndex).toBe(variable.startIndex + name.length);
        expect(initializer.startIndex).toBe(input.indexOf('{'));
        expect(initializer.endIndex).toBe(input.lastIndexOf('}') + 1);
        expect(tree.rootNode.descendantsOfType('function_definition')).toHaveLength(0);
        expect(tree.rootNode.descendantsOfType('struct_specifier').some((node) => node.childForFieldName('body'))).toBe(
          false
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

test('preserves members nested in conditional macro class bodies after edits', () => {
  const input = fs.readFileSync(new URL('macroClasses.cpp', fixtures), 'utf8');
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, '(field_declaration declarator: (field_identifier) @member)');
  const tags = new Query(language, fs.readFileSync(new URL('../../queries/tags.scm', import.meta.url), 'utf8'));
  const tree = parser.parse(input)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(
      tree.rootNode
        .descendantsOfType(['class_specifier', 'struct_specifier'])
        .filter((node) => node.childForFieldName('body'))
        .map((node) => node.childForFieldName('name')?.text)
    ).toEqual(['Conditional', 'Nested', 'Methods', 'Mixed']);
    const members = query.captures(tree.rootNode);
    expect(members.map(({ node }) => node.text)).toEqual([
      'first',
      'other',
      'nested',
      'alternate',
      'another',
      'last',
      'visible',
    ]);
    for (const { node } of members) {
      expect(node.startIndex).toBe(input.indexOf(`int ${node.text};`) + 'int '.length);
      expect(node.endIndex).toBe(node.startIndex + node.text.length);
    }
    expect(
      tags
        .captures(tree.rootNode)
        .filter(({ name }) => name === 'definition.class')
        .map(({ node }) => node.childForFieldName('name')?.text)
    ).toEqual(['Conditional', 'Nested', 'Methods', 'Mixed']);
    const startIndex = input.indexOf('nested;');
    const row = input.slice(0, startIndex).split('\n').length - 1;
    const column = startIndex - input.lastIndexOf('\n', startIndex) - 1;
    tree.edit(
      new Edit({
        startIndex,
        oldEndIndex: startIndex + 'nested'.length,
        newEndIndex: startIndex + 'renamed'.length,
        startPosition: { row, column },
        oldEndPosition: { row, column: column + 'nested'.length },
        newEndPosition: { row, column: column + 'renamed'.length },
      })
    );
    const changed = input.replace('nested;', 'renamed;');
    const edited = parser.parse(changed, tree)!;
    const fresh = parser.parse(changed)!;
    try {
      expect(edited.rootNode.hasError, edited.rootNode.toString()).toBe(false);
      expect(edited.rootNode.toString()).toBe(fresh.rootNode.toString());
      const captures = (parsed: Tree): [string, string, number, number][] =>
        query.captures(parsed.rootNode).map(({ name, node }) => [name, node.text, node.startIndex, node.endIndex]);
      expect(captures(edited)).toEqual(captures(fresh));
      expect(query.captures(edited.rootNode).map(({ node }) => node.text)).toContain('renamed');
    } finally {
      fresh.delete();
      edited.delete();
    }
  } finally {
    tree.delete();
    tags.delete();
    query.delete();
    parser.delete();
  }
});
