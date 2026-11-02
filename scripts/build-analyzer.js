// Compiles analyzer/src/*.cpp into analyzer/clint(.exe). Works on Windows (MinGW/MSYS2) and Linux/macOS.
const { execFileSync } = require("child_process");
const fs = require("fs");
const path = require("path");

const srcDir = path.join(__dirname, "..", "analyzer", "src");
const out = path.join(__dirname, "..", "analyzer", process.platform === "win32" ? "clint.exe" : "clint");
const files = fs.readdirSync(srcDir).filter((f) => f.endsWith(".cpp")).map((f) => path.join(srcDir, f));

console.log("Compiling " + files.length + " files...");
try {
  execFileSync("g++", ["-std=c++17", "-O2", "-Wall", "-Wextra", "-o", out, ...files], { stdio: "inherit" });
  console.log("Built: " + out);
} catch (e) {
  console.error("\nBuild failed. Make sure g++ is installed and on your PATH (g++ --version).");
  process.exit(1);
}
