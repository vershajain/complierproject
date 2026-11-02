import { Router } from "express";
import { AiNotConfiguredError, requestFix } from "../services/aiService";
import { runAnalyzer } from "../services/analyzerRunner";

export const aiFixRouter = Router();

const key = (d: { rule: string; message: string }) => `${d.rule}|${d.message}`;

aiFixRouter.post("/", async (req, res) => {
  const code = req.body?.code;
  if (typeof code !== "string" || code.trim() === "") return res.status(400).json({ error: "Missing 'code'." });
  try {
    // Always analyze again on the server so the AI sees trustworthy findings.
    const before = await runAnalyzer(code);
    if (before.diagnostics.length === 0) return res.status(400).json({ error: "C-Lint found no issues, so there is nothing to fix." });

    // The key idea: do not trust the AI blindly. Run its answer through C-Lint again,
    // and if errors/warnings are left (or new ones appeared), give the AI one more chance with that feedback.
    // Info-level notes are hints only and never trigger a retry.
    let attempts = 1;
    let fix = await requestFix(code, before.diagnostics);
    let after = await runAnalyzer(fix.fixedCode);
    const leftovers = after.diagnostics.filter((d) => d.severity !== "info");
    if (leftovers.length > 0) {
      try {
        const retryFix = await requestFix(code, before.diagnostics, { fixedCode: fix.fixedCode, problems: leftovers });
        const retryAfter = await runAnalyzer(retryFix.fixedCode);
        attempts = 2;
        // Keep the retry only if it is actually better (fewer errors/warnings).
        const score = (r: typeof before) => r.summary.errors * 2 + r.summary.warnings;
        if (score(retryAfter) <= score(after)) { fix = retryFix; after = retryAfter; }
      } catch {
        /* the retry is a bonus; keep the first answer if it fails */
      }
    }
    const afterKeys = new Set(after.diagnostics.map(key));
    const beforeKeys = new Set(before.diagnostics.map(key));
    res.json({
      fixedCode: fix.fixedCode,
      explanation: fix.explanation,
      attempts,
      before: before.summary,
      after: after.summary,
      resolved: before.diagnostics.filter((d) => !afterKeys.has(key(d))),
      remaining: after.diagnostics.filter((d) => beforeKeys.has(key(d))),
      introduced: after.diagnostics.filter((d) => !beforeKeys.has(key(d))),
      afterResult: after,
    });
  } catch (e) {
    const status = e instanceof AiNotConfiguredError ? 503 : 500;
    res.status(status).json({ error: (e as Error).message });
  }
});
