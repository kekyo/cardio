import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';

const [objdump, directory] = process.argv.slice(2);
const forbidden = /\b(?:CancelIoEx|CancelSynchronousIo|GetQueuedCompletionStatusEx|InitializeCriticalSectionEx|InitializeConditionVariable|SleepConditionVariable\w*|Wake(?:All)?ConditionVariable|(?:Initialize|Acquire|Release|TryAcquire)SRWLock\w*|GetTickCount64|GetThreadId|Fls\w*)\b/;

const imports = (path) => {
  const result = spawnSync(objdump, ['-p', path], {
    encoding: 'utf8',
    maxBuffer: 32 * 1024 * 1024,
  });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr);
  const table = result.stdout.split('The Import Tables')[1];
  assert.ok(table, `Missing PE import table: ${path}`);
  return table.split(/The (?:Function Table|Export Tables)/)[0];
};

for (const arch of ['amd64', 'i686']) {
  for (const test of [
    'core.exe', 'cancellation.exe', 'iocp.exe', 'native.exe', 'minimal.exe',
    'shared-lease.exe', 'lease-plugin.dll', 'libcardio.dll',
  ]) {
    const path = join(directory, `${arch}-${test}`);
    const table = imports(path);
    assert.doesNotMatch(table, forbidden, path);
    assert.doesNotMatch(table, /DLL Name: (?:api-ms-|libwinpthread)/i, path);
  }
  const table = imports(join(directory, `${arch}-modern-native.exe`));
  assert.match(table, /\bCancelIoEx\b/);
  assert.match(table, /\bGetQueuedCompletionStatusEx\b/);
}
console.log('XP optional API imports / Vista direct imports: PASS');
