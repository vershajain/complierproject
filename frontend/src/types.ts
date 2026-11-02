export type Severity = "error" | "warning" | "info";

export interface Diagnostic {
  rule: string;
  title: string;
  severity: Severity;
  line: number;
  col: number;
  message: string;
  suggestion: string;
  why?: string;
  category?: string;
  snippet?: string;
  confidence: string;
  phase: string;
}

export interface Summary { errors: number; warnings: number; infos: number; lines: number; tokens: number; score: number }
export interface TokenInfo { type: string; text: string; line: number; col: number }
export interface SymbolInfo { name: string; type: string; kind: string; scope: string; line: number; col: number; initialized: boolean; used: boolean }
export interface AstNode { kind: string; label: string; line: number; col: number; children: AstNode[] }
export interface CfgBlock { id: number; title: string; reachable: boolean; line: number; lines: string[]; succ: { to: number; label: string }[] }
export interface Cfg { function: string; line: number; entry: number; exit: number; blocks: CfgBlock[] }

export interface AnalysisResult {
  success: boolean;
  summary: Summary;
  diagnostics: Diagnostic[];
  tokens: TokenInfo[];
  symbols: SymbolInfo[];
  ast: AstNode | null;
  cfgs: Cfg[];
}

export interface AiFixResult {
  fixedCode: string;
  explanation: string;
  attempts?: number;
  before: Summary;
  after: Summary;
  resolved: Diagnostic[];
  remaining: Diagnostic[];
  introduced: Diagnostic[];
}
