#pragma once
#include <cstdlib>
#include <functional>
#include <map>
#include "ast.h"
#include "lexer.h"

using IdLookup = std::function<bool(const Node*, long long&)>;

inline bool macroValue(const std::string& name, const std::map<std::string, Macro>& m, long long& out) {
    if (name == "NULL" || name == "false") { out = 0; return true; }
    if (name == "true") { out = 1; return true; }
    auto it = m.find(name);
    if (it == m.end() || it->second.isFunction) return false;
    std::string t;
    for (char c : it->second.body) if (c != ' ' && c != '\t' && c != '(' && c != ')') t += c;
    if (t.empty()) return false;
    char* end = nullptr;
    long long v = strtoll(t.c_str(), &end, 0);
    if (end == t.c_str()) return false;
    while (*end == 'u' || *end == 'U' || *end == 'l' || *end == 'L') end++;
    if (*end != '\0') return false;
    out = v;
    return true;
}

// Evaluates an expression made only of constants (literals, simple macros, and optionally
// variables whose value is known via `idl`). Returns false if the value is not known.
inline bool constEval(const Node* n, const std::map<std::string, Macro>& m, long long& out,
                      const IdLookup& idl = IdLookup()) {
    if (!n) return false;
    switch (n->kind) {
        case NK::IntLit: out = strtoll(n->text.c_str(), nullptr, 0); return true;
        case NK::CharLit: {
            const std::string& t = n->text;
            if (t.size() < 3) return false;
            if (t[1] == '\\') { char e = t[2]; out = e == 'n' ? 10 : e == 't' ? 9 : e == 'r' ? 13 : e == '0' ? 0 : e; }
            else out = (unsigned char)t[1];
            return true;
        }
        case NK::Ident:
            if (n->sym < 0 && macroValue(n->text, m, out)) return true;
            return idl && idl(n, out);
        case NK::Cast: return constEval(n->kid(0), m, out, idl);
        case NK::Unary: {
            long long v;
            if (!constEval(n->kid(0), m, v, idl)) return false;
            if (n->text == "-") { out = -v; return true; }
            if (n->text == "+") { out = v; return true; }
            if (n->text == "!") { out = !v; return true; }
            if (n->text == "~") { out = ~v; return true; }
            return false;
        }
        case NK::Binary: {
            long long a, b;
            if (!constEval(n->kid(0), m, a, idl) || !constEval(n->kid(1), m, b, idl)) return false;
            const std::string& o = n->text;
            if (o == "+") out = a + b; else if (o == "-") out = a - b; else if (o == "*") out = a * b;
            else if (o == "/") { if (!b) return false; out = a / b; }
            else if (o == "%") { if (!b) return false; out = a % b; }
            else if (o == "==") out = a == b; else if (o == "!=") out = a != b;
            else if (o == "<") out = a < b; else if (o == ">") out = a > b;
            else if (o == "<=") out = a <= b; else if (o == ">=") out = a >= b;
            else if (o == "&&") out = a && b; else if (o == "||") out = a || b;
            else if (o == "&") out = a & b; else if (o == "|") out = a | b; else if (o == "^") out = a ^ b;
            else if (o == "<<") out = a << (b & 63); else if (o == ">>") out = a >> (b & 63);
            else return false;
            return true;
        }
        case NK::Ternary: {
            long long c;
            if (!constEval(n->kid(0), m, c, idl)) return false;
            return constEval(c ? n->kid(1) : n->kid(2), m, out, idl);
        }
        default: return false;
    }
}
