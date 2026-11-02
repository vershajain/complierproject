import { execFile } from "node:child_process";
import { randomUUID } from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const exeName = process.platform === "win32" ? "clint.exe" : "clint";
// backend/src/services -> up three levels = project root
export const ANALYZER_PATH = process.env.CLINT_PATH || path.resolve(__dirname, "../../../analyzer", exeName);

export const analyzerExists = () => fs.existsSync(ANALYZER_PATH);

export interface AnalysisResult {
  success: boolean;
  summary: { errors: number; warnings: number; infos: number; lines: number; tokens: number; score: number };
  diagnostics: Array<{
    rule: string; title: string; severity: "error" | "warning" | "info";
    line: number; col: number; message: string; suggestion: string; why: string; confidence: string; phase: string;
  }>;
  tokens: unknown[];
  symbols: unknown[];
  ast: unknown;
  cfgs: unknown[];
}

/** Saves the code to a temporary .c file, runs clint on it, and returns the parsed JSON. */
export async function runAnalyzer(code: string): Promise<AnalysisResult> {
  if (!analyzerExists()) {
    throw new Error("The C++ analyzer is not built yet. Run: npm run build:analyzer");
  }
  const file = path.join(os.tmpdir(), `clint-${randomUUID()}.c`);
  await fs.promises.writeFile(file, code, "utf8");
  try {
    const stdout = await new Promise<string>((resolve, reject) => {
      execFile(ANALYZER_PATH, [file], { timeout: 10_000, maxBuffer: 50 * 1024 * 1024 }, (err, out, stderr) => {
        if (err) reject(new Error(stderr?.trim() || err.message));
        else resolve(out);
      });
    });
    return JSON.parse(stdout) as AnalysisResult;
  } finally {
    fs.promises.unlink(file).catch(() => undefined);
  }
}
