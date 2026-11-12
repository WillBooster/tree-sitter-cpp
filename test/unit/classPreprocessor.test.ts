import fs from 'node:fs';

import { type Node, Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { fieldChild, fieldChildren } from '../helpers/ast';
import { loadCurrentWasmBuild } from './wasmBuild';

await Parser.init();
const language = await loadCurrentWasmBuild();
const fixtures = new URL('../fixtures/directiveInitializers/', import.meta.url);

test('retains directive-only elaborated initializers as variable declarations', () => {
  const parser = new Parser().setLanguage(language);
  try {
    for (const name of ['includeValue', 'includeEmpty', 'emptyIf', 'emptyIfdef', 'onlyDefine', 'emptyBranches']) {
      const input = fs.readFileSync(new URL(`${name}.cpp`, fixtures), 'utf8');
      const tree = parser.parse(input)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        const matches = elaboratedInitializerDeclarations(tree.rootNode);
        expect(matches).toHaveLength(1);
        const { type, variable, initializer } = matches[0]!;
        expect(type.text).toBe('S');
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
    parser.delete();
  }
});

test('preserves members nested in conditional macro class bodies and after renaming one', () => {
  const input = fs.readFileSync(new URL('macroClasses.cpp', fixtures), 'utf8');
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(input)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(
      tree.rootNode
        .descendantsOfType(['class_specifier', 'struct_specifier'])
        .filter((node) => node.childForFieldName('body'))
        .map((node) => node.childForFieldName('name')?.text)
    ).toEqual(['Conditional', 'Nested', 'Methods', 'Mixed']);
    const members = fieldMembers(tree.rootNode);
    expect(members.map((node) => node.text)).toEqual([
      'first',
      'other',
      'nested',
      'alternate',
      'another',
      'last',
      'visible',
    ]);
    for (const node of members) {
      expect(node.startIndex).toBe(input.indexOf(`int ${node.text};`) + 'int '.length);
      expect(node.endIndex).toBe(node.startIndex + node.text.length);
    }
    const changed = input.replace('nested;', 'renamed;');
    const renamed = parser.parse(changed)!;
    try {
      expect(renamed.rootNode.hasError, renamed.rootNode.toString()).toBe(false);
      expect(fieldMembers(renamed.rootNode).map((node) => node.text)).toContain('renamed');
    } finally {
      renamed.delete();
    }
  } finally {
    tree.delete();
    parser.delete();
  }
});

function elaboratedInitializerDeclarations(root: Node): { type: Node; variable: Node; initializer: Node }[] {
  const results: { type: Node; variable: Node; initializer: Node }[] = [];
  for (const declaration of root.descendantsOfType('declaration')) {
    const type = fieldChild(fieldChild(declaration, 'type', 'struct_specifier'), 'name', 'type_identifier');
    if (!type) continue;
    for (const init of fieldChildren(declaration, 'declarator', 'init_declarator')) {
      const variable = fieldChild(init, 'declarator', 'identifier');
      const initializer = fieldChild(init, 'value', 'initializer_list');
      if (variable && initializer) results.push({ type, variable, initializer });
    }
  }
  return results;
}

function fieldMembers(root: Node): Node[] {
  return root
    .descendantsOfType('field_declaration')
    .flatMap((node) => fieldChildren(node, 'declarator', 'field_identifier'));
}
