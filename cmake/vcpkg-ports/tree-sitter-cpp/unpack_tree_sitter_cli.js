const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const [, , inputPath, outputPath] = process.argv;

if (!inputPath || !outputPath) {
  console.error('usage: node unpack_tree_sitter_cli.js <tree-sitter.gz> <tree-sitter-executable>');
  process.exit(1);
}

fs.mkdirSync(path.dirname(outputPath), { recursive: true });
fs.writeFileSync(outputPath, zlib.gunzipSync(fs.readFileSync(inputPath)), { mode: 0o755 });
