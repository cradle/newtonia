// CI supplies its just-compiled artifact; standalone tests compile in isolation.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.resolve(__dirname, '../..');
module.exports = function webTestSource() {
  if (process.env.NEWTONIA_WEB_JS) {
    return fs.readFileSync(path.resolve(root, process.env.NEWTONIA_WEB_JS), 'utf8');
  }
  const out = fs.mkdtempSync(path.join(os.tmpdir(), 'newtonia-web-test-'));
  try {
    execFileSync('tsc', ['-p', path.join(root, 'web/tsconfig.json'), '--outDir', out]);
    return fs.readFileSync(path.join(out, 'main.js'), 'utf8');
  } finally { fs.rmSync(out, {recursive:true, force:true}); }
};
