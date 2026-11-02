#include "ast.h"
#include <ostream>

const char* nkName(NK k) {
    switch (k) {
        case NK::Program: return "Program"; case NK::Function: return "Function";
        case NK::Param: return "Param"; case NK::Block: return "Block";
        case NK::DeclList: return "DeclList"; case NK::VarDecl: return "VarDecl";
        case NK::InitList: return "InitList"; case NK::If: return "If";
        case NK::While: return "While"; case NK::DoWhile: return "DoWhile";
        case NK::For: return "For"; case NK::Switch: return "Switch";
        case NK::Case: return "Case"; case NK::Default: return "Default"; case NK::Return: return "Return";
        case NK::Break: return "Break"; case NK::Continue: return "Continue";
        case NK::ExprStmt: return "ExprStmt"; case NK::Empty: return "Empty";
        case NK::Binary: return "Binary"; case NK::Unary: return "Unary";
        case NK::Postfix: return "Postfix"; case NK::Assign: return "Assign";
        case NK::Call: return "Call"; case NK::Index: return "Index";
        case NK::Ident: return "Ident"; case NK::IntLit: return "IntLit";
        case NK::FloatLit: return "FloatLit"; case NK::CharLit: return "CharLit";
        case NK::StringLit: return "StringLit"; case NK::Cast: return "Cast";
        case NK::Ternary: return "Ternary"; case NK::Sizeof: return "Sizeof";
    }
    return "?";
}

std::string typeStr(const std::string& base, int ptr, bool arr) {
    std::string s = base;
    if (ptr > 0) s += " " + std::string(ptr, '*');
    if (arr) s += "[]";
    return s;
}

static std::string wrap(const Node* n) {
    std::string s = exprStr(n);
    if (n && (n->kind == NK::Binary || n->kind == NK::Assign || n->kind == NK::Ternary)) return "(" + s + ")";
    return s;
}

std::string exprStr(const Node* n) {
    if (!n) return "";
    switch (n->kind) {
        case NK::Ident: case NK::IntLit: case NK::FloatLit: case NK::CharLit: return n->text;
        case NK::StringLit:
            return n->text.size() > 24 ? n->text.substr(0, 21) + "...\"" : n->text;
        case NK::Binary: case NK::Assign:
            return wrap(n->kid(0)) + " " + n->text + " " + wrap(n->kid(1));
        case NK::Unary: return n->text + wrap(n->kid(0));
        case NK::Postfix: return wrap(n->kid(0)) + n->text;
        case NK::Call: {
            std::string s = n->text + "(";
            for (size_t i = 0; i < n->kids.size(); i++) s += (i ? ", " : "") + exprStr(n->kids[i].get());
            return s + ")";
        }
        case NK::Index: return exprStr(n->kid(0)) + "[" + exprStr(n->kid(1)) + "]";
        case NK::Cast: return "(" + typeStr(n->type, n->ptr) + ")" + wrap(n->kid(0));
        case NK::Ternary: return wrap(n->kid(0)) + " ? " + wrap(n->kid(1)) + " : " + wrap(n->kid(2));
        case NK::Sizeof: return n->kid(0) ? "sizeof(" + exprStr(n->kid(0)) + ")" : "sizeof(" + typeStr(n->type, n->ptr) + ")";
        case NK::InitList: return "{...}";
        default: return "";
    }
}

std::string declStr(const Node* n) {
    std::string s = typeStr(n->type, n->ptr) + " " + n->text + (n->isArray ? "[]" : "");
    if (n->kid(0)) s += " = " + exprStr(n->kid(0));
    return s;
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    return o;
}

static std::string label(const Node* n) {
    switch (n->kind) {
        case NK::Program: return "Program";
        case NK::Function: return "Function " + n->text + "() -> " + typeStr(n->type, n->ptr);
        case NK::Param: return "Param " + n->text + " : " + typeStr(n->type, n->ptr);
        case NK::VarDecl: return "VarDecl " + n->text + " : " + typeStr(n->type, n->ptr, n->isArray);
        case NK::Binary: case NK::Unary: case NK::Postfix: case NK::Assign: return std::string(nkName(n->kind)) + " (" + n->text + ")";
        case NK::Call: return "Call " + n->text + "()";
        case NK::Ident: return "Ident " + n->text;
        case NK::IntLit: case NK::FloatLit: case NK::CharLit: return std::string(nkName(n->kind)) + " " + n->text;
        case NK::StringLit: return "String " + exprStr(n);
        case NK::Cast: return "Cast (" + typeStr(n->type, n->ptr) + ")";
        case NK::Empty: return "(empty)";
        default: return nkName(n->kind);
    }
}

void astToJson(const Node* n, std::ostream& os) {
    os << "{\"kind\":\"" << nkName(n->kind) << "\",\"label\":\"" << jsonEscape(label(n))
       << "\",\"line\":" << n->line << ",\"col\":" << n->col << ",\"children\":[";
    bool first = true;
    for (auto& k : n->kids) {
        if (!k) continue;
        if (!first) os << ",";
        first = false;
        astToJson(k.get(), os);
    }
    os << "]}";
}
