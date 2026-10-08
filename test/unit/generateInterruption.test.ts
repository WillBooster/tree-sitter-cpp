import { execFileSync, spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFile, mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';

import { expect, test } from 'vitest';

const repository = join(import.meta.dirname, '../..');

for (const signal of ['SIGTERM', 'SIGHUP'] as const) {
  test(`generation waits for its child before restoring sources after ${signal}`, async () => {
    await mkdir(join(repository, '.tmp'), { recursive: true });
    const directory = await mkdtemp(join(repository, '.tmp/generation-interruption-'));
    let child: ReturnType<typeof spawn> | undefined;
    let output = '';
    try {
      await mkdir(join(directory, 'script'));
      await mkdir(join(directory, 'src'));
      await copyFile(join(repository, 'script/generate'), join(directory, 'script/generate'));
      await writeFile(join(directory, 'tree-sitter.json'), JSON.stringify({ grammars: [{}] }));
      await writeFile(join(directory, 'src/scanner.c'), 'local scanner edit\n');
      await writeFile(join(directory, 'src/parser.c'), 'previous parser\n');
      // A controlled child makes cancellation happen during a write, without relying on CLI timing.
      await writeFile(
        join(directory, 'script/tree-sitter'),
        `#!/usr/bin/env node
const fs = require('node:fs');
fs.writeFileSync('src/parser.c', 'incomplete parser');
process.stdout.write('ready\\n');
const timer = setInterval(() => {
  if (!fs.existsSync('continue')) return;
  clearInterval(timer);
  fs.writeFileSync('src/parser.c', 'late parser write');
  process.exit(5);
}, 10);
`,
        { mode: 0o755 }
      );
      execFileSync('git', ['init', '-q'], { cwd: directory });
      child = spawn('bash', ['script/generate'], { cwd: directory, detached: true });
      const exited = once(child, 'exit');
      child.stdout?.on('data', (data: Buffer) => {
        output += data.toString();
      });
      child.stderr?.on('data', (data: Buffer) => {
        output += data.toString();
      });
      await expect.poll(() => output).toContain('ready');
      expect(child.kill(signal)).toBe(true);
      await writeFile(join(directory, 'continue'), '');
      const [code, exitSignal] = await exited;
      expect(exitSignal).toBeNull();
      expect({ code, output }).toEqual({
        code: signal === 'SIGHUP' ? 129 : 143,
        output: 'ready\n',
      });
      expect(await readFile(join(directory, 'src/scanner.c'), 'utf8')).toBe('local scanner edit\n');
      expect(await readFile(join(directory, 'src/parser.c'), 'utf8')).toBe('previous parser\n');
      expect(await readdir(join(directory, '.tmp/generation-profiles'))).toEqual([]);
    } finally {
      if (child?.pid) {
        try {
          process.kill(-child.pid, 'SIGKILL');
        } catch (error) {
          expect(error).toMatchObject({ code: 'ESRCH' });
        }
      }
      await rm(directory, { recursive: true, force: true });
    }
  });
}
