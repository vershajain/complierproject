#include "flow.h"
#include "consteval.h"

using std::set;
using std::string;

Flow::State Flow::join(const State& a, const State& b) {
    if (a.dead) return b;
    if (b.dead) return a;
    State r;
    for (int x : a.init) if (b.init.count(x)) r.init.insert(x);
    for (int x : a.nz) if (b.nz.count(x)) r.nz.insert(x);
    for (int x : a.freed) if (b.freed.count(x)) r.freed.insert(x);
    r.freedMay = a.freedMay;
    r.freedMay.insert(b.freedMay.begin(), b.freedMay.end());
    r.maybeNull = a.maybeNull;
    r.maybeNull.insert(b.maybeNull.begin(), b.maybeNull.end());
    for (auto& kv : a.consts) {
        auto it = b.consts.find(kv.first);
        if (it != b.consts.end() && it->second == kv.second) r.consts.insert(kv);
    }
    return r;
}

void Flow::rep(const string& rule, const string& title, Severity sev, const Node* at, const string& msg, const string& sug, const string& conf) {
    diags.add(rule, title, sev, at->line, at->col, msg, sug, conf, "static");
}

bool Flow::constS(const Node* n, const State& s, long long& v) {
    return constEval(n, macros, v, [&](const Node* id, long long& o) {
        if (id->sym < 0) return false;
        auto it = s.consts.find(id->sym);
        if (it == s.consts.end()) return false;
        o = it->second;
        return true;
    });
}

bool Flow::isFloaty(const Node* n) {
    if (!n) return false;
    if (n->kind == NK::FloatLit) return true;
    if (n->kind == NK::Ident && n->sym >= 0) {
        const Symbol& s = st.all[n->sym];
        return s.ptr == 0 && (s.type.find("float") != string::npos || s.type.find("double") != string::npos);
    }
    if (n->kind == NK::Cast && n->ptr == 0 && (n->type.find("float") != string::npos || n->type.find("double") != string::npos)) return true;
    for (auto& k : n->kids) if (isFloaty(k.get())) return true;
    return false;
}

void Flow::collectAssigned(const Node* n, set<int>& out) {
    if (!n) return;
    if (n->kind == NK::Assign) {
        const Node* l = n->kid(0);
        if (l->kind == NK::Ident && l->sym >= 0) out.insert(l->sym);
    } else if ((n->kind == NK::Unary && (n->text == "++" || n->text == "--" || n->text == "&")) || n->kind == NK::Postfix) {
        const Node* k = n->kid(0);
        if (k->kind == NK::Ident && k->sym >= 0) out.insert(k->sym);
    } else if (n->kind == NK::VarDecl && n->sym >= 0) out.insert(n->sym);
    for (auto& k : n->kids) collectAssigned(k.get(), out);
}
void Flow::collectIdents(const Node* n, set<int>& out) {
    if (!n) return;
    if (n->kind == NK::Ident && n->sym >= 0) out.insert(n->sym);
    for (auto& k : n->kids) collectIdents(k.get(), out);
}
bool Flow::hasKind(const Node* n, NK k) {
    if (!n) return false;
    if (n->kind == k) return true;
    for (auto& c : n->kids) if (hasKind(c.get(), k)) return true;
    return false;
}
bool Flow::hasCall(const Node* n) { return hasKind(n, NK::Call); }
bool Flow::hasExitCall(const Node* n) {
    if (!n) return false;
    if (n->kind == NK::Call && (n->text == "exit" || n->text == "abort")) return true;
    for (auto& c : n->kids) if (hasExitCall(c.get())) return true;
    return false;
}

void Flow::kill(State& s, const set<int>& vars) {
    for (int v : vars) { s.consts.erase(v); s.nz.erase(v); }
}

void Flow::run(Node* prog) {
    for (auto& k : prog->kids) {
        if (k->kind != NK::Function || !k->hasBody) continue;
        everAssigned.clear();
        reported.clear();
        State s;
        for (auto& p : k->kids) if (p->kind == NK::Param && p->sym >= 0) s.init.insert(p->sym);
        allocSites.clear();
        freeLine.clear();
        stmt(k->kids.back().get(), s);
        leakCheck(k.get());
    }
}

