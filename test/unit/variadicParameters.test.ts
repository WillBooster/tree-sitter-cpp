import { type Node, Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { childrenOfType, fieldChild, fieldChildren } from '../helpers/ast.js';
import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();
const language = await loadCurrentWasmBuild();

test('exposes terminal variadic parameters while preserving template packs', () => {
  const source = `void first(...);
void second(int value, ...);
void third(int value...);
void unnamed(int...);
void sized(unsigned long...);
void pointers(int*...);
void wide(wchar_t...);
void after(int value, unsigned long...);
void keywordPrefixes(int intensity, double doubleValue);
template<class... Ts> void packed(Ts... values);
template<class... size_t> void libraryNamedPack(size_t...);
`;
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const ellipses = tree.rootNode
      .descendantsOfType('parameter_list')
      .flatMap((node) => childrenOfType(node, 'variadic_parameter'));
    expect(ellipses.map((node) => node.parent?.parent?.childForFieldName('declarator')?.text)).toEqual([
      'first',
      'second',
      'third',
      'unnamed',
      'sized',
      'pointers',
      'wide',
      'after',
    ]);
    for (const node of ellipses) {
      expect(node.text).toBe('...');
      expect(node.parent?.namedChildren.at(-1)?.startIndex).toBe(node.startIndex);
    }
    const prefixControl = tree.rootNode
      .descendantsOfType('function_declarator')
      .find((node) => node.childForFieldName('declarator')?.text === 'keywordPrefixes')!;
    expect(
      prefixControl
        .childForFieldName('parameters')
        ?.namedChildren.map((node) => node.childForFieldName('declarator')?.text)
    ).toEqual(['intensity', 'doubleValue']);
    expect(tree.rootNode.descendantsOfType('variadic_parameter_declaration').map((node) => node.text)).toEqual([
      'Ts... values',
      'size_t...',
    ]);
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('rejects parameters after a variadic ellipsis and a leading comma', () => {
  const parser = new Parser().setLanguage(language);
  try {
    for (const source of [
      'void f(..., int value);',
      'void f(int, ..., ...);',
      'void f(, ...);',
      'void f(\n#ifdef V\n..., int value\n#else\nint value\n#endif\n);',
      'void f(\n#ifdef V\n...\n#else\nint value\n#endif\n, int after);',
    ]) {
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, source).toBe(true);
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

test('keeps function varargs separate from exception declarations', () => {
  const parser = new Parser().setLanguage(language);
  const source = `void handled() {
  try {}
  catch (int) {}
  catch ([[maybe_unused]] double value) {}
  catch (int (*callback)(int, ...)) {}
  catch (...) {}
}`;
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const catches = tree.rootNode.descendantsOfType('catch_clause');
    expect(catches).toHaveLength(4);
    const catchAll = catches.flatMap((clause) =>
      childrenOfType(fieldChild(clause, 'parameters', 'parameter_list'), 'variadic_parameter')
    );
    expect(catchAll.map((node) => node.text)).toEqual(['...']);
    for (const parameters of [
      'int value...',
      'int...',
      'int value, ...',
      '..., int value',
      'int first, int second',
      '',
    ]) {
      const invalid = parser.parse(`void f() { try {} catch (${parameters}) {} }`)!;
      try {
        expect(invalid.rootNode.hasError, parameters).toBe(true);
      } finally {
        invalid.delete();
      }
    }
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('recovers conditional parameter lists without consuming following definitions', () => {
  const parser = new Parser().setLanguage(language);
  const following = 'int after1(int value) { return value; }\nint after2(int value) { return value; }\n';
  try {
    for (const conditional of [
      '#ifdef A\n int second\n#else\n long second\n#endif\n',
      '#ifdef VARARGS\n ...\n#else\n int second\n#endif\n',
    ]) {
      const prefix = 'void conditional(int first,\n';
      const suffix = ');\n' + following;
      for (const parameters of [conditional, 'int second\n']) {
        const source = prefix + parameters + suffix;
        const tree = parser.parse(source)!;
        try {
          expect(
            tree.rootNode
              .descendantsOfType('function_definition')
              .map((node) => [node.text, node.startIndex, node.endIndex])
          ).toEqual([
            [
              'int after1(int value) { return value; }',
              source.indexOf('int after1'),
              source.indexOf('int after1') + 'int after1(int value) { return value; }'.length,
            ],
            [
              'int after2(int value) { return value; }',
              source.indexOf('int after2'),
              source.indexOf('int after2') + 'int after2(int value) { return value; }'.length,
            ],
          ]);
        } finally {
          tree.delete();
        }
      }
    }
  } finally {
    parser.delete();
  }
});

test('keeps conditional parameter prefixes and tails within the complete function declaration', () => {
  const parser = new Parser().setLanguage(language);
  const conditionalTail = '#ifdef VARARGS\n  const char *fmt, ...\n#else\n  const char *fmt\n#endif\n';
  const definitions = ['int after1(int value) { return value; }', 'int after2(int value) { return value; }'];
  const following = definitions.join('\n') + '\n';
  try {
    for (const { first, conditional, tail, plain } of [
      { first: '', conditional: conditionalTail, tail: '', plain: ['const char *fmt, ...\n', 'const char *fmt\n'] },
      {
        first: 'int first,',
        conditional: conditionalTail,
        tail: '',
        plain: ['const char *fmt, ...\n', 'const char *fmt\n'],
      },
      { first: '', conditional: '#ifdef A\n int x,\n#endif\n', tail: 'int y\n', plain: ['int x,\n', ''] },
      {
        first: '',
        conditional: '#if defined(A)\n int x\n#else\n long x\n#endif\n,\n',
        tail: 'int y\n',
        plain: ['int x,\n', ''],
      },
      {
        first: '',
        conditional: '#ifdef A\n int x, int z\n#else\n long x\n#endif\n,\n',
        tail: 'int y\n',
        plain: ['int x, int z,\n', ''],
      },
      {
        first: '',
        conditional: '#ifdef A\n#ifdef B\n int x,\n#endif\n#endif\n',
        tail: 'int y\n',
        plain: ['int x,\n', ''],
      },
    ]) {
      const prefix = 'int log_message(' + first + '\n';
      const suffix = tail + ');\n' + following;
      for (const parameters of [conditional, ...plain]) {
        const source = prefix + parameters + suffix;
        const tree = parser.parse(source)!;
        try {
          expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
          const declarations = functionDeclarations(tree.rootNode);
          expect(declarations.map(({ name }) => [name.text, name.startIndex, name.endIndex])).toEqual([
            ['log_message', 4, 15],
          ]);
          const declarationText = prefix + parameters + tail + ');';
          expect(
            declarations.map(({ declaration }) => [declaration.text, declaration.startIndex, declaration.endIndex])
          ).toEqual([[declarationText, 0, declarationText.length]]);
          expect(
            tree.rootNode
              .descendantsOfType('function_definition')
              .map((node) => [node.text, node.startIndex, node.endIndex])
          ).toEqual(
            definitions.map((definition) => [
              definition,
              source.indexOf(definition),
              source.indexOf(definition) + definition.length,
            ])
          );
        } finally {
          tree.delete();
        }
      }
    }
  } finally {
    parser.delete();
  }
});

function functionDeclarations(root: Node): { declaration: Node; name: Node }[] {
  return root.descendantsOfType('declaration').flatMap((declaration) =>
    fieldChildren(declaration, 'declarator', 'function_declarator').flatMap((declarator) => {
      const name = fieldChild(declarator, 'declarator', 'identifier');
      return name ? [{ declaration, name }] : [];
    })
  );
}
