import { useCallback, useState } from "react";
import { analyze } from "./api";
import AiFixPanel from "./components/AiFixPanel";
import AstView from "./components/AstView";
import CfgView from "./components/CfgView";
import CodeEditor from "./components/CodeEditor";
import DiagnosticsPanel from "./components/DiagnosticsPanel";
import ReportSummary from "./components/ReportSummary";
import { SymbolsView, TokensView } from "./components/TablesView";
import { SAMPLES } from "./samples";
import type { AnalysisResult } from "./types";

type Tab = "diagnostics" | "ast" | "cfg" | "symbols" | "tokens" | "ai";
const TABS: [Tab, string][] = [
  ["diagnostics", "Diagnostics"], ["ast", "AST"], ["cfg", "CFG"], ["symbols", "Symbol table"], ["tokens", "Tokens"], ["ai", "AI Fix"],
];

export default function App() {
  const [code, setCode] = useState(SAMPLES[0].code);
  const [result, setResult] = useState<AnalysisResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");
  const [tab, setTab] = useState<Tab>("diagnostics");
  const [reveal, setReveal] = useState<{ line: number; col: number; nonce: number } | null>(null);

  const runAnalysis = useCallback(async (source?: string) => {
    setLoading(true);
    setError("");
    try {
      setResult(await analyze(source ?? code));
    } catch (e) {
      setError((e as Error).message + " (is the backend running? Start everything with: npm run dev)");
    } finally {
      setLoading(false);
    }
  }, [code]);

  const select = (line: number, col: number) => setReveal({ line, col, nonce: Date.now() });

  return (
    <div className="app">
      <header>
        <div className="brand"><b>C-Lint</b><span>Compiler-based syntax checker &amp; static analyzer for C</span></div>
        <div className="toolbar">
          <select onChange={(e) => { const s = SAMPLES[Number(e.target.value)]; if (s) { setCode(s.code); setResult(null); } }} defaultValue="">
            <option value="" disabled>Load an example…</option>
            {SAMPLES.map((s, i) => <option key={s.name} value={i}>{s.name}</option>)}
          </select>
          <button className="primary" onClick={() => runAnalysis()} disabled={loading}>
            {loading ? "Analyzing…" : "Analyze (Ctrl+Enter)"}
          </button>
        </div>
      </header>

      <main>
        <section className="editor-pane">
          <CodeEditor value={code} onChange={setCode} onAnalyze={() => runAnalysis()} diagnostics={result?.diagnostics ?? []} reveal={reveal} />
        </section>

        <section className="result-pane">
          {error && <div className="note err">{error}</div>}
          {!result && !error && (
            <div className="empty">
              Write or paste C code on the left and click <b>Analyze</b>.<br />
              C-Lint reads the code without running it and explains what might be wrong.
            </div>
          )}
          {result && (
            <>
              <ReportSummary summary={result.summary} success={result.success} />
              <nav className="tabs">
                {TABS.map(([id, label]) => (
                  <button key={id} className={tab === id ? "tab active" : "tab"} onClick={() => setTab(id)}>
                    {label}{id === "diagnostics" ? ` (${result.diagnostics.length})` : ""}
                  </button>
                ))}
              </nav>
              <div className="tab-body">
                {tab === "diagnostics" && <DiagnosticsPanel diagnostics={result.diagnostics} onSelect={select} />}
                {tab === "ast" && <AstView ast={result.ast} />}
                {tab === "cfg" && <CfgView cfgs={result.cfgs} />}
                {tab === "symbols" && <SymbolsView symbols={result.symbols} />}
                {tab === "tokens" && <TokensView tokens={result.tokens} />}
                {tab === "ai" && (
                  <AiFixPanel
                    code={code}
                    diagnostics={result.diagnostics}
                    onApply={(c) => { setCode(c); setResult(null); runAnalysis(c); setTab("diagnostics"); }}
                  />
                )}
              </div>
            </>
          )}
        </section>
      </main>
    </div>
  );
}
