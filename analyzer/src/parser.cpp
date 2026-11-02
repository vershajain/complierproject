#include <cstdlib>
#include "parser.h"
#include <set>

using std::string;

namespace {
const std::set<string> TYPE_WORDS = {
    "int", "char", "float", "double", "void", "long", "short", "unsigned", "signed", "const",
    "static", "extern", "volatile", "register", "size_t", "bool", "FILE", "ssize_t", "int8_t",
    "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t"};
const std::set<string> QUALIFIERS = {"const", "static", "extern", "volatile", "register"};
const std::set<string> UNSUPPORTED = {"struct", "union", "enum", "typedef", "goto"};
const std::set<string> ASSIGN_OPS = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};

int prec(const string& op) {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "|") return 3;
    if (op == "^") return 4;
    if (op == "&") return 5;
    if (op == "==" || op == "!=") return 6;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 7;
    if (op == "<<" || op == ">>") return 8;
    if (op == "+" || op == "-") return 9;
    if (op == "*" || op == "/" || op == "%") return 10;
    return 0;
}
}  // namespace

Parser::Parser(std::vector<Token>& t, Diagnostics& d) : toks(t), diags(d) {}

const Token& Parser::cur() const { return toks[p < toks.size() ? p : toks.size() - 1]; }
bool Parser::is(const char* t) const {
    const Token& k = cur();
    return (k.type == TokType::Op || k.type == TokType::Keyword) && k.text == t;
}
bool Parser::isAt(size_t off, const char* t) const {
    size_t i = p + off;
    if (i >= toks.size()) return false;
    return (toks[i].type == TokType::Op || toks[i].type == TokType::Keyword) && toks[i].text == t;
}
bool Parser::isTypeStart() const { return cur().type == TokType::Keyword && TYPE_WORDS.count(cur().text); }
bool Parser::isTypeAt(size_t off) const {
    size_t i = p + off;
    return i < toks.size() && toks[i].type == TokType::Keyword && TYPE_WORDS.count(toks[i].text);
}
bool Parser::isUnsupported() const { return cur().type == TokType::Keyword && UNSUPPORTED.count(cur().text); }
bool Parser::accept(const char* t) {
    if (is(t)) { p++; return true; }
    return false;
}
string Parser::desc() const { return cur().type == TokType::End ? "end of file" : "'" + cur().text + "'"; }

void Parser::fail(const string& msg, const string& hint, int line, int col, const string& rule) const {
    throw ParseError{msg, hint, rule, line, col};
}
void Parser::failUnsupported() const {
    fail("'" + cur().text + "' is not supported by C-Lint yet.",
         "C-Lint analyzes a subset of C (no struct/union/enum/typedef/switch/goto). This part of the program is skipped.",
         cur().line, cur().col, "unsupported");
}

void Parser::expect(const char* t, const string& what, const string& hint) {
    if (is(t)) { p++; return; }
    string msg = string("Expected '") + t + "' " + what + ", but found " + desc() + ".";
    string h = hint.empty() ? string("Check the punctuation near this line; '") + t + "' is missing." : hint;
    if (string(t) == ";" && p > 0) {
        const Token& pv = toks[p - 1];
        // The next token starts on a later line: the semicolon was simply forgotten. Report it, then carry on
        // as if it had been typed, so the following statements are still parsed and checked.
        if (cur().type != TokType::End && cur().line > pv.line) {
            report(ParseError{msg, h, "syntax-error", pv.line, pv.col + (int)pv.text.size()});
            return;
        }
        fail(msg, h, pv.line, pv.col + (int)pv.text.size());
    }
    fail(msg, h, cur().line, cur().col);
}

void Parser::report(const ParseError& e) {
    errorCount++;
    diags.add(e.rule, e.rule == "unsupported" ? "Unsupported construct" : "Syntax error", Severity::Error,
              e.line, e.col, e.msg, e.hint, "High", "syntax");
}

// Error recovery: skip ahead to a likely statement boundary.
void Parser::sync() {
    while (cur().type != TokType::End) {
        if (is(";")) { p++; return; }
        if (is("}")) return;
        if (isTypeStart() || is("if") || is("while") || is("for") || is("return") || is("do") || is("switch") || is("case") || is("default")) return;
        p++;
    }
}

