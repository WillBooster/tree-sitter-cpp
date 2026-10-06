import fs from 'node:fs';
import path from 'node:path';

import { Edit, Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import treeSitterJson from '../../tree-sitter.json';
import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();
const language = await loadCurrentWasmBuild();
const highlights = [treeSitterJson.grammars[0]!.highlights]
  .flat()
  .map((file) => fs.readFileSync(path.join(import.meta.dirname, '../..', file), 'utf8'))
  .join('\n');

test('preserves macro name adjacency across comments and line splices', () => {
  const parser = new Parser().setLanguage(language);
  try {
    for (const name of [
      'M',
      'int',
      '$name',
      '_name',
      'a0',
      'é',
      '変数',
      'á',
      String.raw`\u00e9`,
      String.raw`\U000000e9`,
    ]) {
      for (const splice of ['', '\\\n', '\\\r\n', '\\\r', '\\\n\r', '\\\n\\\n', '\\\r\n\\\n']) {
        for (const separator of ['', ' ', '/**/', '/*one\ntwo*/', '/**/\t/**/']) {
          const source = `#define ${name}${splice}${separator}(x) x\nint after;\n`;
          const tree = parser.parse(source)!;
          try {
            expect(tree.rootNode.hasError, JSON.stringify(source)).toBe(false);
            const [macro, declaration] = tree.rootNode.namedChildren;
            expect(macro?.childForFieldName('name')?.text).toBe(name);
            expect(declaration?.text).toBe('int after;');
            if (separator) {
              expect(macro?.type).toBe('preproc_def');
              expect(macro?.childForFieldName('parameters')).toBeNull();
              expect(macro?.childForFieldName('value')?.text).toBe('(x) x');
            } else {
              expect(macro?.type).toBe('preproc_function_def');
              expect(macro?.childForFieldName('parameters')?.text).toBe('(x)');
              expect(macro?.childForFieldName('value')?.text).toBe('x');
            }
          } finally {
            tree.delete();
          }
        }
      }
    }
  } finally {
    parser.delete();
  }
});

test('retains function-like names after leading splices and comments', () => {
  const parser = new Parser().setLanguage(language);
  try {
    for (const newline of ['\n', '\r\n', '\r', '\n\r']) {
      const splice = `\\${newline}`;
      for (const prefix of [splice, `/**/${splice}`, `${splice} ${splice}\t`, `${splice}/**/`]) {
        for (const name of ['M', String.raw`\u00e9`, String.raw`\U000000e9`]) {
          const source = `#define ${prefix}${name}(x) x\nint after;\n`;
          const tree = parser.parse(source)!;
          try {
            expect(tree.rootNode.hasError, JSON.stringify(source)).toBe(false);
            const [macro, declaration] = tree.rootNode.namedChildren;
            expect(macro?.type).toBe('preproc_function_def');
            expect(macro?.childForFieldName('name')?.text).toBe(name);
            expect(macro?.childForFieldName('parameters')?.text).toBe('(x)');
            expect(macro?.childForFieldName('value')?.text).toBe('x');
            expect(declaration?.text).toBe('int after;');
          } finally {
            tree.delete();
          }
        }
      }
    }
  } finally {
    parser.delete();
  }
});

test('updates macro classification and query captures after adjacency edits', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, highlights);
  const prefix = '#define M';
  const suffix = '(x) x\nint after;\n';
  let gap = '';
  let tree = parser.parse(prefix + suffix)!;
  const captures = (root: typeof tree.rootNode): unknown[] =>
    query
      .captures(root)
      .map(({ name, node }) => ({ name, text: node.text, start: node.startIndex, end: node.endIndex }));
  try {
    for (const [nextGap, functionNames] of [
      ['/**/', []],
      ['', ['M']],
      ['\\\n', ['M']],
      ['\\\n/**/', []],
      ['\\\r/**/', []],
      ['\\\r', ['M']],
      [' ', []],
      ['', ['M']],
      ['/**/\\\n', []],
      ['', ['M']],
    ] as const) {
      tree.edit(
        new Edit({
          startIndex: prefix.length,
          oldEndIndex: prefix.length + gap.length,
          newEndIndex: prefix.length + nextGap.length,
          startPosition: point(prefix),
          oldEndPosition: point(prefix + gap),
          newEndPosition: point(prefix + nextGap),
        })
      );
      const source = prefix + nextGap + suffix;
      const next = parser.parse(source, tree)!;
      tree.delete();
      tree = next;
      const fresh = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, JSON.stringify(nextGap)).toBe(false);
        expect(tree.rootNode.toString()).toBe(fresh.rootNode.toString());
        expect(captures(tree.rootNode)).toEqual(captures(fresh.rootNode));
        expect(
          query
            .captures(tree.rootNode)
            .filter(({ name }) => name === 'function.special')
            .map(({ node }) => node.text)
        ).toEqual(functionNames);
      } finally {
        fresh.delete();
      }
      gap = nextGap;
    }
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('retains ordinary call expressions while recovering outside macro definitions', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, highlights);
  try {
    for (const source of ['int x = @ foo(1) + 2;', 'void f(void) { if (@ foo(1)) return; }']) {
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.type).toBe('translation_unit');
        expect(tree.rootNode.hasError).toBe(true);
        const calls = tree.rootNode.descendantsOfType('call_expression');
        expect(
          calls.map((node) => node.childForFieldName('function')?.text),
          source
        ).toEqual(['foo']);
        expect(
          query.captures(tree.rootNode).filter(({ name, node }) => name === 'function' && node.text === 'foo')
        ).toHaveLength(1);
      } finally {
        tree.delete();
      }
    }
  } finally {
    query.delete();
    parser.delete();
  }
});