void Flow::readIdent(Node* n, State& s) {
    if (n->sym < 0 || s.dead) return;
    const Symbol& sy = st.all[n->sym];
    if (s.freedMay.count(n->sym)) {
        bool sure = s.freed.count(n->sym) > 0;
        rep("use-after-free", "Use after free", Severity::Error, n,
            "'" + sy.name + "' is used after free(" + sy.name + ") on line " + std::to_string(freeLine[n->sym]) + (sure ? "." : " (on at least one path).") + " The memory no longer belongs to the program, so reading or writing it is undefined behavior.",
            "Do not use the pointer after freeing it. Set it to NULL right after free(), and move the free() to the end of its last use.", sure ? "High" : "Medium");
    }
    if (sy.isFunc || sy.isArray || sy.isGlobal || sy.isParam) return;
    if (!s.init.count(n->sym) && !reported.count(n->sym)) {
        bool never = !everAssigned.count(n->sym);
        rep("uninitialized-variable", "Uninitialized variable", Severity::Warning, n,
            "Variable '" + sy.name + "' is used before it has been given a value" + (never ? "." : " on at least one path."),
            "Initialize it when you declare it, e.g. '" + typeStr(sy.type, sy.ptr) + " " + sy.name + " = 0;'.",
            never ? "High" : "Medium");
        reported.insert(n->sym);
    }
    s.init.insert(n->sym);
}

void Flow::setValue(int sym, Node* rhs, State& s) {
    if (sym < 0) return;
    s.init.insert(sym);
    s.freed.erase(sym);
    s.freedMay.erase(sym);
    everAssigned.insert(sym);
    {
        const Node* core = rhs;
        while (core->kind == NK::Cast && core->kid(0)) core = core->kid(0);   // (char *)malloc(10)
        if (core->kind == NK::Call && (core->text == "malloc" || core->text == "calloc" || core->text == "realloc")) {
            const Symbol& ps = st.all[sym];
            if (ps.ptr > 0 && !ps.isGlobal && !ps.isParam && !allocSites.count(sym)) allocSites[sym] = core;
        }
    }
    long long v;
    if (constS(rhs, s, v)) {
        s.consts[sym] = v;
        if (v != 0) s.nz.insert(sym); else s.nz.erase(sym);
    } else { s.consts.erase(sym); s.nz.erase(sym); }
    bool mayNull = false;
    const Node* src = rhs;
    while (src->kind == NK::Cast && src->kid(0)) src = src->kid(0);
    if (src->kind == NK::Call && (src->text == "malloc" || src->text == "calloc" || src->text == "realloc" || src->text == "fopen")) mayNull = true;
    if (rhs->kind == NK::Ident && rhs->sym >= 0 && s.maybeNull.count(rhs->sym) && !s.nz.count(rhs->sym)) mayNull = true;
    if (mayNull) s.maybeNull.insert(sym); else s.maybeNull.erase(sym);
}

void Flow::invalidate(Node* k, State& s) {
    if (k && k->kind == NK::Ident && k->sym >= 0) { s.consts.erase(k->sym); s.nz.erase(k->sym); }
}

void Flow::derefCheck(Node* base, State& s) {
    if (s.dead || !base || base->kind != NK::Ident || base->sym < 0) return;
    const Symbol& sy = st.all[base->sym];
    if (sy.ptr == 0 || sy.isArray) return;
    if (!sy.isGlobal && !sy.isParam && !s.init.count(base->sym) && !reported.count(base->sym)) {
        rep("uninitialized-pointer", "Uninitialized pointer", Severity::Error, base,
            "Pointer '" + sy.name + "' is dereferenced before it points to anything. It holds a random address, so this read/write hits arbitrary memory and usually crashes.",
            "Make it point to a variable (p = &x;) or allocate memory (p = malloc(...)) before using it.", "High");
        reported.insert(base->sym);
        s.init.insert(base->sym);
        return;
    }
    auto c = s.consts.find(base->sym);
    if (c != s.consts.end() && c->second == 0) {
        rep("null-dereference", "Null pointer dereference", Severity::Error, base,
            "Pointer '" + sy.name + "' is NULL when it is dereferenced here.",
            "Assign it a valid address (or allocate memory) before using it, and check for NULL.", "High");
        s.consts.erase(base->sym);
        s.nz.insert(base->sym);
        return;
    }
    if (s.maybeNull.count(base->sym) && !s.nz.count(base->sym)) {
        rep("potential-null-dereference", "Possible null pointer dereference", Severity::Warning, base,
            "Pointer '" + sy.name + "' comes from an allocation/open call that can return NULL, but it is not checked before use.",
            "Add 'if (" + sy.name + " == NULL) { /* handle error */ }' before dereferencing it.", "Medium");
        s.nz.insert(base->sym);
    }
}

