import fs from 'node:fs';
import path from 'node:path';

import { generationInputMtime } from '../helpers/generationInputs.js';

import { Language } from '@willbooster/web-tree-sitter';

const Root = path.join(import.meta.dirname, '../..');

// Call `Parser.init()` before loading the package’s Wasm build. Source freshness is checked because tests load the existing build.
export async function loadCurrentWasmBuild(): Promise<Language> {
  const wasmPath = path.join(Root, 'tree-sitter-cpp.wasm');
  const sources = ['grammar.js', 'src/parser.c', 'src/scanner.c', 'src/pragma.h', 'src/identifier.h'].map(
    (name) => fs.statSync(path.join(Root, name)).mtimeMs
  );
  if (Math.max(generationInputMtime(Root), ...sources) > fs.statSync(wasmPath).mtimeMs) {
    throw new Error('generation inputs or src/ changed after the Wasm build was built; run `bun run build/ci`');
  }
  return Language.load(wasmPath);
}