// Skips an unsupported construct such as a switch block or struct definition.
void Parser::skipConstruct() {
    int depth = 0;
    while (cur().type != TokType::End) {
        if (is("{")) depth++;
        else if (is("}")) { depth--; if (depth <= 0) { p++; accept(";"); return; } }
        else if (is(";") && depth == 0) { p++; return; }
        p++;
    }
}

NodePtr Parser::mk(NK k, const Token& t) const { return std::make_unique<Node>(k, t.line, t.col); }

NodePtr Parser::parseProgram() {
    auto prog = std::make_unique<Node>(NK::Program, 1, 1);
    while (cur().type != TokType::End) {
        size_t before = p;
        try {
            parseExternal(*prog);
        } catch (ParseError& e) {
            report(e);
            if (errorCount >= 25) break;
            if (e.rule == "unsupported") skipConstruct();
            else { sync(); if (is("}")) p++; }
            if (p == before) p++;
        }
    }
    return prog;
}

void Parser::parseExternal(Node& prog) {
    if (isUnsupported()) failUnsupported();
    if (!isTypeStart())
        fail("Expected a function or variable declaration, but found " + desc() + ".",
             "At the top level only declarations and function definitions are allowed. Statements must be inside a function.",
             cur().line, cur().col);
    string base = parseTypeSpec();
    size_t save = p;
    int ptr = 0;
    while (accept("*")) ptr++;
    if (cur().type == TokType::Ident && isAt(1, "(")) {
        Token nameTok = cur();
        p++;
        auto fn = mk(NK::Function, nameTok);
        fn->text = nameTok.text; fn->type = base; fn->ptr = ptr;
        parseParams(*fn);
        if (is("{")) { fn->hasBody = true; fn->kids.push_back(parseBlock()); }
        else expect(";", "after the function prototype");
        prog.kids.push_back(std::move(fn));
        return;
    }
    p = save;
    if (parseDeclarators(base, prog.kids)) expect(";", "after the declaration", "End the declaration with ';'.");
}

void Parser::parseParams(Node& fn) {
    expect("(", "to start the parameter list");
    if (is(")")) { p++; return; }
    if (is("void") && isAt(1, ")")) { p += 2; return; }
    while (true) {
        if (accept("...")) fn.variadic = true;
        else {
            if (!isTypeStart())
                fail("Expected a parameter type, but found " + desc() + ".", "Every parameter needs a type, e.g. 'int a'.", cur().line, cur().col);
            string base = parseTypeSpec();
            int ptr = 0;
            while (accept("*")) ptr++;
            while (is("const")) p++;
            auto prm = mk(NK::Param, cur());
            prm->type = base;
            if (cur().type == TokType::Ident) { prm->text = cur().text; p++; }
            while (accept("[")) {
                while (!is("]") && cur().type != TokType::End) p++;
                expect("]", "to close the array parameter");
                ptr++;
            }
            prm->ptr = ptr;
            fn.kids.push_back(std::move(prm));
        }
        if (!accept(",")) break;
    }
    expect(")", "to close the parameter list");
}

string Parser::parseTypeSpec() {
    std::vector<string> parts;
    while (isTypeStart()) {
        string t = cur().text;
        p++;
        if (QUALIFIERS.count(t)) continue;
        parts.push_back(t);
    }
    if (parts.empty()) return "int";
    string s;
    for (size_t i = 0; i < parts.size(); i++) s += (i ? " " : "") + parts[i];
    return s;
}

void Parser::skipToSemicolon() {
    while (cur().type != TokType::End) {
        if (is(";")) { p++; return; }
        if (is("}")) return;
        p++;
    }
}

