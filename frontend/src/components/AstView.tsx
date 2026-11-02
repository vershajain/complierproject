import { useEffect, useMemo, useRef, useState } from "react";
import type { AstNode } from "../types";

function groupOf(kind: string): string {
  if (["Program", "Function", "Param"].includes(kind)) return "g-top";
  if (["Block", "DeclList", "VarDecl", "InitList"].includes(kind)) return "g-decl";
  if (["If", "While", "DoWhile", "For", "Return", "Break", "Continue", "ExprStmt", "Empty"].includes(kind)) return "g-stmt";
  if (["Ident", "IntLit", "FloatLit", "CharLit", "StringLit"].includes(kind)) return "g-leaf";
  return "g-expr";
}

function countNodes(n: AstNode): number {
  return 1 + n.children.reduce((s, c) => s + (c.kind === "Empty" ? 0 : countNodes(c)), 0);
}

function TreeNode({ node, depth, openDepth }: { node: AstNode; depth: number; openDepth: number }) {
  const [open, setOpen] = useState(depth < openDepth);
  const kids = node.children.filter((c) => c.kind !== "Empty");
  return (
    <li>
      <div
        className={`ast-node ${groupOf(node.kind)} ${kids.length ? "clickable" : ""}`}
        onClick={() => kids.length && setOpen(!open)}
        title={`Line ${node.line}, column ${node.col}`}
      >
        {node.label}
        {kids.length > 0 && <span className="toggle">{open ? "−" : "+"}</span>}
      </div>
      {open && kids.length > 0 && (
        <ul>
          {kids.map((c, i) => <TreeNode key={i} node={c} depth={depth + 1} openDepth={openDepth} />)}
        </ul>
      )}
    </li>
  );
}

export default function AstView({ ast }: { ast: AstNode | null }) {
  const total = useMemo(() => (ast ? countNodes(ast) : 0), [ast]);
  // Big programs start collapsed to a few levels so the tree stays readable; every node can be opened by clicking it.
  const [mode, setMode] = useState({ depth: 99, nonce: 0 });
  const [zoom, setZoom] = useState(1);
  const [full, setFull] = useState(false);
  const scroller = useRef<HTMLDivElement>(null);
  const inner = useRef<HTMLDivElement>(null);

  useEffect(() => {
    setMode((m) => ({ depth: total > 120 ? 4 : 99, nonce: m.nonce + 1 }));
    setZoom(1);
  }, [ast, total]);

  if (!ast) return <div className="empty">No AST available because the code has syntax errors.</div>;

  const setDepth = (depth: number) => setMode((m) => ({ depth, nonce: m.nonce + 1 }));
  const clamp = (z: number) => Math.min(2, Math.max(0.2, Math.round(z * 100) / 100));
  const fit = () => {
    const s = scroller.current, i = inner.current;
    if (!s || !i) return;
    const natural = i.scrollWidth / zoom;
    setZoom(clamp((s.clientWidth - 24) / natural));
    s.scrollTo({ left: 0, top: 0 });
  };

  return (
    <div className={full ? "ast-wrap ast-full" : "ast-wrap"}>
      <div className="legend">
        <span className="ast-node g-top">Program / Function</span>
        <span className="ast-node g-decl">Declaration</span>
        <span className="ast-node g-stmt">Statement</span>
        <span className="ast-node g-expr">Expression</span>
        <span className="ast-node g-leaf">Name / value</span>
      </div>
      <div className="ast-bar">
        <span className="hint">{total} nodes · click a node to collapse or expand it</span>
        <span className="spacer" />
        <button className="chip" onClick={() => setDepth(99)}>Expand all</button>
        <button className="chip" onClick={() => setDepth(3)}>Collapse to level 3</button>
        <button className="chip" onClick={() => setZoom(clamp(zoom - 0.1))} title="Zoom out">−</button>
        <span className="zoom-label">{Math.round(zoom * 100)}%</span>
        <button className="chip" onClick={() => setZoom(clamp(zoom + 0.1))} title="Zoom in">+</button>
        <button className="chip" onClick={fit}>Fit width</button>
        <button className="chip" onClick={() => setZoom(1)}>100%</button>
        <button className="chip" onClick={() => setFull(!full)}>{full ? "Close full screen" : "Full screen"}</button>
      </div>
      <div className="tree-scroll" ref={scroller}>
        <div className="tree-inner" ref={inner} style={{ zoom }}>
          <div className="tree"><ul><TreeNode key={mode.nonce} node={ast} depth={0} openDepth={mode.depth} /></ul></div>
        </div>
      </div>
    </div>
  );
}
