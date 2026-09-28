import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parsePs, findAncestor, findClaudePid } from '../lib/proc.js';

const PS = `
    1     0 /sbin/launchd
34076 37899 /bin/zsh
34450 34076 claude
68429 34450 /bin/zsh
70000 68429 /usr/local/bin/node
`;

test('parsePs reads pid, ppid and command', () => {
  const m = parsePs(PS);
  assert.deepEqual(m.get(34450), { ppid: 34076, comm: 'claude' });
});

test('finds the nearest claude ancestor starting from the hook shell', () => {
  assert.equal(findAncestor(parsePs(PS), 68429), 34450);
});

test('accepts an npm-installed node Claude and full paths', () => {
  const ps = '10 1 /bin/zsh\n20 10 /opt/homebrew/bin/node\n30 20 /bin/sh\n';
  assert.equal(findAncestor(parsePs(ps), 30), 20);
});

test('returns null when there is no match or on Windows', () => {
  assert.equal(findAncestor(parsePs('10 1 /bin/zsh\n'), 10), null);
  assert.equal(findClaudePid({ platform: 'win32' }), null);
  assert.equal(findClaudePid({ platform: 'darwin', startPid: 68429, runPs: () => PS }), 34450);
  assert.equal(findClaudePid({ platform: 'linux', runPs: () => { throw new Error('no ps'); } }), null);
});
