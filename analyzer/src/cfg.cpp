#include "cfg.h"
#include <functional>
#include <ostream>
#include <queue>
#include <set>
#include "consteval.h"

using std::string;

namespace {
struct Builder {
    Cfg g;
    const std::map<string, Macro>& macros;
    int cur = 0;
    int stmtCounter = 0;
    std::vector<std::pair<int, int>> loopStack;   // (break target, continue target)

    explicit Builder(const std::map<string, Macro>& m) : macros(m) {}

    int nb() {
        CfgBlock b;
        b.id = (int)g.blocks.size();
        b.title = "B" + std::to_string(b.id);
        g.blocks.push_back(b);
        return b.id;
    }
    void edge(int a, int b, const string& l = "") { g.blocks[a].succ.push_back({b, l}); }
    void add(int b, Node* n, const string& t) {
        stmtCounter++;
        g.blocks[b].stmts.push_back(n);
        g.blocks[b].lines.push_back(t);
        if (!g.blocks[b].line) g.blocks[b].line = n->line;
    }
    bool isTrue(Node* c) {
        if (!c || c->kind == NK::Empty) return true;
        long long v;
        return constEval(c, macros, v) && v != 0;
    }
    bool isFalse(Node* c) {
        long long v;
        return c && c->kind != NK::Empty && constEval(c, macros, v) && v == 0;
    }
    static bool noReturn(const Node* e) { return e && e->kind == NK::Call && (e->text == "exit" || e->text == "abort"); }

    void stmt(Node* n) {
        if (!n) return;
        switch (n->kind) {
            case NK::Block: case NK::DeclList: for (auto& k : n->kids) stmt(k.get()); break;
            case NK::VarDecl: add(cur, n, declStr(n)); break;
            case NK::Empty: break;
            case NK::ExprStmt:
                add(cur, n, exprStr(n->kid(0)));
                if (noReturn(n->kid(0))) { edge(cur, g.exit, "exit"); cur = nb(); }
                break;
            case NK::Return:
                add(cur, n, n->kid(0) ? "return " + exprStr(n->kid(0)) : "return");
                edge(cur, g.exit, "return");
                cur = nb();
                break;
            case NK::Break:
                add(cur, n, "break");
                if (!loopStack.empty()) edge(cur, loopStack.back().first, "break");
                cur = nb();
                break;
            case NK::Continue:
                add(cur, n, "continue");
                if (!loopStack.empty() && loopStack.back().second >= 0) edge(cur, loopStack.back().second, "continue");
                cur = nb();
                break;
            case NK::If: {
                add(cur, n, "if (" + exprStr(n->kid(0)) + ")");
                int c = cur, t = nb(), j = nb();
                bool hasElse = n->kid(2) != nullptr;
                int e = hasElse ? nb() : j;
                edge(c, t, "true");
                edge(c, e, "false");
                cur = t; stmt(n->kid(1)); edge(cur, j);
                if (hasElse) { cur = e; stmt(n->kid(2)); edge(cur, j); }
                cur = j;
                break;
            }
            case NK::Switch: {
                add(cur, n, "switch (" + exprStr(n->kid(0)) + ")");
                int disp = cur, after = nb();
                int contTarget = loopStack.empty() ? -1 : loopStack.back().second;
                loopStack.push_back({after, contTarget});
                cur = nb();   // statements before the first label can never run
                bool hasDefault = false, first = true;
                int sectionStart = stmtCounter;
                for (auto& k : n->kid(1)->kids) {
                    if (k->kind == NK::Case || k->kind == NK::Default) {
                        int cb = nb();
                        if (!first) {
                            edge(cur, cb, "fall-through");
                            if (stmtCounter > sectionStart) g.fallthroughs.push_back({cur, k.get()});
                        }
                        bool isCase = k->kind == NK::Case;
                        edge(disp, cb, isCase ? "case " + exprStr(k->kid(0)) : "default");
                        g.blocks[cb].lines.push_back(isCase ? "case " + exprStr(k->kid(0)) + ":" : "default:");
                        if (!isCase) hasDefault = true;
                        cur = cb;
                        sectionStart = stmtCounter;
                        first = false;
                    } else stmt(k.get());
                }
                edge(cur, after);
                if (!hasDefault) edge(disp, after, "no match");
                loopStack.pop_back();
                cur = after;
                break;
            }
            case NK::While: {
                int h = nb();
                edge(cur, h);
                add(h, n, "while (" + exprStr(n->kid(0)) + ")");
                int body = nb(), after = nb();
                bool ct = isTrue(n->kid(0)), cf = isFalse(n->kid(0));
                if (!cf) edge(h, body, "true");
                if (!ct) edge(h, after, "false");
                loopStack.push_back({after, h});
                cur = body; stmt(n->kid(1)); edge(cur, h);
                loopStack.pop_back();
                g.loops.push_back({n, h, after, (int)g.blocks.size(), ct});
                cur = after;
                break;
            }
            case NK::DoWhile: {
                int body = nb(), cond = nb(), after = nb();
                edge(cur, body);
                bool ct = isTrue(n->kid(1)), cf = isFalse(n->kid(1));
                loopStack.push_back({after, cond});
                cur = body; stmt(n->kid(0)); edge(cur, cond);
                loopStack.pop_back();
                add(cond, n, "while (" + exprStr(n->kid(1)) + ")");
                if (!cf) edge(cond, body, "true");
                if (!ct) edge(cond, after, "false");
                g.loops.push_back({n, body, after, (int)g.blocks.size(), ct});
                cur = after;
                break;
            }
            case NK::For: {
                stmt(n->kid(0));
                Node* c = n->kid(1);
                bool noCond = c->kind == NK::Empty;
                int h = nb(), body = nb(), inc = nb(), after = nb();
                edge(cur, h);
                add(h, n, "for (" + (noCond ? string("") : exprStr(c)) + ")");
                bool ct = isTrue(c), cf = isFalse(c);
                if (!cf) edge(h, body, "true");
                if (!ct) edge(h, after, "false");
                loopStack.push_back({after, inc});
                cur = body; stmt(n->kid(3)); edge(cur, inc);
                loopStack.pop_back();
                if (n->kid(2)->kind != NK::Empty) add(inc, n->kid(2), exprStr(n->kid(2)));
                edge(inc, h);
                g.loops.push_back({n, h, after, (int)g.blocks.size(), ct});
                cur = after;
                break;
            }
            default: break;
        }
    }
};
}  // namespace

