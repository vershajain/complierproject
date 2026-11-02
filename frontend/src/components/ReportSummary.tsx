import type { Summary } from "../types";

export default function ReportSummary({ summary, success }: { summary: Summary; success: boolean }) {
  const color = summary.score >= 80 ? "var(--ok)" : summary.score >= 50 ? "var(--warn)" : "var(--err)";
  return (
    <div className="report">
      <div className="report-counts">
        <div className="count err"><b>{summary.errors}</b><span>Errors</span></div>
        <div className="count warn"><b>{summary.warnings}</b><span>Warnings</span></div>
        <div className="count info"><b>{summary.infos}</b><span>Info</span></div>
        <div className="count"><b>{summary.lines}</b><span>Lines</span></div>
        <div className="count"><b>{summary.tokens}</b><span>Tokens</span></div>
      </div>
      <div className="score">
        <div className="score-label">Code quality score <b style={{ color }}>{summary.score}/100</b></div>
        <div className="score-bar"><div style={{ width: `${summary.score}%`, background: color }} /></div>
      </div>
      {!success && (
        <div className="note">
          Syntax errors were found. Checks that look at single statements (types, declarations, format strings, risky expressions) still ran, but the flow-based checks (uninitialized variables, unreachable code, missing return, loops, memory, null pointers) and the CFG need a complete program, so they were skipped. Fix the syntax errors and analyze again to see them.
        </div>
      )}
    </div>
  );
}