bool Parser::parseDeclarators(const string& base, std::vector<NodePtr>& out) {
    do {
        int ptr = 0;
        while (accept("*")) { ptr++; while (is("const")) p++; }
        if (cur().type != TokType::Ident)
            fail("Expected a variable name, but found " + desc() + ".", "Give the variable a name, e.g. 'int count;'.", cur().line, cur().col);
        Token nameTok = cur();
        p++;
        auto d = mk(NK::VarDecl, nameTok);
        d->text = nameTok.text; d->type = base; d->ptr = ptr;
        int dims = 0;
        bool sized = false;
        while (accept("[")) {
            d->isArray = true;
            dims++;
            if (!is("]")) {
                auto sz = parseAssign();
                if (dims == 1 && sz->kind == NK::IntLit) { d->arrLen = std::strtoll(sz->text.c_str(), nullptr, 0); sized = true; }
            }
            expect("]", "to close the array size");
        }
        d->dims = dims;
        if (dims != 1) d->arrLen = -1;   // only 1-D arrays are bounds-checked
        if (accept("=")) {
            try {
                d->kids.push_back(parseInit());
            } catch (ParseError& e) {
                // keep the variable (without its broken initializer) so later uses are not reported as undeclared
                report(e);
                out.push_back(std::move(d));
                skipToSemicolon();
                return false;
            }
            // int a[] = {1, 2, 3};  -> length comes from the initializer list
            if (dims == 1 && !sized && d->kids[0]->kind == NK::InitList) d->arrLen = (long long)d->kids[0]->kids.size();
        }
        out.push_back(std::move(d));
    } while (accept(","));
    return true;
}

NodePtr Parser::parseInit() {
    if (is("{")) {
        auto n = mk(NK::InitList, cur());
        p++;
        while (!is("}") && cur().type != TokType::End) {
            n->kids.push_back(parseInit());
            if (!accept(",")) break;
        }
        expect("}", "to close the initializer list");
        return n;
    }
    return parseAssign();
}

NodePtr Parser::parseBlock() {
    Token open = cur();
    expect("{", "to start a block");
    auto b = mk(NK::Block, open);
    while (!is("}") && cur().type != TokType::End) {
        size_t before = p;
        try {
            parseStatementInto(*b);
        } catch (ParseError& e) {
            report(e);
            if (errorCount >= 25) { p = toks.size() - 1; break; }
            if (e.rule == "unsupported") skipConstruct();
            else { sync(); if (p == before && !is("}")) p++; }
        }
    }
    if (is("}")) { b->endLine = cur().line; b->endCol = cur().col; p++; }
    else if (errorCount < 25)
        fail("Missing closing '}' for the block opened on line " + std::to_string(open.line) + ".",
             "Add a '}' to close the block.", cur().line, cur().col);
    return b;
}

void Parser::parseStatementInto(Node& parent) {
    if (isUnsupported()) failUnsupported();
    if (isTypeStart()) {
        string base = parseTypeSpec();
        if (parseDeclarators(base, parent.kids)) expect(";", "after the declaration", "End the declaration with ';'.");
    } else {
        parent.kids.push_back(parseStatement());
    }
}