// a[7] on "int a[5]": index must be 0..4. Only checked when the index is a known constant.
void Flow::boundsCheck(Node* idx, State& s) {
    if (s.dead || !idx || idx->kind != NK::Index) return;
    Node* base = idx->kid(0);
    if (!base || base->kind != NK::Ident || base->sym < 0) return;
    const Symbol& sy = st.all[base->sym];
    if (!sy.isArray || sy.isParam || sy.arrLen < 0) return;
    long long v;
    if (!constS(idx->kid(1), s, v)) return;
    if (v >= 0 && v < sy.arrLen) return;
    string n = std::to_string(sy.arrLen);
    rep("array-out-of-bounds", "Array index out of bounds", Severity::Error, idx,
        "'" + sy.name + "' has " + n + " element" + (sy.arrLen == 1 ? "" : "s") + " (valid indexes are 0 to " + std::to_string(sy.arrLen - 1) +
        "), but index " + std::to_string(v) + " is used here. This is undefined behavior and can corrupt memory or crash.",
        "Use an index between 0 and " + std::to_string(sy.arrLen - 1) + ", or make the array larger.", "High");
}

void Flow::checkDivisorNode(Node* d, Node* at, State& s) {
    if (s.dead || !d) return;
    long long v;
    bool fl = isFloaty(d) || isFloaty(at->kid(0));
    if (d->kind == NK::FloatLit && strtod(d->text.c_str(), nullptr) == 0.0) {
        rep("division-by-zero", "Division by zero", Severity::Warning, at, "Division by the floating-point zero '" + d->text + "' gives inf or nan instead of a usable number.", "Make sure the divisor cannot be 0.", "High");
        return;
    }
    if (constS(d, s, v)) {
        if (v == 0) {
            string what = d->kind == NK::Ident ? "'" + d->text + "' is always 0 at this point" : "the divisor evaluates to 0";
            if (fl)
                rep("division-by-zero", "Division by zero", Severity::Warning, at, "Floating-point division by zero: " + what + ", the result will be inf or nan.", "Make sure the divisor cannot be 0.", "Medium");
            else
                rep("division-by-zero", "Division by zero", Severity::Error, at, "Division by zero: " + what + ". This is undefined behavior and usually crashes the program.",
                    "Use a non-zero divisor, or check the divisor before dividing.", "High");
        }
        // Divisor is a known non-zero constant. A zero numerator is still worth a note.
        if (!fl) {
            long long nv;
            if (constS(at->kid(0), s, nv) && nv == 0)
                rep("zero-numerator", "Result is always 0", Severity::Info, at,
                    "The numerator is 0, so this expression is always 0.", "Replace it with 0, or check that the numerator is really meant to be 0.", "High");
        }
        return;
    }
    if (!fl) {
        long long nv;
        if (constS(at->kid(0), s, nv) && nv == 0) {
            string num = exprStr(at->kid(0)), dv = exprStr(d);
            string op = at->kind == NK::Binary ? at->text : at->text.substr(0, 1);
            rep("zero-numerator", "Result is always 0", Severity::Info, at,
                "'" + num + " " + op + " " + dv + "' is always 0 when '" + dv + "' is not 0, so '" + dv + "' has no effect on the result. " +
                "If '" + dv + "' is 0, the division is invalid (undefined behavior).",
                "Replace the expression with 0, or remove the division. If '" + dv + "' can be 0, check it first.", "High");
        }
    }
    if (d->kind == NK::Ident && d->sym >= 0 && !fl) {
        const Symbol& sy = st.all[d->sym];
        if ((sy.isParam || sy.addrTaken) && !s.nz.count(d->sym)) {
            rep("potential-division-by-zero", "Possible division by zero", Severity::Warning, at,
                "The divisor '" + sy.name + "' could be 0 because its value comes from outside this code and is never checked.",
                "Guard the division: 'if (" + sy.name + " != 0) { ... }'.", "Medium");
            s.nz.insert(d->sym);
        }
    }
}

