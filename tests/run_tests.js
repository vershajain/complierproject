// Runs the analyzer on every tests/c_samples/*.c and compares the set of triggered rule ids
// with tests/expected/<name>.json. Usage: node tests/run_tests.js [--update]
const { execFileSync } = require("child_process");
const fs = require("fs");
const path = require("path");

const exe = path.join(__dirname, "..", "analyzer", process.platform === "win32" ? "clint.exe" : "clint");
const samples = path.join(__dirname, "c_samples");
const expectedDir = path.join(__dirname, "expected");
const update = process.argv.includes("--update");

if (!fs.existsSync(exe)) {
  console.error("Analyzer not built yet. Run: npm run build:analyzer");
  process.exit(1);
}

let passed = 0;
let failed = 0;
for (const file of fs.readdirSync(samples).filter((f) => f.endsWith(".c")).sort()) {
  const name = file.replace(/\.c$/, "");
  const out = execFileSync(exe, [path.join(samples, file)], { maxBuffer: 50 * 1024 * 1024 }).toString();
  const rules = JSON.parse(out).diagnostics.map((d) => d.rule).sort();
  const expFile = path.join(expectedDir, name + ".json");
  if (update) fs.writeFileSync(expFile, JSON.stringify({ rules }, null, 2) + "\n");
  const expected = JSON.parse(fs.readFileSync(expFile, "utf8")).rules.slice().sort();
  const ok = JSON.stringify(rules) === JSON.stringify(expected);
  console.log((ok ? "PASS  " : "FAIL  ") + name);
  if (!ok) {
    console.log("   expected: " + JSON.stringify(expected));
    console.log("   actual:   " + JSON.stringify(rules));
  }
  ok ? passed++ : failed++;
}
console.log("\n" + passed + " passed, " + failed + " failed");
process.exit(failed ? 1 : 0);
