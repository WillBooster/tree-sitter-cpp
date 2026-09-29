/// <reference types="vite/client" />
import { Language, Parser } from '@willbooster/web-tree-sitter';
import runtimeUrl from '@willbooster/web-tree-sitter/web-tree-sitter.wasm?url';
import { expect, test } from 'vitest';

import cppUrl from '../../../tree-sitter-cpp.wasm?url';

test('parses in a browser, fetching the Wasm files over HTTP', async () => {
  await Parser.init({ locateFile: () => runtimeUrl });
  const parser = new Parser();
  parser.setLanguage(await Language.load(cppUrl));
  expect(parser.parse('class A {};')?.rootNode.toString()).toBe(
    '(translation_unit (class_specifier name: (type_identifier) body: (field_declaration_list)))'
  );
});
