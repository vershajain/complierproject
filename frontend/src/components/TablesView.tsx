import type { SymbolInfo, TokenInfo } from "../types";

export function SymbolsView({ symbols }: { symbols: SymbolInfo[] }) {
  if (!symbols.length) return <div className="empty">The symbol table is empty (it is built after the code parses without syntax errors).</div>;
  return (
    <table>
      <thead><tr><th>Name</th><th>Kind</th><th>Type</th><th>Scope</th><th>Declared</th><th>Initialized</th><th>Used</th></tr></thead>
      <tbody>
        {symbols.map((s, i) => (
          <tr key={i} className={!s.used && s.kind === "variable" ? "row-warn" : ""}>
            <td><code>{s.name}</code></td><td>{s.kind}</td><td><code>{s.type}</code></td><td>{s.scope}</td>
            <td>{s.line}:{s.col}</td><td>{s.initialized ? "yes" : "no"}</td><td>{s.used ? "yes" : "no"}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

export function TokensView({ tokens }: { tokens: TokenInfo[] }) {
  if (!tokens.length) return <div className="empty">No tokens.</div>;
  return (
    <table>
      <thead><tr><th>#</th><th>Token type</th><th>Text</th><th>Line</th><th>Column</th></tr></thead>
      <tbody>
        {tokens.map((t, i) => (
          <tr key={i}><td>{i + 1}</td><td>{t.type}</td><td><code>{t.text}</code></td><td>{t.line}</td><td>{t.col}</td></tr>
        ))}
      </tbody>
    </table>
  );
}