bool Flow::splitCmp(const Node* b, const State& s, int& sym, string& op, long long& c) {
    const Node* l = b->kid(0);
    const Node* r = b->kid(1);
    long long v;
    auto flip = [](const string& o) {
        if (o == "<") return string(">");
        if (o == ">") return string("<");
        if (o == "<=") return string(">=");
        if (o == ">=") return string("<=");
        return o;
    };
    if (l->kind == NK::Ident && l->sym >= 0 && constS(r, s, v)) { sym = l->sym; op = b->text; c = v; return true; }
    if (r->kind == NK::Ident && r->sym >= 0 && constS(l, s, v)) { sym = r->sym; op = flip(b->text); c = v; return true; }
    return false;
}

// Learns from a condition: inside `if (b != 0) {...}` we know b is non-zero, and so on.
void Flow::applyFacts(Node* c, bool truth, State& s) {
    if (s.dead || !c) return;
    auto markNz = [&](int sym) { s.nz.insert(sym); s.maybeNull.erase(sym); };
    auto setConst = [&](int sym, long long v) {
        s.consts[sym] = v;
        if (v != 0) markNz(sym); else s.nz.erase(sym);
    };
    if (c->kind == NK::Ident && c->sym >= 0) {
        if (truth) markNz(c->sym); else setConst(c->sym, 0);
        return;
    }
    if (c->kind == NK::Unary && c->text == "!") { applyFacts(c->kid(0), !truth, s); return; }
    if (c->kind != NK::Binary) return;
    const string& o = c->text;
    if (o == "&&") { if (truth) { applyFacts(c->kid(0), true, s); applyFacts(c->kid(1), true, s); } return; }
    if (o == "||") { if (!truth) { applyFacts(c->kid(0), false, s); applyFacts(c->kid(1), false, s); } return; }
    int sym; string op; long long k;
    if (!splitCmp(c, s, sym, op, k)) return;
    if (op == "==") { if (truth) setConst(sym, k); else if (k == 0) markNz(sym); }
    else if (op == "!=") { if (truth) { if (k == 0) markNz(sym); } else setConst(sym, k); }
    else if (truth && ((op == ">" && k >= 0) || (op == "<" && k <= 0) || (op == ">=" && k >= 1) || (op == "<=" && k <= -1))) markNz(sym);
}

void Flow::assign(Node* n, State& s) {
    Node* l = n->kid(0);
    Node* r = n->kid(1);
    const string& op = n->text;
    bool div = op == "/=" || op == "%=";
    if (l->kind == NK::Ident) {
        if (op == "=" && r->kind == NK::Ident && r->sym >= 0 && r->sym == l->sym && !s.dead)
            rep("self-assignment", "Self-assignment", Severity::Warning, n,
                "'" + l->text + " = " + l->text + "' assigns the variable to itself, which has no effect.",
                "Assign the intended value, e.g. a different variable or an expression.", "High");
        if (op == "=") { expr(r, s); setValue(l->sym, r, s); }
        else { expr(l, s); expr(r, s); if (div) checkDivisorNode(r, n, s); invalidate(l, s); }
    } else {
        expr(r, s);
        if (l->kind == NK::Index) { derefCheck(l->kid(0), s); expr(l->kid(0), s); expr(l->kid(1), s); boundsCheck(l, s); }
        else if (l->kind == NK::Unary) { derefCheck(l->kid(0), s); expr(l->kid(0), s); }
        if (div) checkDivisorNode(r, n, s);
    }
}