function point(text: string): { row: number; column: number } {
  const lines = text.split('\n');
  return { row: lines.length - 1, column: lines.at(-1)!.length };
}

test('preserves pragma extras before macro names without reserving identifier prefixes', () => {
  const parser = new Parser().setLanguage(language);
  try {
    const pragmas = ['_Pragma("once")', '_Pragma /* gap */ (L"once")'];
    for (const newline of ['\n', '\r\n', '\r', '\n\r']) {
      const splice = `\\${newline}`;
      pragmas.push(
        `_Pragma${splice}("once")`,
        `_Pragma(${splice}"once")`,
        `_Pragma("once"${splice})`,
        `_Pragma("on${splice}ce")`
      );
    }
    for (const pragma of pragmas) {
      const code = parser.parse(`${pragma} int after;`)!;
      try {
        expect(code.rootNode.hasError, JSON.stringify(pragma)).toBe(false);
        expect(code.rootNode.descendantsOfType('pragma_operator').map((node) => node.text)).toEqual([pragma]);
        expect(code.rootNode.descendantsOfType('declaration').map((node) => node.text)).toEqual(['int after;']);
      } finally {
        code.delete();
      }
      for (const skipped of [false, true]) {
        const definition = `#define ${pragma} M(x) x\n`;
        const source = skipped ? `#if 0\n${definition}#endif\n` : definition;
        const tree = parser.parse(source)!;
        try {
          expect(tree.rootNode.hasError, source).toBe(false);
          const macro = tree.rootNode.descendantsOfType('preproc_function_def')[0]!;
          expect(macro.childForFieldName('name')?.text).toBe('M');
          expect(macro.childForFieldName('parameters')?.text).toBe('(x)');
          expect(macro.descendantsOfType('pragma_operator').map((node) => node.text)).toEqual([pragma]);
        } finally {
          tree.delete();
        }
      }
    }
    for (const newline of ['\n', '\r\n', '\r', '\n\r']) {
      const pragma = `_Pragma("a${newline}b")`;
      for (const source of [`${pragma} int after;`, `#define ${pragma} M(x) x\n`]) {
        const tree = parser.parse(source)!;
        try {
          expect(tree.rootNode.descendantsOfType('pragma_operator'), JSON.stringify(source)).toHaveLength(0);
        } finally {
          tree.delete();
        }
      }
    }
    for (const name of ['_Pragma', '_PragmaX', '_Pragm', String.raw`\u005fPragma`]) {
      const tree = parser.parse(`#define ${name}(x) x\n`)!;
      try {
        expect(tree.rootNode.hasError, name).toBe(false);
        expect(tree.rootNode.descendantsOfType('pragma_operator')).toHaveLength(0);
        expect(tree.rootNode.firstNamedChild?.childForFieldName('name')?.text).toBe(name);
        expect(tree.rootNode.firstNamedChild?.childForFieldName('parameters')?.text).toBe('(x)');
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});
