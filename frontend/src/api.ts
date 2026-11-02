import type { AiFixResult, AnalysisResult, Diagnostic } from "./types";

async function post<T>(url: string, body: unknown): Promise<T> {
  const res = await fetch(url, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || `Request failed (${res.status})`);
  return data as T;
}

export const analyze = (code: string) => post<AnalysisResult>("/api/analyze", { code });
export const aiFix = (code: string, diagnostics: Diagnostic[]) => post<AiFixResult>("/api/ai-fix", { code, diagnostics });