Cfg buildCfg(Node* fn, const std::map<string, Macro>& macros) {
    Builder b(macros);
    b.g.function = fn->text;
    b.g.line = fn->line;
    int en = b.nb(), ex = b.nb();
    b.g.blocks[en].title = "ENTRY";
    b.g.blocks[en].lines.push_back("function " + fn->text);
    b.g.blocks[ex].title = "EXIT";
    b.g.entry = en;
    b.g.exit = ex;
    int first = b.nb();
    b.edge(en, first);
    b.cur = first;
    b.stmt(fn->kids.back().get());
    b.edge(b.cur, ex, "fall-through");
    b.g.fallBlock = b.cur;
    return b.g;
}

void analyzeCfg(Cfg& g, Node* fn, Diagnostics& d) {
    size_t n = g.blocks.size();
    std::queue<int> q;
    g.blocks[g.entry].reachable = true;
    q.push(g.entry);
    while (!q.empty()) {
        int b = q.front(); q.pop();
        for (auto& e : g.blocks[b].succ)
            if (!g.blocks[e.to].reachable) { g.blocks[e.to].reachable = true; q.push(e.to); }
    }

    // Missing return: the end of a non-void function can be reached.
    bool nonVoid = !(fn->type == "void" && fn->ptr == 0);
    Node* body = fn->kids.back().get();
    if (nonVoid && fn->text != "main" && g.fallBlock >= 0 && g.blocks[g.fallBlock].reachable)
        d.add("missing-return", "Missing return", Severity::Warning, body->endLine, body->endCol,
              "Function '" + fn->text + "' returns '" + typeStr(fn->type, fn->ptr) + "' but some path reaches the end without a return statement.",
              "Add 'return <value>;' at the end of the function and in every branch.", "High", "static");

    // Unreachable code: blocks with statements that no path from ENTRY can reach.
    std::vector<std::vector<int>> pred(n);
    for (auto& b : g.blocks) for (auto& e : b.succ) pred[e.to].push_back(b.id);
    std::function<bool(int, std::set<int>&)> hasAncestor = [&](int b, std::set<int>& seen) {
        for (int p : pred[b]) {
            if (g.blocks[p].reachable || seen.count(p)) continue;
            seen.insert(p);
            if (!g.blocks[p].stmts.empty()) return true;
            if (hasAncestor(p, seen)) return true;
        }
        return false;
    };
    for (auto& b : g.blocks) {
        if (b.reachable || b.stmts.empty()) continue;
        std::set<int> seen;
        seen.insert(b.id);
        if (hasAncestor(b.id, seen)) continue;   // only report the first statement of a dead region
        Node* s = b.stmts[0];
        d.add("unreachable-code", "Unreachable code", Severity::Warning, s->line, s->col,
              "This statement can never be executed: no path through the function reaches it.",
              "Remove the dead code, or fix the earlier return/break/continue/infinite loop that blocks it.", "High", "static");
    }

    // Switch sections that run into the next label without break/return.
    for (auto& ft : g.fallthroughs)
        if (g.blocks[ft.first].reachable)
            d.add("implicit-fallthrough", "Implicit fallthrough", Severity::Warning, ft.second->line, ft.second->col,
                  "The previous case does not end with break or return, so execution continues into this case.",
                  "Add 'break;' at the end of the previous case. If falling through is intended, add a comment saying so.", "Medium", "static");

    // Infinite loops: the condition is always true and nothing inside can leave the loop.
    for (auto& li : g.loops) {
        if (!li.constTrue || !g.blocks[li.header].reachable) continue;
        std::set<int> S;
        for (int i = li.header; i < li.last; i++) if (i != li.after) S.insert(i);
        bool leaves = false;
        for (int i : S) {
            if (!g.blocks[i].reachable) continue;
            for (auto& e : g.blocks[i].succ) if (!S.count(e.to)) leaves = true;
        }
        if (!leaves)
            d.add("infinite-loop", "Infinite loop", Severity::Warning, li.node->line, li.node->col,
                  "The loop condition is always true and there is no break or return inside the loop, so it never ends.",
                  "Add a break/return for the exit case, or make the condition depend on a variable that changes.", "High", "static");
    }
}

