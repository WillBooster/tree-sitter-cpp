import fs from 'node:fs';

import { Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();
const language = await loadCurrentWasmBuild();
const source = fs.readFileSync(new URL('../fixtures/conditionalInitializers.cpp', import.meta.url), 'utf8');

test('preserves conditional initializer values and following declarations', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const initializers = tree.rootNode.descendantsOfType('init_declarator');
    expect(
      initializers.map((node) => [node.childForFieldName('declarator')?.text, node.childForFieldName('value')?.type])
    ).toEqual([
      ['global', 'preproc_if'],
      ['coordinates', 'preproc_if'],
      ['designated', 'initializer_list'],
      ['alternatives', 'preproc_if'],
      ['emptyPrimary', 'preproc_if'],
      ['emptyAlternative', 'preproc_if'],
      ['delayed', 'preproc_if'],
      ['(parenthesized)', 'preproc_if'],
      ['nested', 'preproc_if'],
      ['emptyNestedPrimary', 'preproc_if'],
      ['emptyNestedAlternative', 'preproc_if'],
      ['literalConditions', 'preproc_if'],
    ]);
    const global = initializers[0]!.childForFieldName('value')!;
    expect(global.childForFieldName('condition')?.text).toBe('SELECT');
    const alternative = global.childForFieldName('alternative')!;
    expect(alternative.type).toBe('preproc_elif');
    expect(alternative.childForFieldName('condition')?.text).toBe('ALTERNATE');
    expect(alternative.childForFieldName('alternative')?.type).toBe('preproc_else');
    expect(initializers[1]!.descendantsOfType('initializer_list').map((node) => node.text)).toEqual([
      '{1, 2}',
      '{3, 4}',
    ]);
    const member = tree.rootNode
      .descendantsOfType('field_declaration')
      .find((node) => node.childForFieldName('declarator')?.text === 'member')!;
    expect(member.childForFieldName('default_value')?.type).toBe('preproc_ifdef');
    expect(tree.rootNode.descendantsOfType('initializer_pair')[0]!.childForFieldName('value')?.type).toBe(
      'preproc_ifdef'
    );
    const delayed = initializers[6]!.childForFieldName('value')!;
    const laterBranch = delayed.childForFieldName('alternative')!.childForFieldName('alternative')!;
    expect(laterBranch.type).toBe('preproc_elifdef');
    expect(laterBranch.childForFieldName('name')?.text).toBe('OMITTED');
    const nested = initializers[8]!.childForFieldName('value')!;
    expect(nested.namedChildren.filter((node) => node.type === 'preproc_if')).toHaveLength(1);
    const nestedEmpty = initializers[9]!.childForFieldName('value')!;
    expect(nestedEmpty.namedChildren.filter((node) => node.type === 'preproc_if')).toHaveLength(1);
    expect(
      nestedEmpty
        .childForFieldName('alternative')
        ?.descendantsOfType('number_literal')
        .map((node) => node.text)
    ).toEqual(['44']);
    expect(
      initializers[10]!
        .childForFieldName('value')
        ?.descendantsOfType('number_literal')
        .map((node) => node.text)
    ).toEqual(['1', '45']);
    const after = tree.rootNode.lastNamedChild!;
    expect(after.type).toBe('function_definition');
    expect(after.childForFieldName('declarator')?.text).toBe('after()');
    expect(after.startIndex).toBe(source.indexOf('int after()'));
  } finally {
    tree.delete();
    parser.delete();
  }
});

test.each([
  'int missingValue =\n#if 1\n#else\n#endif\n;',
  'int emptyChain =\n#if 1\n#elif 0\n#else\n#endif\n;',
  'int emptyNested =\n#if 1\n#if INNER\n#endif\n#else\n#ifdef OTHER\n#endif\n#endif\n;',
  'int missingEnd =\n#if 1\n1\n#else\n2\n;',
  'int invalidValue =\n#if 1\n1 2\n#else\n3\n#endif\n;',
])('reports incomplete or invalid conditional initializer syntax: %s', (input) => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(input)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(true);
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('resets empty-group source facts between full parses on a reused parser', () => {
  const parser = new Parser().setLanguage(language);
  const invalid = 'int invalid =\n#if A\n1\n#if E\n#else\n#elif F\n#endif\n#endif\n;';
  try {
    for (const input of [invalid, source, invalid, source]) {
      const tree = parser.parse(input)!;
      expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(input === invalid);
      tree.delete();
    }
  } finally {
    parser.delete();
  }
});
