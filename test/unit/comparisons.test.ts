import { Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();
const language = await loadCurrentWasmBuild();

test('keeps greater-equal intact beside possible template names', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(`bool compare(int a, int b, int c) {
  if (a < b >= c) return true;
  a < b >= c;
  return a < b >= c;
}`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    expect(tree.rootNode.descendantsOfType('assignment_expression')).toHaveLength(0);
    expect(tree.rootNode.descendantsOfType('template_function')).toHaveLength(0);
    expect(
      tree.rootNode
        .descendantsOfType('binary_expression')
        .filter((node) => node.childForFieldName('operator')?.text === '>=')
        .map((node) => node.childForFieldName('left')?.text)
    ).toEqual(['a < b', 'a < b', 'a < b']);
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('keeps split template closers before comparison tokens', () => {
  const parser = new Parser().setLanguage(language);
  const prefix = 'template<class> struct Box {}; template<class> constexpr int value = 2; bool f() { return ';
  try {
    for (const expression of ['value<Box<int>>>=1', 'value<Box<int>>>=2', 'value<Box<int> > >=1', 'value<int>>1']) {
      const source = prefix + expression + '; }';
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, source).toBe(false);
        expect(tree.rootNode.descendantsOfType('assignment_expression')).toHaveLength(0);
        const comparison = tree.rootNode.descendantsOfType('return_statement')[0]!.namedChildren[0]!;
        expect(comparison.type).toBe('binary_expression');
        expect(comparison.childForFieldName('left')?.type).toBe('template_function');
        expect(comparison.childForFieldName('left')?.endIndex).toBe(
          prefix.length + expression.slice(0, expression.lastIndexOf('>')).trimEnd().length
        );
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

test('preserves pragma extras beside template closers and expression operators', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(`template<class> struct Box {};
Box<int _Pragma(/*spacing*/ "GCC diagnostic push")> box;
bool compare(int a, int b, int c) {
  return a _Pragma(/*spacing*/ "GCC diagnostic push") + b _Pragma(/*spacing*/ "GCC diagnostic push") > c;
}
int shift(int a, int b) { return a >> b; }`)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(tree.rootNode.descendantsOfType('pragma_operator')).toHaveLength(3);
    const returns = tree.rootNode.descendantsOfType('return_statement');
    expect(returns.map((node) => node.namedChildren[0]?.childForFieldName('operator')?.text)).toEqual(['>', '>>']);
    expect(
      tree.rootNode.descendantsOfType('template_type').map((node) => node.childForFieldName('name')?.text)
    ).toEqual(['Box']);
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('classifies spliced closing tokens without joining spaces or comments', () => {
  const parser = new Parser().setLanguage(language);
  const prefix = 'template<class> struct Box {}; template<class> constexpr int value = 2; bool f() { return ';
  try {
    for (const newline of ['\n', '\r\n']) {
      const splice = '\\' + newline;
      for (const gap of ['', splice]) {
        const template = 'value<Box<int>' + splice + '>';
        const source = prefix + template + gap + '>=1; }';
        const tree = parser.parse(source)!;
        try {
          expect(tree.rootNode.hasError, source).toBe(false);
          expect(tree.rootNode.descendantsOfType('assignment_expression')).toHaveLength(0);
          const comparison = tree.rootNode.descendantsOfType('return_statement')[0]!.namedChildren[0]!;
          expect(comparison.childForFieldName('operator')?.text).toBe('>=');
          expect(comparison.childForFieldName('left')?.text).toBe(template);
          expect(comparison.childForFieldName('left')?.endIndex).toBe(prefix.length + template.length);
        } finally {
          tree.delete();
        }
      }
    }
    for (const gap of [' ', '/* keep */']) {
      const source = 'template<class> int slot; void f() { slot<int>' + gap + '=1; }';
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, source).toBe(false);
        const assignment = tree.rootNode.descendantsOfType('assignment_expression')[0]!;
        expect(assignment.childForFieldName('left')?.text).toBe('slot<int>');
        expect(assignment.childForFieldName('operator')?.text).toBe('=');
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

test('keeps escaped identifiers separate from template closing tokens', () => {
  const parser = new Parser().setLanguage(language);
  const source = String.raw`template<class> struct Box {};
Box<int>\u03b1;
Box<Box<int>>\u03b2;
Box<int>\
\u03b3;`;
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const declarations = tree.rootNode.descendantsOfType('declaration');
    expect(declarations.map((node) => node.childForFieldName('declarator')?.text)).toEqual([
      String.raw`\u03b1`,
      String.raw`\u03b2`,
      String.raw`\u03b3`,
    ]);
    expect(declarations.map((node) => node.childForFieldName('type')?.text)).toEqual([
      'Box<int>',
      'Box<Box<int>>',
      'Box<int>',
    ]);
  } finally {
    tree.delete();
    parser.delete();
  }
});
