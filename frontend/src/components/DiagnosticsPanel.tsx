import { useState } from "react";
import type { Diagnostic, Severity } from "../types";

interface Props {
  diagnostics: Diagnostic[];
  onSelect: (line: number, col: number) => void;
}

export default function DiagnosticsPanel({ diagnostics, onSelect }: Props) {
  const [filter, setFilter] = useState<Severity | "all">("all");
  const shown = diagnostics.filter((d) => filter === "all" || d.severity === filter);

  if (diagnostics.length === 0) return <div className="empty good">No issues found. Nice work!</div>;

  return (
    <div>
      <div className="filters">
        {(["all", "error", "warning", "info"] as const).map((f) => (
          <button key={f} className={filter === f ? "chip active" : "chip"} onClick={() => setFilter(f)}>
            {f === "all" ? "All" : f[0].toUpperCase() + f.slice(1)}
          </button>
        ))}
      </div>
      {shown.map((d, i) => (
        <div key={i} className={`diag ${d.severity}`} onClick={() => onSelect(d.line, d.col)} title="Click to show in the editor">
          <div className="diag-head">
            <span className={`badge ${d.severity}`}>{d.severity.toUpperCase()}</span>
            <b>{d.title}</b>
            <span className="loc">Line {d.line}, Col {d.col}</span>
          </div>
          {d.snippet && d.snippet.trim() && (
            <pre className="snippet"><span className="ln">{d.line}</span> {d.snippet}{"\n"}<span className="ln">{" ".repeat(String(d.line).length)}</span> {" ".repeat(Math.max(0, d.col - 1))}<span className="caret">^</span></pre>
          )}
          <p><b>What is wrong:</b> {d.message}</p>
          {d.why && <p className="why"><b>Why it is a problem:</b> {d.why}</p>}
          <p className="suggestion"><b>How to fix:</b> {d.suggestion}</p>
          <div className="meta">
            <span>Confidence: <b>{d.confidence}</b></span>
            <span>Found in: {d.phase} analysis</span>
            {d.category && <span>Category: {d.category}</span>}
            <span className="rule">{d.rule}</span>
          </div>
        </div>
      ))}
    </div>
  );
}