void Flow::expr(Node* n, State& s) {
    if (!n) return;
    switch (n->kind) {
        case NK::Ident: readIdent(n, s); break;
        case NK::Assign: assign(n, s); break;
        case NK::Unary: {
            const string& op = n->text;
            Node* k = n->kid(0);
            if (op == "&") {
                if (k->kind == NK::Ident && k->sym >= 0) {
                    s.init.insert(k->sym); s.consts.erase(k->sym); s.nz.erase(k->sym); everAssigned.insert(k->sym);
                } else if (k->kind == NK::Index) { expr(k->kid(0), s); expr(k->kid(1), s); }   // &a[N] is legal
                else expr(k, s);
            } else if (op == "*") { derefCheck(k, s); expr(k, s); }
            else if (op == "++" || op == "--") { expr(k, s); invalidate(k, s); }
            else expr(k, s);
            break;
        }
        case NK::Postfix: expr(n->kid(0), s); invalidate(n->kid(0), s); break;
        case NK::Binary: {
            const string& op = n->text;
            if (op == "&&" || op == "||") {
                expr(n->kid(0), s);
                State c = s;
                applyFacts(n->kid(0), op == "&&", c);
                expr(n->kid(1), c);   // right side may not run: its effects are discarded
            } else {
                expr(n->kid(0), s);
                expr(n->kid(1), s);
                if (op == "/" || op == "%") checkDivisorNode(n->kid(1), n, s);
            }
            break;
        }
        case NK::Ternary: {
            expr(n->kid(0), s);
            State t = s, f = s;
            applyFacts(n->kid(0), true, t);
            applyFacts(n->kid(0), false, f);
            expr(n->kid(1), t);
            expr(n->kid(2), f);
            s = join(t, f);
            break;
        }
        case NK::Call:
            if (n->text == "free" && n->kids.size() == 1 && n->kid(0)->kind == NK::Ident && n->kid(0)->sym >= 0) {
                Node* a = n->kid(0);
                const Symbol& sy = st.all[a->sym];
                if (!s.dead && s.freedMay.count(a->sym))
                    rep("double-free", "Double free", Severity::Error, n,
                        "'" + sy.name + "' is freed again here, but it was already freed on line " + std::to_string(freeLine[a->sym]) + (s.freed.count(a->sym) ? "." : " (on at least one path)."),
                        "Free each allocation exactly once. Set the pointer to NULL after free() (free(NULL) is harmless).", s.freed.count(a->sym) ? "High" : "Medium");
                else if (!s.dead && sy.ptr > 0 && s.consts.count(a->sym) == 0 && !sy.isGlobal && !s.init.count(a->sym) && !sy.isParam) readIdent(a, s);
                s.freed.insert(a->sym);
                s.freedMay.insert(a->sym);
                freeLine[a->sym] = n->line;
                break;
            }
            for (auto& a : n->kids) expr(a.get(), s);
            if (n->text == "exit" || n->text == "abort") s.dead = true;
            break;
        case NK::Index: derefCheck(n->kid(0), s); expr(n->kid(0), s); expr(n->kid(1), s); boundsCheck(n, s); break;
        case NK::Cast: expr(n->kid(0), s); break;
        case NK::InitList: for (auto& k : n->kids) expr(k.get(), s); break;
        default: break;
    }
}

void Flow::checkNoProgress(Node* loop, Node* cond, Node* body, Node* incr) {
    if (!cond || cond->kind == NK::Empty) return;
    long long v;
    if (constEval(cond, macros, v)) return;   // constant conditions are handled by the CFG
    set<int> vars;
    collectIdents(cond, vars);
    if (vars.empty() || hasCall(cond)) return;
    set<int> assigned;
    collectAssigned(cond, assigned);
    collectAssigned(body, assigned);
    if (incr && incr->kind != NK::Empty) collectAssigned(incr, assigned);
    for (int id : vars) {
        const Symbol& sy = st.all[id];
        if (assigned.count(id) || sy.isGlobal || sy.addrTaken || sy.ptr > 0 || sy.isArray) return;
    }
    if (hasKind(body, NK::Break) || hasKind(body, NK::Return) || hasExitCall(body)) return;
    string names;
    for (int id : vars) names += (names.empty() ? "" : ", ") + string("'") + st.all[id].name + "'";
    rep("infinite-loop", "Possible infinite loop", Severity::Warning, loop,
        "The loop condition only depends on " + names + ", but nothing inside the loop changes it, so the loop may never end.",
        "Update the variable inside the loop (e.g. i++), or add a break/return.", "Medium");
}

