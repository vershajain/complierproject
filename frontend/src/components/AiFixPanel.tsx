import { useState } from "react";
import { aiFix } from "../api";
import type { AiFixResult, Diagnostic } from "../types";

interface Props {
  code: string;
  diagnostics: Diagnostic[];
  onApply: (code: string) => void;
}

export default function AiFixPanel({ code, diagnostics, onApply }: Props) {
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");
  const [result, setResult] = useState<AiFixResult | null>(null);

  const run = async () => {
    setLoading(true); setError(""); setResult(null);
    try { setResult(await aiFix(code, diagnostics)); }
    catch (e) { setError((e as Error).message); }
    finally { setLoading(false); }
  };

  return (
    <div>
      <p className="hint">
        The AI receives your code and C-Lint&apos;s findings, proposes a fix, and then <b>C-Lint analyzes the fix again</b> to check whether the problems are really gone.
        AI suggestions can be wrong, so always review the code before applying it.
      </p>
      <button className="primary" onClick={run} disabled={loading || diagnostics.length === 0}>
        {loading ? "Asking the AI…" : "Get AI fix"}
      </button>
      {diagnostics.length === 0 && <span className="hint"> Nothing to fix.</span>}
      {error && <div className="note err">{error}</div>}
      {result && (
        <div className="ai-result">
          <h4>Verification by C-Lint{result.attempts === 2 ? " (improved on a second try)" : ""}</h4>
          <div className="verify">
            <div>Before: <b>{result.before.errors}</b> errors, <b>{result.before.warnings}</b> warnings</div>
            <div>After: <b>{result.after.errors}</b> errors, <b>{result.after.warnings}</b> warnings</div>
          </div>
          <p>
            <span className="badge info">{result.resolved.length} resolved</span>{" "}
            <span className="badge warning">{result.remaining.length} still present</span>{" "}
            <span className="badge error">{result.introduced.length} new</span>
          </p>
          {result.remaining.length + result.introduced.length > 0 && (
            <ul className="small">
              {result.remaining.map((d, i) => <li key={"r" + i}>Still present: {d.message}</li>)}
              {result.introduced.map((d, i) => <li key={"n" + i}>New: {d.message}</li>)}
            </ul>
          )}
          <h4>What changed</h4>
          <p className="pre">{result.explanation}</p>
          <h4>Suggested code</h4>
          <pre className="code">{result.fixedCode}</pre>
          <button className="primary" onClick={() => onApply(result.fixedCode)}>Apply to editor</button>
          <button onClick={() => navigator.clipboard.writeText(result.fixedCode)}>Copy</button>
        </div>
      )}
    </div>
  );
}
