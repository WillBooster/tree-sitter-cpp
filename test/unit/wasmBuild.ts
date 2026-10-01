import fs from 'node:fs';
import path from 'node:path';

import { Language } from '@willbooster/web-tree-sitter';

const Root = path.join(import.meta.dirname, '../..');

// Loads the Wasm build that the package ships. Only `bun run build/ci` rebuilds it, so a test against a stale one would
// pass after a source edit; this fails instead. Call `Parser.init()` first.
export async function loadCurrentWasmBuild(): Promise<Language> {
  const wasmPath = path.join(Root, 'tree-sitter-cpp.wasm');
  // src/parser.c is generated from grammar.js, so an edit to the grammar alone also makes the Wasm build stale.
  const sources = ['grammar.js', 'src/parser.c', 'src/scanner.c'].map(
    (name) => fs.statSync(path.join(Root, name)).mtimeMs
  );
  if (Math.max(...sources) > fs.statSync(wasmPath).mtimeMs) {
    throw new Error('grammar.js or src/ changed after the Wasm build was built; run `bun run build/ci`');
  }
  return Language.load(wasmPath);
}