NodePtr Parser::parseStatement() {
    if (isUnsupported()) failUnsupported();
    Token t = cur();
    if (is("{")) return parseBlock();
    if (is("if")) {
        p++;
        auto n = mk(NK::If, t);
        expect("(", "after 'if'");
        n->kids.push_back(parseExpr());
        expect(")", "to close the 'if' condition");
        n->kids.push_back(parseStatement());
        if (accept("else")) n->kids.push_back(parseStatement());
        return n;
    }
    if (is("while")) {
        p++;
        auto n = mk(NK::While, t);
        expect("(", "after 'while'");
        n->kids.push_back(parseExpr());
        expect(")", "to close the 'while' condition");
        n->kids.push_back(parseStatement());
        return n;
    }
    if (is("do")) {
        p++;
        auto n = mk(NK::DoWhile, t);
        n->kids.push_back(parseStatement());
        expect("while", "after the body of 'do'");
        expect("(", "after 'while'");
        n->kids.push_back(parseExpr());
        expect(")", "to close the 'while' condition");
        expect(";", "after 'do ... while (...)'");
        return n;
    }
    if (is("for")) {
        p++;
        auto n = mk(NK::For, t);
        expect("(", "after 'for'");
        if (is(";")) { n->kids.push_back(mk(NK::Empty, cur())); p++; }
        else if (isTypeStart()) {
            auto dl = mk(NK::DeclList, cur());
            string base = parseTypeSpec();
            parseDeclarators(base, dl->kids);
            expect(";", "after the 'for' initializer");
            n->kids.push_back(std::move(dl));
        } else {
            auto es = mk(NK::ExprStmt, cur());
            es->kids.push_back(parseExpr());
            expect(";", "after the 'for' initializer");
            n->kids.push_back(std::move(es));
        }
        if (is(";")) n->kids.push_back(mk(NK::Empty, cur())); else n->kids.push_back(parseExpr());
        expect(";", "after the 'for' condition");
        if (is(")")) n->kids.push_back(mk(NK::Empty, cur())); else n->kids.push_back(parseExpr());
        expect(")", "to close the 'for' header");
        n->kids.push_back(parseStatement());
        return n;
    }
    if (is("switch")) {
        p++;
        auto n = mk(NK::Switch, t);
        expect("(", "after 'switch'");
        n->kids.push_back(parseExpr());
        expect(")", "to close the 'switch' expression");
        if (!is("{")) fail("Expected '{' after switch (...), but found " + desc() + ".", "A switch body must be enclosed in { }.", cur().line, cur().col);
        n->kids.push_back(parseBlock());
        return n;
    }
    if (is("case")) {
        p++;
        auto n = mk(NK::Case, t);
        n->kids.push_back(parseTernary());
        expect(":", "after the case value", "Write 'case VALUE:' with a colon.");
        return n;
    }
    if (is("default")) {
        p++;
        auto n = mk(NK::Default, t);
        expect(":", "after 'default'", "Write 'default:' with a colon.");
        return n;
    }
    if (is("return")) {
        p++;
        auto n = mk(NK::Return, t);
        if (!is(";")) n->kids.push_back(parseExpr());
        expect(";", "after 'return'");
        return n;
    }
    if (is("break") || is("continue")) {
        p++;
        auto n = mk(t.text == "break" ? NK::Break : NK::Continue, t);
        expect(";", "after '" + t.text + "'");
        return n;
    }
    if (is(";")) { p++; return mk(NK::Empty, t); }
    auto es = mk(NK::ExprStmt, t);
    es->kids.push_back(parseExpr());
    expect(";", "after the expression", "End the statement with ';'.");
    return es;
}

NodePtr Parser::parseExpr() {
    auto lhs = parseAssign();
    while (is(",")) {
        Token op = cur();
        p++;
        auto rhs = parseAssign();
        auto n = mk(NK::Binary, op);
        n->text = ",";
        n->kids.push_back(std::move(lhs));
        n->kids.push_back(std::move(rhs));
        lhs = std::move(n);
    }
    return lhs;
}

NodePtr Parser::parseAssign() {
    auto lhs = parseTernary();
    if (cur().type == TokType::Op && ASSIGN_OPS.count(cur().text)) {
        Token op = cur();
        bool lvalue = lhs->kind == NK::Ident || lhs->kind == NK::Index || (lhs->kind == NK::Unary && lhs->text == "*");
        if (!lvalue)
            fail("The left side of '" + op.text + "' must be a variable.", "You can only assign to a variable, array element or dereferenced pointer.", op.line, op.col);
        p++;
        auto rhs = parseAssign();
        auto n = mk(NK::Assign, op);
        n->text = op.text;
        n->kids.push_back(std::move(lhs));
        n->kids.push_back(std::move(rhs));
        return n;
    }
    return lhs;
}

NodePtr Parser::parseTernary() {
    auto c = parseBinary(1);
    if (is("?")) {
        Token q = cur();
        p++;
        auto a = parseExpr();
        expect(":", "in the '?:' expression");
        auto b = parseTernary();
        auto n = mk(NK::Ternary, q);
        n->kids.push_back(std::move(c));
        n->kids.push_back(std::move(a));
        n->kids.push_back(std::move(b));
        return n;
    }
    return c;
}

