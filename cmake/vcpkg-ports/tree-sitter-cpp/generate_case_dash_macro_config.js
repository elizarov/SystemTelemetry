const fs = require('fs');
const path = require('path');

const [, , inputPath, outputPath] = process.argv;

if (!inputPath || !outputPath) {
  console.error('usage: node generate_case_dash_macro_config.js <format_config.json> <case_dash_macro_config.js>');
  process.exit(1);
}

const requiredCategories = [
  'calling_convention',
];

function fail(message) {
  console.error(message);
  process.exit(1);
}

const config = JSON.parse(fs.readFileSync(inputPath, 'utf8'));
if (config.schema_version !== 1) {
  fail('format_config.json schema_version must be 1');
}
if (!config.macro_categories || typeof config.macro_categories !== 'object' || Array.isArray(config.macro_categories)) {
  fail('format_config.json macro_categories must be an object');
}

const macroNamePattern = /^[A-Za-z_][A-Za-z0-9_]*$/;
const categories = {};
for (const category of requiredCategories) {
  const names = config.macro_categories[category];
  if (!Array.isArray(names) || names.length === 0) {
    fail(`format_config.json macro_categories.${category} must be a non-empty array`);
  }
  const seen = new Set();
  categories[category] = names.map((name) => {
    if (typeof name !== 'string' || !macroNamePattern.test(name)) {
      fail(`format_config.json macro ${category} entry is not a C/C++ macro name: ${JSON.stringify(name)}`);
    }
    if (seen.has(name)) {
      fail(`format_config.json macro ${category} entry is duplicated: ${name}`);
    }
    seen.add(name);
    return name;
  });
}

let output = '// Generated from tools/format_config.json by the tree-sitter-cpp overlay port.\n';
output += 'module.exports = {\n';
output += '  macro_categories: {\n';
for (const category of requiredCategories) {
  output += `    ${category}: [\n`;
  for (const name of categories[category]) {
    output += `      ${JSON.stringify(name)},\n`;
  }
  output += '    ],\n';
}
output += '  },\n';
output += '};\n';

fs.mkdirSync(path.dirname(outputPath), { recursive: true });
fs.writeFileSync(outputPath, output, 'utf8');