void Flow::stmt(Node* n, State& s) {
    if (!n) return;
    switch (n->kind) {
        case NK::Block: case NK::DeclList: for (auto& k : n->kids) stmt(k.get(), s); break;
        case NK::VarDecl: {
            Node* init = n->kid(0);
            if (n->isArray) { s.init.insert(n->sym); if (init) expr(init, s); break; }
            if (init) { expr(init, s); setValue(n->sym, init, s); }
            else { s.init.erase(n->sym); s.consts.erase(n->sym); s.nz.erase(n->sym); s.maybeNull.erase(n->sym); }
            break;
        }
        case NK::ExprStmt:
            expr(n->kid(0), s);
            if (!s.dead && n->kid(0)->kind == NK::Call && (n->kid(0)->text == "malloc" || n->kid(0)->text == "calloc"))
                rep("memory-leak", "Allocation result ignored", Severity::Warning, n->kid(0), n->kid(0)->text + "() returns the new memory, but the result is thrown away, so it can never be used or freed.", "Store the result in a pointer: int *p = " + n->kid(0)->text + "(...);", "High");
            break;
        case NK::Return: expr(n->kid(0), s); s.dead = true; break;
        case NK::Break:
            if (!s.dead && !brk.empty()) brk.back() = join(brk.back(), s);
            s.dead = true;
            break;
        case NK::Continue: s.dead = true; break;
        case NK::Switch: {
            expr(n->kid(0), s);
            Node* body = n->kid(1);
            set<int> as;
            collectAssigned(body, as);
            State entry = s;
            kill(entry, as);
            State cur;
            cur.dead = true;                 // nothing falls into the first label
            bool hasDefault = false;
            State none;
            none.dead = true;
            brk.push_back(none);
            for (auto& k : body->kids) {
                if (k->kind == NK::Case || k->kind == NK::Default) {
                    if (k->kind == NK::Default) hasDefault = true;
                    cur = join(cur, entry);  // reached by a jump from the switch or by falling through
                } else stmt(k.get(), cur);
            }
            State out = join(brk.back(), cur);
            brk.pop_back();
            if (!hasDefault) out = join(out, entry);
            s = out;
            break;
        }
        case NK::If: {
            Node* c = n->kid(0);
            expr(c, s);
            long long cv;
            if (!s.dead && constS(c, s, cv))
                rep("constant-condition", "Constant condition", Severity::Info, c,
                    "'" + exprStr(c) + "' is always " + (cv ? "true" : "false") + " here (every value it uses is known), so " + (cv ? "the else branch" : "the code inside the if") + " can never run.",
                    "Remove the condition or replace it with the real check you intended.", "Medium");
            State t = s, f = s;
            applyFacts(c, true, t);
            applyFacts(c, false, f);
            stmt(n->kid(1), t);
            if (n->kid(2)) stmt(n->kid(2), f);
            s = join(t, f);
            break;
        }
        case NK::While: {
            Node* c = n->kid(0);
            set<int> as;
            collectAssigned(c, as);
            collectAssigned(n->kid(1), as);
            kill(s, as);
            checkNoProgress(n, c, n->kid(1), nullptr);
            expr(c, s);
            { long long z; if (!s.dead && constS(c, s, z) && z == 0) rep("constant-condition", "Loop never runs", Severity::Warning, c, "The loop condition '" + exprStr(c) + "' is already false before the first iteration, so the loop body never runs.", "Check the starting value and the comparison.", "High"); }
            State b = s;
            applyFacts(c, true, b);
            brk.push_back(State());
            stmt(n->kid(1), b);
            brk.pop_back();
            applyFacts(c, false, s);
            long long cv;
            if (constEval(c, macros, cv) && cv != 0 && !hasKind(n->kid(1), NK::Break)) s.dead = true;
            break;
        }
        case NK::DoWhile: {
            set<int> as;
            collectAssigned(n->kid(0), as);
            collectAssigned(n->kid(1), as);
            kill(s, as);
            checkNoProgress(n, n->kid(1), n->kid(0), nullptr);
            State b = s;
            brk.push_back(State());
            stmt(n->kid(0), b);
            brk.pop_back();
            if (b.dead) b = s;
            expr(n->kid(1), b);
            applyFacts(n->kid(1), false, b);
            s = b;
            break;
        }
        case NK::For: {
            Node* c = n->kid(1);
            bool hasCond = c->kind != NK::Empty;
            stmt(n->kid(0), s);
            checkLoopDirection(n, s);
            set<int> as;
            if (hasCond) collectAssigned(c, as);
            collectAssigned(n->kid(2), as);
            collectAssigned(n->kid(3), as);
            kill(s, as);
            checkNoProgress(n, c, n->kid(3), n->kid(2));
            if (hasCond) expr(c, s);
            { long long z; if (hasCond && !s.dead && constS(c, s, z) && z == 0) rep("constant-condition", "Loop never runs", Severity::Warning, c, "The loop condition '" + exprStr(c) + "' is already false before the first iteration, so the loop body never runs.", "Check the starting value and the comparison (< versus >, <= versus <).", "High"); }
            State b = s;
            if (hasCond) applyFacts(c, true, b);
            brk.push_back(State());
            stmt(n->kid(3), b);
            brk.pop_back();
            if (!b.dead && n->kid(2)->kind != NK::Empty) expr(n->kid(2), b);
            if (hasCond) applyFacts(c, false, s);
            long long cv;
            bool forever = !hasCond || (constEval(c, macros, cv) && cv != 0);
            if (forever && !hasKind(n->kid(3), NK::Break)) s.dead = true;
            break;
        }
        default: break;
    }
}