NodePtr Parser::parseBinary(int minPrec) {
    auto lhs = parseUnary();
    while (cur().type == TokType::Op) {
        int pr = prec(cur().text);
        if (pr == 0 || pr < minPrec) break;
        Token op = cur();
        p++;
        auto rhs = parseBinary(pr + 1);
        auto n = mk(NK::Binary, op);
        n->text = op.text;
        n->kids.push_back(std::move(lhs));
        n->kids.push_back(std::move(rhs));
        lhs = std::move(n);
    }
    return lhs;
}

NodePtr Parser::parseUnary() {
    Token t = cur();
    if (t.type == TokType::Op && (t.text == "-" || t.text == "+" || t.text == "!" || t.text == "~" ||
                                  t.text == "*" || t.text == "&" || t.text == "++" || t.text == "--")) {
        p++;
        auto n = mk(NK::Unary, t);
        n->text = t.text;
        n->kids.push_back(parseUnary());
        return n;
    }
    if (is("sizeof")) {
        p++;
        auto n = mk(NK::Sizeof, t);
        if (is("(") && isTypeAt(1)) {
            p++;
            n->type = parseTypeSpec();
            while (accept("*")) n->ptr++;
            expect(")", "to close sizeof");
        } else n->kids.push_back(parseUnary());
        return n;
    }
    if (is("(") && isTypeAt(1)) {
        p++;
        auto n = mk(NK::Cast, t);
        n->type = parseTypeSpec();
        while (accept("*")) n->ptr++;
        expect(")", "to close the cast");
        n->kids.push_back(parseUnary());
        return n;
    }
    return parsePostfix();
}

NodePtr Parser::parsePostfix() {
    auto e = parsePrimary();
    while (true) {
        if (is("(")) {
            if (e->kind != NK::Ident)
                fail("Only a function name can be called here.", "Check the expression before '('.", cur().line, cur().col);
            p++;
            auto c = std::make_unique<Node>(NK::Call, e->line, e->col);
            c->text = e->text;
            if (!is(")")) { do { c->kids.push_back(parseAssign()); } while (accept(",")); }
            expect(")", "to close the call to '" + c->text + "'");
            e = std::move(c);
        } else if (is("[")) {
            Token b = cur();
            p++;
            auto idx = parseExpr();
            expect("]", "to close the array index");
            auto n = mk(NK::Index, b);
            n->kids.push_back(std::move(e));
            n->kids.push_back(std::move(idx));
            e = std::move(n);
        } else if (is("++") || is("--")) {
            Token op = cur();
            p++;
            auto n = mk(NK::Postfix, op);
            n->text = op.text;
            n->kids.push_back(std::move(e));
            e = std::move(n);
        } else if (is(".") || is("->")) {
            fail("Member access '" + cur().text + "' (structs) is not supported by C-Lint yet.",
                 "C-Lint analyzes a subset of C without structs.", cur().line, cur().col, "unsupported");
        } else break;
    }
    return e;
}

NodePtr Parser::parsePrimary() {
    const Token t = cur();
    switch (t.type) {
        case TokType::IntLit: case TokType::FloatLit: case TokType::CharLit: case TokType::Ident: {
            p++;
            NK k = t.type == TokType::IntLit ? NK::IntLit : t.type == TokType::FloatLit ? NK::FloatLit
                 : t.type == TokType::CharLit ? NK::CharLit : NK::Ident;
            auto n = mk(k, t);
            n->text = t.text;
            return n;
        }
        case TokType::StringLit: {
            p++;
            auto n = mk(NK::StringLit, t);
            n->text = t.text;
            while (cur().type == TokType::StringLit) {   // "a" "b" -> "ab"
                if (n->text.size() >= 2) n->text.pop_back();
                n->text += cur().text.substr(1);
                p++;
            }
            return n;
        }
        default: break;
    }
    if (is("(")) {
        p++;
        auto e = parseExpr();
        expect(")", "to close the parenthesis");
        e->paren = true;
        return e;
    }
    fail("Expected an expression, but found " + desc() + ".",
         "An operand such as a number, a variable name or '(' is required here (is a value missing after an operator?).",
         cur().line, cur().col);
}
