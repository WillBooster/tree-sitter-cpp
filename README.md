# @willbooster/tree-sitter-cpp

[![npm version](https://img.shields.io/npm/v/@willbooster/tree-sitter-cpp.svg)](https://www.npmjs.com/package/@willbooster/tree-sitter-cpp)
[![license](https://img.shields.io/npm/l/@willbooster/tree-sitter-cpp.svg)](https://www.npmjs.com/package/@willbooster/tree-sitter-cpp)
[![Test](https://github.com/WillBooster/tree-sitter-cpp/actions/workflows/test.yml/badge.svg)](https://github.com/WillBooster/tree-sitter-cpp/actions/workflows/test.yml)
[![Test rust](https://github.com/WillBooster/tree-sitter-cpp/actions/workflows/test-rust.yml/badge.svg)](https://github.com/WillBooster/tree-sitter-cpp/actions/workflows/test-rust.yml)
[![semantic-release](https://img.shields.io/badge/%20%20%F0%9F%93%A6%F0%9F%9A%80-semantic--release-e10079.svg)](https://github.com/semantic-release/semantic-release)
[![wbfy](https://img.shields.io/badge/wbfy-20.28.8-1e90ff.svg)](https://github.com/WillBooster/shared/tree/main/packages/wbfy)
[![crates.io](https://img.shields.io/crates/v/willbooster-tree-sitter-cpp.svg)](https://crates.io/crates/willbooster-tree-sitter-cpp)

C++ grammar for [tree-sitter](https://github.com/tree-sitter/tree-sitter), forked from
[tree-sitter/tree-sitter-cpp](https://github.com/tree-sitter/tree-sitter-cpp). We are grateful
to its authors and contributors. This is not an official release of that project.

This fork fixes parsing bugs and raises conformance with the ISO C++ standard
([working draft](https://eel.is/c++draft/)).

## Usage

The npm package ships `tree-sitter-cpp.wasm` for
[@willbooster/web-tree-sitter](https://www.npmjs.com/package/@willbooster/web-tree-sitter), which runs in Node.js, Bun,
browsers, and Cloudflare Workers. Install both:

```sh
npm install @willbooster/tree-sitter-cpp @willbooster/web-tree-sitter
```

In Node.js and Bun, load the `.wasm` file from its path:

```js
import { fileURLToPath } from 'node:url';
import { Language, Parser } from '@willbooster/web-tree-sitter';

await Parser.init();
const parser = new Parser();
const wasmPath = fileURLToPath(import.meta.resolve('@willbooster/tree-sitter-cpp/tree-sitter-cpp.wasm'));
parser.setLanguage(await Language.load(wasmPath));
const tree = parser.parse('int main() {}\n');
```

In browsers, serve both `.wasm` files and load them by URL. With Vite:

```js
import { Language, Parser } from '@willbooster/web-tree-sitter';
import runtimeUrl from '@willbooster/web-tree-sitter/web-tree-sitter.wasm?url';
import cppUrl from '@willbooster/tree-sitter-cpp/tree-sitter-cpp.wasm?url';

await Parser.init({ locateFile: () => runtimeUrl });
const parser = new Parser();
parser.setLanguage(await Language.load(cppUrl));
```

In Cloudflare Workers, which do not allow compiling Wasm at run time, import both `.wasm` files as modules:

```js
import { Language, Parser } from '@willbooster/web-tree-sitter';
import runtime from '@willbooster/web-tree-sitter/web-tree-sitter.wasm';
import cpp from '@willbooster/tree-sitter-cpp/tree-sitter-cpp.wasm';

await Parser.init({ wasmModule: runtime });
const parser = new Parser();
parser.setLanguage(await Language.load(cpp));
```

The package also ships the node types in `src/node-types.json`, and the queries that `tree-sitter.json` lists: this
grammar's in `queries/` and, in `queries/c/`, the highlights of the C grammar it extends.

In Rust, depend on the [crate](https://crates.io/crates/willbooster-tree-sitter-cpp) and on
[willbooster-tree-sitter](https://crates.io/crates/willbooster-tree-sitter), the runtime this package is tested and
fuzzed with (the grammar also loads in the upstream `tree-sitter` crate 0.27, whose error recovery never ends on some
malformed input):

```toml
[dependencies]
tree-sitter = { package = "willbooster-tree-sitter", version = "1" }
tree-sitter-cpp = { package = "willbooster-tree-sitter-cpp", version = "1" }
```

```rust
let mut parser = tree_sitter::Parser::new();
parser.set_language(&tree_sitter_cpp::LANGUAGE.into())?;
```

The crate ships the same `tree-sitter.json` and query files as the npm package.

## Development

```sh
mise install
bun install --frozen-lockfile
bun run build/ci
bun run test
script/parse-examples
cargo test --locked
```

The scripts and tests generate, build, test, and parse with `script/tree-sitter`, the tree-sitter CLI of the
WillBooster/tree-sitter runtime version locked in `Cargo.lock`, since the generator and the runtime of upstream's CLI are
not the ones this package ships with. Its first run downloads that CLI from the runtime's GitHub Release, or builds it
with `cargo` (whose build runs the CMake that `mise.toml` pins) when the download fails or the release has no binary
that runs here. Run other CLI commands through it as well (e.g. `script/tree-sitter parse file.cpp`).

`bun run test` runs:

- the corpus in `test/corpus`, with the native build and with the Wasm build (the first run downloads the WASI SDK);
- an incremental-parsing check (`test/unit/incremental.test.ts`): `script/fuzz-corpus` runs `tree-sitter fuzz`, which
  edits each corpus case at random, reparses it, undoes the edits, and reparses again. `TREE_SITTER_SEED`, `TREE_SITTER_ITERATIONS`, and
  `TREE_SITTER_EDITS` run other or more edits;
- a check that the real-world C++ files in `examples/`, the checked-in ones and those of the cloned repositories,
  fail to parse exactly as listed in `script/known-failures.txt`. The first run clones the repositories. The example
  repositories are pinned to commits in `script/parse-examples`. After a grammar change or a moved pin alters that
  list, `script/parse-examples` rewrites it; review its diff before committing;
- a performance check (`test/unit/performance.test.ts`) that recovering from an error on each line takes linear
  time, since consumers parse files while they are being edited. It loads the Wasm build through
  @willbooster/web-tree-sitter, which `bun run build/ci` rebuilds after regenerating the parser;
- a check (`test/unit/expressionKeywordParameter.test.ts`) that every keyword that can start an expression but not a
  parameter is expected in the parameters of the function declarators that compete with a direct initialization, so
  that `long(n)(sizeof(b));` stays an initialization, and that the keyword-led arguments that make `Foo* p(nullptr);` a
  variable start only with such keywords;
- a check (`test/unit/queries.test.ts`) that the queries `tree-sitter.json` lists compile against the grammar, are
  published in the npm package and the crate, and that `queries/c/` matches the queries of the installed
  @willbooster/tree-sitter-c; after updating that dependency, `script/copy-c-queries` refreshes the copy;
- a check (`test/unit/runtimeVersion.test.ts`) that `@willbooster/web-tree-sitter` in `package.json` and
  `willbooster-tree-sitter` in `Cargo.lock` are the same version, since the Wasm tests run on the former and the Rust
  tests, the fuzzer, and the CLI on the latter;
- checks that the Wasm build parses in Chromium (`test/unit/browser/`) and in Cloudflare Workers with and without
  Node.js compatibility (`test/unit/workers.test.ts`). Run `bun run test/ci-setup` once to install Chromium.

CI also runs these tests on Linux arm64 and macOS, where the parser and scanner are compiled natively against each
platform's C library, and fuzzes the parser with libFuzzer and sanitizers (`.github/workflows/robustness.yml`).

### References

- [Hyperlinked C++ BNF Grammar](http://www.nongnu.org/hcb/)
- [EBNF Syntax: C++](http://www.externsoft.ch/download/cpp-iso.html)
