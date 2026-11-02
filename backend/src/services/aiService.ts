export class AiNotConfiguredError extends Error {}

export interface FixSuggestion { fixedCode: string; explanation: string }

interface DiagnosticLike { severity: string; rule: string; title: string; line: number; col: number; message: string; suggestion: string; why?: string }

export interface PreviousAttempt { fixedCode: string; problems: DiagnosticLike[] }

type Provider = "anthropic" | "openai" | "gemini";

const SYSTEM_PROMPT = `You are a careful C programming tutor. You receive a C program and the problems that a static analyzer (C-Lint) found in it.
Rewrite the program so those problems are fixed, changing as little as possible. Keep the program's intended behaviour.
Do not rename functions, do not remove working code, and do not add features. Do not just silence a warning: fix the real cause.
Info-level findings (for example "result is always 0") only need a change if the code clearly does not do what was intended.
Only use this subset of C: no struct/union/enum/typedef/goto.
Reply with ONLY a JSON object, no markdown fences, in this exact shape:
{"fixedCode": "<the complete corrected C program>", "explanation": "<short plain-English explanation of each change, one per line, mentioning the line number, what was wrong and why the new code is correct, for a student>"}`;

/** Picks the provider: AI_PROVIDER if set, otherwise whichever API key is present. */
function pickProvider(): { provider: Provider; key: string } | null {
  const keys: Record<Provider, string | undefined> = {
    anthropic: process.env.ANTHROPIC_API_KEY?.trim(),
    openai: process.env.OPENAI_API_KEY?.trim(),
    gemini: process.env.GEMINI_API_KEY?.trim(),
  };
  const wanted = process.env.AI_PROVIDER?.trim().toLowerCase() as Provider | undefined;
  if (wanted && keys[wanted]) return { provider: wanted, key: keys[wanted]! };
  for (const p of ["anthropic", "openai", "gemini"] as Provider[]) if (keys[p]) return { provider: p, key: keys[p]! };
  return null;
}

export const isAiConfigured = () => pickProvider() !== null;

async function callModel(provider: Provider, key: string, system: string, user: string): Promise<string> {
  const model = process.env.AI_MODEL?.trim();
  let response: Response;
  if (provider === "anthropic") {
    response = await fetch("https://api.anthropic.com/v1/messages", {
      method: "POST",
      headers: { "content-type": "application/json", "x-api-key": key, "anthropic-version": "2023-06-01" },
      body: JSON.stringify({ model: model || "claude-sonnet-5-5", max_tokens: 4000, system, messages: [{ role: "user", content: user }] }),
    });
  } else if (provider === "openai") {
    response = await fetch("https://api.openai.com/v1/chat/completions", {
      method: "POST",
      headers: { "content-type": "application/json", authorization: `Bearer ${key}` },
      body: JSON.stringify({ model: model || "gpt-4o-mini", messages: [{ role: "system", content: system }, { role: "user", content: user }] }),
    });
  } else {
    const m = model || "gemini-2.0-flash";
    response = await fetch(`https://generativelanguage.googleapis.com/v1beta/models/${encodeURIComponent(m)}:generateContent`, {
      method: "POST",
      headers: { "content-type": "application/json", "x-goog-api-key": key },
      body: JSON.stringify({ systemInstruction: { parts: [{ text: system }] }, contents: [{ role: "user", parts: [{ text: user }] }] }),
    });
  }
  if (!response.ok) throw new Error(`AI service error (${provider}, ${response.status}): ${(await response.text()).slice(0, 300)}`);
  const data = (await response.json()) as any;
  if (provider === "anthropic") return (data.content ?? []).filter((c: any) => c.type === "text").map((c: any) => c.text ?? "").join("\n");
  if (provider === "openai") return data.choices?.[0]?.message?.content ?? "";
  return (data.candidates?.[0]?.content?.parts ?? []).map((p: any) => p.text ?? "").join("\n");
}

export async function requestFix(code: string, diagnostics: DiagnosticLike[], previous?: PreviousAttempt): Promise<FixSuggestion> {
  const chosen = pickProvider();
  if (!chosen) {
    throw new AiNotConfiguredError(
      "AI Fix needs an API key. Create the file backend/.env (copy backend/.env.example) and add ONE of: ANTHROPIC_API_KEY, OPENAI_API_KEY or GEMINI_API_KEY. Then stop and restart 'npm run dev'."
    );
  }

  const issues = diagnostics
    .map((d) => `- [${d.severity}] ${d.title} at line ${d.line}, column ${d.col}: ${d.message}${d.why ? ` Why it matters: ${d.why}` : ""} (suggestion: ${d.suggestion})`)
    .join("\n");
  let userMessage = `C program:\n\`\`\`c\n${code}\n\`\`\`\n\nProblems found by C-Lint:\n${issues}`;
  if (previous) {
    const left = previous.problems
      .map((d) => `- [${d.severity}] ${d.title} at line ${d.line}, column ${d.col}: ${d.message}`)
      .join("\n");
    userMessage += `\n\nYour previous attempt was re-checked by C-Lint and still has problems:\n\`\`\`c\n${previous.fixedCode}\n\`\`\`\n${left}\n\nFix the ORIGINAL program again so that none of the problems above remain and no new ones appear.`;
  }

  const text = await callModel(chosen.provider, chosen.key, SYSTEM_PROMPT, userMessage);
  return parseSuggestion(text);
}

function parseSuggestion(text: string): FixSuggestion {
  const cleaned = text.replace(/^```(?:json)?\s*/i, "").replace(/\s*```$/, "").trim();
  // Try the whole reply first, then the outermost {...} in case the model added text around the JSON.
  const first = cleaned.indexOf("{");
  const last = cleaned.lastIndexOf("}");
  const candidates = [cleaned, ...(first >= 0 && last > first ? [cleaned.slice(first, last + 1)] : [])];
  for (const candidate of candidates) {
    try {
      const obj = JSON.parse(candidate);
      if (typeof obj.fixedCode === "string") return { fixedCode: obj.fixedCode, explanation: String(obj.explanation ?? "") };
    } catch {
      /* try the next candidate */
    }
  }
  const block = text.match(/```(?:c)?\s*([\s\S]*?)```/);
  if (block) return { fixedCode: block[1].trim(), explanation: text.replace(block[0], "").trim() };
  throw new Error("The AI reply was not in the expected format. Please try again.");
}
