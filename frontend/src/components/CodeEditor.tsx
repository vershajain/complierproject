import Editor, { OnMount } from "@monaco-editor/react";
import { useEffect, useRef } from "react";
import type { Diagnostic } from "../types";

type EditorInstance = Parameters<OnMount>[0];
type MonacoInstance = Parameters<OnMount>[1];

interface Props {
  value: string;
  onChange: (v: string) => void;
  onAnalyze: () => void;
  diagnostics: Diagnostic[];
  reveal: { line: number; col: number; nonce: number } | null;
}

const SEVERITY = { error: 8, warning: 4, info: 2 } as const;

export default function CodeEditor({ value, onChange, onAnalyze, diagnostics, reveal }: Props) {
  const editorRef = useRef<EditorInstance | null>(null);
  const monacoRef = useRef<MonacoInstance | null>(null);
  const analyzeRef = useRef(onAnalyze);
  analyzeRef.current = onAnalyze;

  const handleMount: OnMount = (editor, monaco) => {
    editorRef.current = editor;
    monacoRef.current = monaco;
    // Ctrl+Enter runs the analysis
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, () => analyzeRef.current());
  };

  // Underline problems in the editor
  useEffect(() => {
    const editor = editorRef.current;
    const monaco = monacoRef.current;
    const model = editor?.getModel();
    if (!editor || !monaco || !model) return;
    const markers = diagnostics.map((d) => {
      const line = Math.min(Math.max(d.line, 1), model.getLineCount());
      const word = model.getWordAtPosition({ lineNumber: line, column: d.col });
      return {
        severity: SEVERITY[d.severity],
        message: `${d.title}: ${d.message}`,
        startLineNumber: line,
        startColumn: d.col,
        endLineNumber: line,
        endColumn: word ? word.endColumn : d.col + 1,
      };
    });
    monaco.editor.setModelMarkers(model, "clint", markers);
  }, [diagnostics, value]);

  // Jump to a line when a diagnostic is clicked
  useEffect(() => {
    const editor = editorRef.current;
    if (!editor || !reveal) return;
    editor.revealLineInCenter(reveal.line);
    editor.setPosition({ lineNumber: reveal.line, column: reveal.col });
    editor.focus();
  }, [reveal]);

  return (
    <Editor
      height="100%"
      defaultLanguage="c"
      theme="vs-dark"
      value={value}
      onChange={(v) => onChange(v ?? "")}
      onMount={handleMount}
      options={{ fontSize: 14, minimap: { enabled: false }, scrollBeyondLastLine: false, automaticLayout: true, tabSize: 4 }}
    />
  );
}