void cfgToJson(const Cfg& g, std::ostream& os) {
    std::set<int> keep;
    for (auto& b : g.blocks) if (b.reachable || !b.stmts.empty() || b.id == g.entry || b.id == g.exit) keep.insert(b.id);
    os << "{\"function\":\"" << jsonEscape(g.function) << "\",\"line\":" << g.line << ",\"entry\":" << g.entry
       << ",\"exit\":" << g.exit << ",\"blocks\":[";
    bool first = true;
    for (auto& b : g.blocks) {
        if (!keep.count(b.id)) continue;
        if (!first) os << ",";
        first = false;
        os << "{\"id\":" << b.id << ",\"title\":\"" << b.title << "\",\"reachable\":" << (b.reachable ? "true" : "false")
           << ",\"line\":" << b.line << ",\"lines\":[";
        for (size_t i = 0; i < b.lines.size(); i++) os << (i ? "," : "") << "\"" << jsonEscape(b.lines[i]) << "\"";
        os << "],\"succ\":[";
        bool f2 = true;
        for (auto& e : b.succ) {
            if (!keep.count(e.to)) continue;
            if (!f2) os << ",";
            f2 = false;
            os << "{\"to\":" << e.to << ",\"label\":\"" << jsonEscape(e.label) << "\"}";
        }
        os << "]}";
    }
    os << "]}";
}