// A for-loop whose counter moves the wrong way (or runs past the end of an array)
static void indexUses(Node* n, int sym, std::vector<Node*>& out, const SymbolTable& st) {
    if (!n) return;
    if (n->kind == NK::Index && n->kid(0)->kind == NK::Ident && n->kid(0)->sym >= 0 && n->kid(1)->kind == NK::Ident && n->kid(1)->sym == sym &&
        st.all[n->kid(0)->sym].isArray && !st.all[n->kid(0)->sym].isParam && st.all[n->kid(0)->sym].arrLen >= 0)
        out.push_back(n);
    for (auto& k : n->kids) indexUses(k.get(), sym, out, st);
}

void Flow::checkLoopDirection(Node* loop, State& s) {
    Node* c = loop->kid(1);
    Node* inc = loop->kid(2);
    if (s.dead || c->kind != NK::Binary || inc->kind == NK::Empty) return;
    int sym;
    string op;
    long long cv;
    if (!splitCmp(c, s, sym, op, cv)) return;
    if (op != "<" && op != "<=" && op != ">" && op != ">=") return;
    int dir = 0;
    bool simple = inc->kind == NK::Postfix || (inc->kind == NK::Unary && (inc->text == "++" || inc->text == "--"));
    if (simple && inc->kid(0)->kind == NK::Ident && inc->kid(0)->sym == sym) dir = inc->text == "++" ? 1 : -1;
    else if (inc->kind == NK::Assign && inc->kid(0)->kind == NK::Ident && inc->kid(0)->sym == sym) {
        long long k;
        if (inc->text == "+=" && constS(inc->kid(1), s, k) && k != 0) dir = k > 0 ? 1 : -1;
        else if (inc->text == "-=" && constS(inc->kid(1), s, k) && k != 0) dir = k > 0 ? -1 : 1;
    }
    if (!dir) return;
    const string name = st.all[sym].name;
    bool up = op == "<" || op == "<=";
    auto it = s.consts.find(sym);
    if (it != s.consts.end()) {
        long long v0 = it->second;
        bool startsTrue = op == "<" ? v0 < cv : op == "<=" ? v0 <= cv : op == ">" ? v0 > cv : v0 >= cv;
        if (startsTrue && ((up && dir < 0) || (!up && dir > 0)))
            rep("infinite-loop", "Loop counter moves the wrong way", Severity::Warning, loop,
                "The condition '" + exprStr(c) + "' keeps the loop running while '" + name + "' " + (up ? "is small" : "is large") + ", but '" + exprStr(inc) + "' makes it " + (dir < 0 ? "smaller" : "larger") +
                    " every time, so the condition never becomes false.",
                string("Use ") + (up ? "'" + name + "++'" : "'" + name + "--'") + " in the loop header, or reverse the comparison.", "Medium");
    }
    if (dir > 0 && up) {
        long long maxIdx = op == "<=" ? cv : cv - 1;
        std::vector<Node*> uses;
        indexUses(loop->kid(3), sym, uses, st);
        for (Node* u : uses) {
            const Symbol& a = st.all[u->kid(0)->sym];
            if (maxIdx >= a.arrLen)
                rep("array-out-of-bounds", "Loop runs past the end of the array", Severity::Warning, u,
                    "The loop lets '" + name + "' reach " + std::to_string(maxIdx) + " (condition '" + exprStr(c) + "'), but '" + a.name + "' only has " + std::to_string(a.arrLen) + " elements (valid indexes 0 to " + std::to_string(a.arrLen - 1) + "), so " + a.name + "[" + name + "] goes out of bounds on the last iteration.",
                    "Use '" + name + " < " + std::to_string(a.arrLen) + "' instead of '" + exprStr(c) + "'.", "High");
        }
    }
}

