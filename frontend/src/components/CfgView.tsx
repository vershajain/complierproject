import { useMemo, useState } from "react";
import type { Cfg } from "../types";

const W = 190, GAP_X = 50, GAP_Y = 56, LINE_H = 16, MAX_LINES = 5;

function boxHeight(n: number) { return 28 + Math.min(n, MAX_LINES + 1) * LINE_H; }

function layout(cfg: Cfg) {
  const level = new Map<number, number>();
  const byId = new Map(cfg.blocks.map((b) => [b.id, b]));
  const bfs = (root: number, offset: number) => {
    level.set(root, offset);
    const q = [root];
    while (q.length) {
      const id = q.shift()!;
      for (const e of byId.get(id)!.succ) {
        if (!level.has(e.to) && byId.has(e.to)) { level.set(e.to, level.get(id)! + 1); q.push(e.to); }
      }
    }
  };
  bfs(cfg.entry, 0);
  for (const b of cfg.blocks) if (!level.has(b.id)) bfs(b.id, 1);   // dead code gets its own small trees

  const rows = new Map<number, number[]>();
  for (const b of cfg.blocks) {
    const l = level.get(b.id)!;
    rows.set(l, [...(rows.get(l) ?? []), b.id]);
  }
  const maxLevel = Math.max(...rows.keys());
  const rowH: number[] = [];
  for (let l = 0; l <= maxLevel; l++) rowH[l] = Math.max(0, ...(rows.get(l) ?? []).map((id) => boxHeight(byId.get(id)!.lines.length)));
  const rowY: number[] = [];
  let y = 20;
  for (let l = 0; l <= maxLevel; l++) { rowY[l] = y; y += rowH[l] + GAP_Y; }
  const maxCount = Math.max(...[...rows.values()].map((r) => r.length));
  const width = maxCount * (W + GAP_X) + GAP_X;

  const pos = new Map<number, { x: number; y: number; h: number }>();
  for (const [l, ids] of rows) {
    const total = ids.length * W + (ids.length - 1) * GAP_X;
    ids.forEach((id, i) => {
      pos.set(id, { x: (width - total) / 2 + i * (W + GAP_X), y: rowY[l], h: boxHeight(byId.get(id)!.lines.length) });
    });
  }
  return { pos, width, height: y, level };
}

export default function CfgView({ cfgs }: { cfgs: Cfg[] }) {
  const [idx, setIdx] = useState(0);
  const cfg = cfgs[Math.min(idx, cfgs.length - 1)];
  const lay = useMemo(() => (cfg ? layout(cfg) : null), [cfg]);

  if (!cfg || !lay) return <div className="empty">No control flow graph available (fix syntax errors first).</div>;

  const edges: JSX.Element[] = [];
  cfg.blocks.forEach((b) =>
    b.succ.forEach((e, i) => {
      const a = lay.pos.get(b.id), t = lay.pos.get(e.to);
      if (!a || !t) return;
      const back = lay.level.get(e.to)! <= lay.level.get(b.id)!;
      let d: string, lx: number, ly: number;
      if (!back) {
        const x1 = a.x + W / 2, y1 = a.y + a.h, x2 = t.x + W / 2, y2 = t.y;
        d = `M${x1},${y1} C${x1},${y1 + 30} ${x2},${y2 - 30} ${x2},${y2}`;
        lx = (x1 + x2) / 2; ly = (y1 + y2) / 2;
      } else {
        const x1 = a.x + W, y1 = a.y + a.h / 2, x2 = t.x + W, y2 = t.y + t.h / 2;
        const out = Math.max(x1, x2) + 40;
        d = `M${x1},${y1} C${out},${y1} ${out},${y2} ${x2},${y2}`;
        lx = out; ly = (y1 + y2) / 2;
      }
      const cls = e.label === "true" ? "e-true" : e.label === "false" ? "e-false" : "";
      edges.push(
        <g key={`${b.id}-${e.to}-${i}`}>
          <path d={d} className={`edge ${cls}`} markerEnd="url(#arrow)" />
          {e.label && <text x={lx + 4} y={ly - 3} className="edge-label">{e.label}</text>}
        </g>
      );
    })
  );

  return (
    <div>
      {cfgs.length > 1 && (
        <div className="filters">
          Function:
          {cfgs.map((c, i) => (
            <button key={c.function} className={i === idx ? "chip active" : "chip"} onClick={() => setIdx(i)}>{c.function}()</button>
          ))}
        </div>
      )}
      <div className="legend">
        <span className="cfg-key reach">reachable block</span>
        <span className="cfg-key dead">unreachable block</span>
        <span className="cfg-key io">entry / exit</span>
      </div>
      <div className="cfg-scroll">
        <svg width={lay.width + 80} height={lay.height + 20}>
          <defs>
            <marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
              <path d="M0,0 L10,5 L0,10 z" fill="#9aa4b2" />
            </marker>
          </defs>
          {edges}
          {cfg.blocks.map((b) => {
            const p = lay.pos.get(b.id)!;
            const special = b.id === cfg.entry || b.id === cfg.exit;
            const cls = special ? "io" : b.reachable ? "reach" : "dead";
            const shown = b.lines.slice(0, MAX_LINES);
            return (
              <g key={b.id}>
                <rect x={p.x} y={p.y} width={W} height={p.h} rx={special ? 16 : 6} className={`cfg-box ${cls}`} />
                <text x={p.x + 10} y={p.y + 18} className="cfg-title">{b.title}{b.line ? `  (line ${b.line})` : ""}</text>
                {shown.map((l, i) => (
                  <text key={i} x={p.x + 10} y={p.y + 36 + i * LINE_H} className="cfg-line">{l.length > 26 ? l.slice(0, 25) + "…" : l}</text>
                ))}
                {b.lines.length > MAX_LINES && <text x={p.x + 10} y={p.y + 36 + MAX_LINES * LINE_H} className="cfg-line">…</text>}
              </g>
            );
          })}
        </svg>
      </div>
    </div>
  );
}
