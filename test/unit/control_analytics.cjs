// Compile the production analytics methods with small state/player fixtures.
// No SDL/GL runtime required. Run from any cwd: node test/unit/control_analytics.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.resolve(__dirname, '../..');
function slice(file, name) {
  const source = fs.readFileSync(path.join(root, file), 'utf8');
  const begin = `// TEST-SLICE-BEGIN: ${name}`;
  const finish = `// TEST-SLICE-END: ${name}`;
  const start = source.indexOf(begin), end = source.indexOf(finish);
  assert.ok(start >= 0 && end > start, `${file}: missing/reversed markers for ${name}`);
  assert.equal(source.indexOf(begin, start + begin.length), -1, `${name}: duplicate begin`);
  assert.equal(source.indexOf(finish, end + finish.length), -1, `${name}: duplicate end`);
  return source.slice(start + begin.length, end);
}
const methods = [
  slice('glship.cpp', 'analytics_ship'),
  slice('glgame.cpp', 'analytics_game'),
  slice('state_manager.cpp', 'analytics_state'),
  slice('glgame.cpp', 'analytics_pause'),
].join('\n');
const fingerMethods = slice('web_main.cpp', 'analytics_fingers');
const motion = slice('web_main.cpp', 'analytics_motion').replace('case SDL_FINGERMOTION:', '');
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