// Memory from malloc that is never freed, returned or handed to another function
void Flow::scanEscapes(Node* n, Node* parent, const std::map<int, const Node*>& sites, std::set<int>& freed, std::set<int>& escaped) {
    if (!n) return;
    static const std::set<string> QUIET = {"printf", "scanf", "puts", "strlen", "strcpy", "strncpy", "strcat", "strcmp", "strncmp", "memset", "memcpy",
                                            "fprintf", "sprintf", "snprintf", "fputs", "fgets", "fread", "fwrite", "strchr", "strstr", "atoi", "sscanf", "fscanf"};
    if (n->kind == NK::Ident && n->sym >= 0 && sites.count(n->sym)) {
        bool ok = false;
        if (parent) {
            const string& o = parent->text;
            switch (parent->kind) {
                case NK::Unary: ok = o == "*" || o == "!"; break;
                case NK::Index: ok = parent->kid(0) == n; break;
                case NK::Binary: ok = o == "==" || o == "!=" || o == "<" || o == ">" || o == "<=" || o == ">=" || o == "&&" || o == "||"; break;
                case NK::If: case NK::While: case NK::Ternary: ok = parent->kid(0) == n; break;
                case NK::DoWhile: case NK::For: ok = parent->kid(1) == n; break;
                case NK::Assign: ok = parent->kid(0) == n; break;
                case NK::Call:
                    if (o == "free") { freed.insert(n->sym); ok = true; }
                    else ok = QUIET.count(o) > 0;
                    break;
                default: break;
            }
        }
        if (!ok) escaped.insert(n->sym);
    }
    for (auto& k : n->kids) scanEscapes(k.get(), n, sites, freed, escaped);
}

void Flow::leakCheck(Node* fn) {
    if (allocSites.empty()) return;
    std::set<int> freed, escaped;
    scanEscapes(fn->kids.back().get(), nullptr, allocSites, freed, escaped);
    for (auto& kv : allocSites) {
        if (freed.count(kv.first) || escaped.count(kv.first)) continue;
        const Symbol& sy = st.all[kv.first];
        rep("memory-leak", "Memory leak", Severity::Warning, kv.second,
            "Memory allocated here with " + kv.second->text + "() and stored in '" + sy.name + "' is never freed, returned or passed on, so it stays reserved after '" + fn->text + "' ends.",
            "Call free(" + sy.name + "); when you are done with the memory.", "Medium");
    }
    allocSites.clear();
}
