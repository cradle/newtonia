// Compile the production analytics methods with small state/player fixtures.
// No SDL/GL runtime required. Run from any cwd: node test/unit/control_analytics.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.resolve(__dirname, '../..');
function method(file, signature) {
  const source = fs.readFileSync(path.join(root, file), 'utf8');
  const start = source.indexOf(signature);
  assert.ok(start >= 0, `${file}: missing ${signature}`);
  const open = source.indexOf('{', start);
  let depth = 1, end = open + 1;
  for (; end < source.length && depth; end++) {
    if (source[end] === '{') depth++;
    if (source[end] === '}') depth--;
  }
  assert.equal(depth, 0, `${file}: unterminated ${signature}`);
  return source.slice(start, end);
}
const methods = [
  method('glship.cpp', 'int GLShip::control_analytics('),
  method('glgame.cpp', 'int GLGame::control_analytics('),
  method('state_manager.cpp', 'int StateManager::control_analytics('),
].join('\n');
const fingerMethods = [
  method('web_main.cpp', 'static unsigned char touch_to_key('),
  method('web_main.cpp', 'static void record_touch_key('),
  method('web_main.cpp', 'static void finger_down('),
  method('web_main.cpp', 'static void finger_up('),
].join('\n');
const motion = method('web_main.cpp', 'case SDL_FINGERMOTION:').replace('case SDL_FINGERMOTION:', '');
const fixture = fs.readFileSync(path.join(__dirname, 'control_analytics_fixture.cpp'), 'utf8');
assert.ok(fixture.includes('// PRODUCTION_METHODS'));
const out = fs.mkdtempSync(path.join(os.tmpdir(), 'newtonia-analytics-cpp-'));
try {
  const source = path.join(out, 'test.cpp');
  const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
  fs.writeFileSync(source, fixture.replace('// PRODUCTION_METHODS', methods)
    .replace('// PRODUCTION_FINGERS', fingerMethods)
    .replace('// PRODUCTION_MOTION', motion));
  execFileSync(process.env.CXX || 'g++', ['-std=c++11', '-Wall', '-Wextra', '-Werror',
    '-I', root, source, '-o', binary], {stdio:'inherit'});
  execFileSync(binary, [], {stdio:'inherit'});
} finally { fs.rmSync(out, {recursive:true, force:true}); }
