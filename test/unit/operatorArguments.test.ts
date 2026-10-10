import { Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();
const language = await loadCurrentWasmBuild();

const Definitions = 'int f(int a) {\n  return a;\n}\n\nint g() { return 3; }\n';

test('keeps the definitions after a macro call that takes an operator and has no semicolon', () => {
  for (const operator of ['< ', '> ', '==', '!=', '<=', '>=', '+', '*', '&', '&&', '->', '.']) {
    const call = `HELPER_(NAME, ${operator})`;
    for (const source of [`${call}\n\n${Definitions}`, `namespace n {\n${call}\n\n${Definitions}}\n`]) {
      expect(parse(source), JSON.stringify(source)).toEqual({ functions: ['f', 'g'], errors: [] });
    }
  }
});

test('keeps the definitions after consecutive macro calls that take operators', () => {
  const calls = ['NE, !=', 'LE, <=', 'LT, < ', 'GE, >=', 'GT, > '].map((args) => `HELPER_(${args})\n`).join('');
  expect(parse(`${calls}\n${Definitions}`).functions).toEqual(['f', 'g']);
});

function parse(source: string): { functions: (string | undefined)[]; errors: string[] } {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  try {
    return {
      functions: tree.rootNode
        .descendantsOfType('function_definition')
        .map((definition) => definition?.childForFieldName('declarator')?.childForFieldName('declarator')?.text),
      errors: tree.rootNode.descendantsOfType('ERROR').map((error) => error?.text ?? ''),
    };
  } finally {
    tree.delete();
    parser.delete();
  }
}
