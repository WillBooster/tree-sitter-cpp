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
    const alternatives = global.childrenForFieldName('alternative');
    expect(alternatives.map((node) => node.type)).toEqual(['preproc_elif', 'preproc_else']);
    const alternative = alternatives[0]!;
    expect(alternative.type).toBe('preproc_elif');
    expect(alternative.childForFieldName('condition')?.text).toBe('ALTERNATE');
    expect(
      initializers[3]!
        .childForFieldName('value')!
        .childrenForFieldName('alternative')
        .map((node) => node.type)
    ).toEqual(['preproc_elif', 'preproc_elifdef', 'preproc_elifdef', 'preproc_else']);
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
    const laterBranch = delayed.childrenForFieldName('alternative')[1]!;
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

test('preserves directives around values and in value-less initializer arms', () => {
  const input = fs.readFileSync(new URL('../fixtures/conditionalInitializerDirectives.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(input)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const values = tree.rootNode.descendantsOfType('init_declarator').map((node) => node.childForFieldName('value')!);
    expect(
      values.map((node) =>
        node
          .descendantsOfType('number_literal')
          .filter((value) => value.parent?.childForFieldName('condition')?.id !== value.id)
          .map((value) => value.text)
      )
    ).toEqual([['1'], ['2'], ['3']]);
    expect(values[0]!.descendantsOfType('preproc_function_def')[0]?.childForFieldName('name')?.text).toBe('VALUE');
    expect(values[1]!.descendantsOfType('preproc_def')[0]?.childForFieldName('name')?.text).toBe('OMITTED');
    expect(values[2]!.descendantsOfType('preproc_include').map((node) => node.childForFieldName('path')?.text)).toEqual(
      ['<missing/*>', '"missing/*"']
    );
    expect(values[2]!.descendantsOfType('pragma_operator')).toHaveLength(2);
    expect(values[2]!.descendantsOfType('preproc_call').some((node) => node.text.startsWith('#error'))).toBe(true);
    expect(tree.rootNode.lastNamedChild?.childForFieldName('declarator')?.text).toBe('after()');
  } finally {
    tree.delete();
    parser.delete();
  }
});

test.each([
  'int missingValue =\n#if 1\n#else\n#endif\n;',
  'int sameLineElse =\n#if 1\n1\n#else 2\n#endif\n;',
  'int sameLineIfdef =\n#ifdef NAME 1\n#else\n2\n#endif\n;',
  'int sameLineElifdef =\n#if 0\n1\n#elifdef NAME 2\n#else\n3\n#endif\n;',
  'int sameLineEnd =\n#if 1\n1\n#else\n2\n#endif ;\n',
  'int directivesOnly =\n#if 1\n#define VALUE 1\n#else\n#pragma clang diagnostic ignored "-Wunused"\n#endif\n;',
  'int emptyChain =\n#if 1\n#elif 0\n#else\n#endif\n;',
  'int emptyNested =\n#if 1\n#if INNER\n#endif\n#else\n#ifdef OTHER\n#endif\n#endif\n;',
  'int missingEnd =\n#if 1\n1\n#else\n2\n;',
  'int invalidValue =\n#if 1\n1 2\n#else\n3\n#endif\n;',
  'int duplicateElse =\n#if 1\n1\n#else\n2\n#else\n#endif\n;',
  'int lateElif =\n#if 1\n1\n#else\n2\n#elif OTHER\n3\n#endif\n;',
  'int invalidBoundary =\n#if 1\n1\n#else\n2\n#else\\bad\n#endif\n;',
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

test.each(['\n', '\r\n'])('preserves conditional header comments with newline %j', (newline) => {
  const parser = new Parser().setLanguage(language);
  const input =
    'int value =\n#ifdef ACTIVE /* header comment */\n#if A // condition comment\n#endif /* closing comment */ \\\n/* continuation comment */\n1\n#else /* alternative comment */ \\\n/* continuation comment */\n2\n#endif /* closing comment */ \\\n/* continuation comment */\n;'.replaceAll(
      '\n',
      newline
    );
  const tree = parser.parse(input)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(tree.rootNode.descendantsOfType('init_declarator')[0]?.childForFieldName('value')?.type).toBe(
      'preproc_ifdef'
    );
  } finally {
    tree.delete();
    parser.delete();
  }
});
