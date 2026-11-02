#include "semantic.h"
#include <algorithm>
#include <set>
#include "consteval.h"

using std::string;

namespace {
const std::set<string> BUILTIN_FUNCS = {
    "printf", "scanf", "puts", "putchar", "getchar", "gets", "fgets", "fputs", "fprintf", "sprintf", "snprintf",
    "sscanf", "fscanf", "malloc", "calloc", "realloc", "free", "exit", "abort", "atoi", "atof", "atol", "abs",
    "labs", "rand", "srand", "time", "strlen", "strcpy", "strncpy", "strcat", "strncat", "strcmp", "strncmp",
    "strchr", "strstr", "memset", "memcpy", "memmove", "memcmp", "sqrt", "pow", "sin", "cos", "tan", "fabs",
    "floor", "ceil", "log", "log10", "exp", "round", "fmod", "fopen", "fclose", "fread", "fwrite", "fflush",
    "perror", "toupper", "tolower", "isdigit", "isalpha", "isalnum", "isspace", "isupper", "islower", "system",
    "qsort", "getc", "putc", "fgetc", "fputc", "feof", "rewind", "fseek", "ftell", "remove", "rename", "clock"};
const std::set<string> BUILTIN_CONSTS = {"NULL", "true", "false", "stdin", "stdout", "stderr", "EOF", "INT_MAX",
    "INT_MIN", "UINT_MAX", "LONG_MAX", "LONG_MIN", "RAND_MAX", "M_PI", "CHAR_BIT", "FLT_MAX", "DBL_MAX",
    "EXIT_SUCCESS", "EXIT_FAILURE", "BUFSIZ", "SEEK_SET", "SEEK_CUR", "SEEK_END"};
const std::map<string, std::pair<string, int>> BUILTIN_RET = {
    {"printf", {"int", 0}}, {"scanf", {"int", 0}}, {"strlen", {"size_t", 0}}, {"atoi", {"int", 0}},
    {"rand", {"int", 0}}, {"getchar", {"int", 0}}, {"malloc", {"void", 1}}, {"calloc", {"void", 1}},
    {"realloc", {"void", 1}}, {"sqrt", {"double", 0}}, {"pow", {"double", 0}}, {"sin", {"double", 0}},
    {"cos", {"double", 0}}, {"tan", {"double", 0}}, {"fabs", {"double", 0}}, {"floor", {"double", 0}},
    {"ceil", {"double", 0}}, {"log", {"double", 0}}, {"exp", {"double", 0}}, {"atof", {"double", 0}}};

const std::map<string, string> HEADER_OF = [] {
    std::map<string, string> m;
    auto add = [&](const char* h, std::initializer_list<const char*> fs) { for (auto f : fs) m[f] = h; };
    add("stdio.h", {"printf", "scanf", "puts", "putchar", "getchar", "gets", "fgets", "fputs", "fprintf", "sprintf", "snprintf", "sscanf", "fscanf", "fopen", "fclose", "fread", "fwrite", "fflush", "perror", "getc", "putc", "fgetc", "fputc", "feof", "rewind", "fseek", "ftell", "remove", "rename"});
    add("stdlib.h", {"malloc", "calloc", "realloc", "free", "exit", "abort", "atoi", "atof", "atol", "abs", "labs", "rand", "srand", "system", "qsort"});
    add("string.h", {"strlen", "strcpy", "strncpy", "strcat", "strncat", "strcmp", "strncmp", "strchr", "strstr", "memset", "memcpy", "memmove", "memcmp"});
    add("math.h", {"sqrt", "pow", "sin", "cos", "tan", "fabs", "floor", "ceil", "log", "log10", "exp", "round", "fmod"});
    add("ctype.h", {"toupper", "tolower", "isdigit", "isalpha", "isalnum", "isspace", "isupper", "islower"});
    add("time.h", {"time", "clock"});
    return m;
}();

bool pureExpr(const Node* n) {
    if (!n) return false;
    switch (n->kind) {
        case NK::Ident: case NK::IntLit: case NK::CharLit: case NK::FloatLit: return true;
        case NK::Index: return pureExpr(n->kid(0)) && pureExpr(n->kid(1));
        case NK::Unary: return n->text != "++" && n->text != "--" && pureExpr(n->kid(0));
        case NK::Cast: return pureExpr(n->kid(0));
        default: return false;
    }
}
bool isCmpOp(const string& o) { return o == "<" || o == ">" || o == "<=" || o == ">="; }

namespace {
enum class SC { Char, Short, Int, Long, Float, Double, LDouble, Other };

SC scalarClass(const string& b) {
    if (b.find("long double") != string::npos) return SC::LDouble;
    if (b.find("double") != string::npos) return SC::Double;
    if (b.find("float") != string::npos) return SC::Float;
    if (b.find("char") != string::npos || b == "bool" || b == "int8_t" || b == "uint8_t") return SC::Char;
    if (b.find("short") != string::npos || b == "int16_t" || b == "uint16_t") return SC::Short;
    if (b.find("long") != string::npos || b == "size_t" || b == "ssize_t" || b == "int64_t" || b == "uint64_t") return SC::Long;
    if (b.find("int") != string::npos || b == "unsigned" || b == "signed" || b == "int32_t" || b == "uint32_t") return SC::Int;
    return SC::Other;
}
const char* scName(SC c) {
    switch (c) {
        case SC::Char: return "char"; case SC::Short: return "short"; case SC::Int: return "int";
        case SC::Long: return "long"; case SC::Float: return "float"; case SC::Double: return "double";
        case SC::LDouble: return "long double"; default: return "?";
    }
}
const char* scanLetterFor(SC c) {
    switch (c) {
        case SC::Char: return "%c"; case SC::Short: return "%hd"; case SC::Int: return "%d"; case SC::Long: return "%ld";
        case SC::Float: return "%f"; case SC::Double: return "%lf"; case SC::LDouble: return "%Lf"; default: return "%d";
    }
}

struct Spec {
    size_t pos = 0;
    string text, len, problem;
    char conv = 0;
    int stars = 0;
    bool suppress = false, hasWidth = false, valid = true;
};

// Splits a raw string literal (as written in the source, quotes included) into its % placeholders.
std::vector<Spec> parseFormat(const string& lit, bool scan) {
    std::vector<Spec> out;
    size_t end = lit.size();
    if (end >= 2 && lit.back() == '"') end--;
    static const string PRINT_OK = "diouxXfFeEgGaAcspn", SCAN_OK = "diouxXfFeEgGaAcspn[";
    for (size_t i = 1; i < end; i++) {
        char c = lit[i];
        if (c == '\\') { i++; continue; }
        if (c != '%') continue;
        Spec sp;
        size_t start = i;
        sp.pos = start;
        i++;
        if (i >= end) { sp.valid = false; sp.text = "%"; sp.problem = "the string ends right after the '%'"; out.push_back(sp); break; }
        if (lit[i] == '%') continue;   // "%%" prints a percent sign
        if (scan) {
            if (lit[i] == '*') { sp.suppress = true; i++; }
            while (i < end && isdigit((unsigned char)lit[i])) { sp.hasWidth = true; i++; }
        } else {
            while (i < end && string("-+ #0").find(lit[i]) != string::npos) i++;
            if (i < end && lit[i] == '*') { sp.stars++; i++; } else while (i < end && isdigit((unsigned char)lit[i])) i++;
            if (i < end && lit[i] == '.') {
                i++;
                if (i < end && lit[i] == '*') { sp.stars++; i++; } else while (i < end && isdigit((unsigned char)lit[i])) i++;
            }
        }
        while (i < end && string("hlLjzt").find(lit[i]) != string::npos) sp.len += lit[i++];
        if (i >= end) { sp.valid = false; sp.text = lit.substr(start, end - start); sp.problem = "the string ends before the conversion letter"; out.push_back(sp); break; }
        char cv = lit[i];
        sp.conv = cv;
        if ((scan ? SCAN_OK : PRINT_OK).find(cv) == string::npos) {
            sp.valid = false;
            sp.text = lit.substr(start, i + 1 - start);
            if (cv == '\\' && i + 1 < end) { sp.text += lit[i + 1]; i++; }
            string shown = sp.text.substr(sp.text.find_last_of("%") == string::npos ? 0 : 1);
            sp.problem = "'" + (cv == '\\' ? sp.text.substr(sp.text.size() - 2) : string(1, cv)) + "' (right after the '%') is not a conversion letter";
            (void)shown;
            out.push_back(sp);
            continue;
        }
        if (cv == '[') {
            size_t j = i + 1;
            if (j < end && lit[j] == '^') j++;
            if (j < end && lit[j] == ']') j++;
            while (j < end && lit[j] != ']') j++;
            i = j < end ? j : end - 1;
        }
        sp.text = lit.substr(start, i + 1 - start);
        out.push_back(sp);
    }
    return out;
}

size_t litLen(const string& lit) {
    size_t n = 0, end = lit.size();
    if (end >= 2 && lit.back() == '"') end--;
    for (size_t i = 1; i < end; i++) { if (lit[i] == '\\') i++; n++; }
    return n;
}
}  // namespace

int lev(const string& a, const string& b) {
    std::vector<int> prev(b.size() + 1), curr(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) prev[j] = (int)j;
    for (size_t i = 1; i <= a.size(); i++) {
        curr[0] = (int)i;
        for (size_t j = 1; j <= b.size(); j++)
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + (a[i - 1] != b[j - 1])});
        prev = curr;
    }
    return prev[b.size()];
}
bool isFloatBase(const string& b) { return b.find("float") != string::npos || b.find("double") != string::npos; }
char cat(const string& b) { return isFloatBase(b) ? 'F' : b == "void" ? 'V' : 'I'; }
string tstr(const string& base, int ptr) { return typeStr(base, ptr); }
}  // namespace

void Semantic::err(const string& rule, const string& title, const Node* at, const string& msg, const string& sug, const string& conf) {
    diags.add(rule, title, Severity::Error, at->line, at->col, msg, sug, conf, "semantic");
}
void Semantic::warn(const string& rule, const string& title, const Node* at, const string& msg, const string& sug, const string& conf) {
    diags.add(rule, title, Severity::Warning, at->line, at->col, msg, sug, conf, "semantic");
}

void Semantic::info(const string& rule, const string& title, const Node* at, const string& msg, const string& sug, const string& conf) {
    diags.add(rule, title, Severity::Info, at->line, at->col, msg, sug, conf, "semantic");
}

string Semantic::scopeLabel() const {
    if (st.depth() <= 1 || !curFn) return "global";
    return curFn->text + (st.depth() > 2 ? "/block" + std::to_string(st.depth() - 2) : "");
}

void Semantic::run(Node* prog) {
    for (auto& k : prog->kids) if (k->kind == NK::Function) declareFunction(*k);
    for (auto& k : prog->kids) {
        if (k->kind == NK::Function) visitFunction(*k);
        else if (k->kind == NK::VarDecl) visitVarDecl(*k, true);
    }
    if (!partial) {
        bool anyBody = false, hasMain = false;
        for (auto& k : prog->kids) if (k->kind == NK::Function && k->hasBody) { anyBody = true; if (k->text == "main") hasMain = true; }
        if (anyBody && !hasMain) {
            Node at(NK::Function, 1, 1);
            warn("missing-main", "No main function", &at, "The program has no main() function, so it cannot be built into an executable.", "Add 'int main() { ... return 0; }'.", "Medium");
        }
        for (auto& k : prog->kids)
            if (k->kind == NK::Function && k->text == "main" && k->hasBody && !(k->type == "int" && k->ptr == 0))
                warn("main-return-type", "Unusual return type of main", k.get(), "main() is declared to return '" + typeStr(k->type, k->ptr) + "', but the C standard requires int.", "Declare it as 'int main(...)' and return 0 on success.", "Medium");
        for (auto& sy : st.all)
            if (sy.isFunc && !sy.hasBody && sy.used) {
                Node at(NK::Function, sy.line, sy.col);
                warn("undefined-reference", "Function declared but never defined", &at, "Function '" + sy.name + "' is called but only declared, never defined in this file. The linker will fail with 'undefined reference'.",
                     "Write the function body, or include the file/library that defines it.", "Medium");
            }
    }
    reportUnused();
}

void Semantic::declareFunction(Node& n) {
    int ex = st.lookupCurrent(n.text);
    if (ex >= 0) {
        Symbol& s = st.all[ex];
        if (!s.isFunc) {
            err("redeclaration", "Redeclaration", &n, "'" + n.text + "' was already declared as a variable on line " + std::to_string(s.line) + ".", "Use a different name for the function.");
            return;
        }
        if (s.hasBody && n.hasBody)
            err("redeclaration", "Redefinition", &n, "Function '" + n.text + "' is defined more than once (first on line " + std::to_string(s.line) + ").", "Remove or rename the duplicate definition.");
        else if (n.hasBody) s.hasBody = true;
        n.sym = ex;
        return;
    }
    Symbol s;
    s.name = n.text; s.type = n.type; s.ptr = n.ptr; s.isFunc = true; s.isGlobal = true; s.hasBody = n.hasBody;
    s.line = n.line; s.col = n.col; s.variadic = n.variadic; s.scope = "global"; s.hasInit = true;
    int pc = 0;
    for (auto& k : n.kids) if (k->kind == NK::Param) pc++;
    s.paramCount = pc;
    for (auto& k : n.kids) if (k->kind == NK::Param) s.paramTypes.push_back({k->type, k->ptr});
    n.sym = st.declare(s);
}

void Semantic::visitFunction(Node& n) {
    if (!n.hasBody) return;
    curFn = &n;
    st.push();
    for (auto& k : n.kids) {
        if (k->kind != NK::Param || k->text.empty()) continue;
        int ex = st.lookupCurrent(k->text);
        if (ex >= 0) {
            err("redeclaration", "Redeclaration", k.get(), "Parameter '" + k->text + "' is declared twice.", "Give each parameter a unique name.");
            continue;
        }
        Symbol s;
        s.name = k->text; s.type = k->type; s.ptr = k->ptr; s.isParam = true; s.hasInit = true;
        s.line = k->line; s.col = k->col; s.scope = n.text;
        k->sym = st.declare(s);
    }
    Node* body = n.kids.back().get();
    for (auto& s : body->kids) visitStmt(s.get());
    st.pop();
    curFn = nullptr;
}

void Semantic::visitVarDecl(Node& n, bool global) {
    if (n.type == "void" && n.ptr == 0 && !n.isArray)
        err("invalid-type", "Invalid type", &n, "Variable '" + n.text + "' cannot have type 'void'.", "Use a real type such as int, or use 'void *' for a generic pointer.");
    int ex = st.lookupCurrent(n.text);
    if (ex >= 0) {
        err("redeclaration", "Redeclaration", &n,
            "'" + n.text + "' is already declared in this scope (first declared on line " + std::to_string(st.all[ex].line) + ").",
            "Rename one of the variables, or delete the duplicate declaration.");
        if (n.kid(0)) visitExpr(n.kid(0));
        return;
    }
    if (!global) {
        int outer = st.lookup(n.text);
        if (outer >= 0 && !st.all[outer].isFunc)
            warn("shadowed-variable", "Shadowed variable", &n,
                 "'" + n.text + "' hides another variable with the same name declared on line " + std::to_string(st.all[outer].line) + ".",
                 "Rename the inner variable (e.g. '" + n.text + "2') so it is clear which one you are using.", "Medium");
    }
    Symbol s;
    s.name = n.text; s.type = n.type; s.ptr = n.ptr; s.isArray = n.isArray; s.arrLen = n.arrLen; s.dims = n.dims; s.isGlobal = global;
    s.hasInit = n.kid(0) != nullptr; s.line = n.line; s.col = n.col; s.scope = global ? "global" : scopeLabel();
    n.sym = st.declare(s);
    if (n.isArray && n.arrLen == 0)
        err("invalid-array-size", "Invalid array size", &n, "Array '" + n.text + "' is declared with size 0.", "Give the array a positive size, e.g. " + n.text + "[10].");
    if (Node* ini = n.kid(0)) {
        if (n.isArray && n.arrLen > 0) {
            if (ini->kind == NK::StringLit && scalarClass(n.type) == SC::Char && (long long)litLen(ini->text) + 1 > n.arrLen)
                err("buffer-overflow", "String does not fit", &n, "The string needs " + std::to_string(litLen(ini->text) + 1) + " bytes (with the terminating '\\0') but '" + n.text + "' only holds " + std::to_string(n.arrLen) + ".",
                    "Make the array at least " + std::to_string(litLen(ini->text) + 1) + " long, or write char " + n.text + "[] = \"...\"; to let the compiler size it.");
            if (ini->kind == NK::InitList && (long long)ini->kids.size() > n.arrLen)
                err("array-out-of-bounds", "Too many initializers", &n, "'" + n.text + "' holds " + std::to_string(n.arrLen) + " element(s) but the initializer list has " + std::to_string(ini->kids.size()) + ".",
                    "Remove the extra values or enlarge the array.");
        }
    }
    if (Node* init = n.kid(0)) {
        visitExpr(init);
        TI dst{n.type, n.ptr, n.isArray, true};
        checkAssign(dst, init, &n, "Initializing");
    }
}

void Semantic::visitCond(Node* c) {
    visitExpr(c);
    if (c->kind == NK::Assign && c->text == "=" && !c->paren)
        warn("assignment-in-condition", "Assignment in condition", c,
             "'" + exprStr(c) + "' assigns a value inside a condition instead of comparing.",
             "Did you mean '=='? If the assignment is intentional, wrap it in extra parentheses.", "Medium");
}

void Semantic::visitStmt(Node* n) {
    if (!n) return;
    switch (n->kind) {
        case NK::Block: st.push(); for (auto& k : n->kids) visitStmt(k.get()); st.pop(); break;
        case NK::DeclList: for (auto& k : n->kids) visitStmt(k.get()); break;
        case NK::VarDecl: visitVarDecl(*n, false); break;
        case NK::ExprStmt: visitExpr(n->kid(0)); checkStmtEffect(n->kid(0)); break;
        case NK::If:
            visitCond(n->kid(0));
            if (n->kid(1) && n->kid(1)->kind == NK::Empty && !n->kid(2))
                warn("empty-statement", "Empty statement after 'if'", n->kid(1),
                     "The ';' right after this 'if (...)' is an empty body, so the 'if' does nothing and the code after it always runs.",
                     "Remove the ';', or put the intended statements in { } after the condition.", "Medium");
            visitStmt(n->kid(1));
            visitStmt(n->kid(2));
            break;
        case NK::While:
            visitCond(n->kid(0));
            if (n->kid(1) && n->kid(1)->kind == NK::Empty)
                warn("empty-statement", "Empty loop body", n->kid(1),
                     "The ';' right after 'while (...)' is an empty loop body, so the loop repeats doing nothing.",
                     "Remove the ';' and put the loop body in { }, unless an empty loop is really intended.", "Medium");
            loopDepth++; visitStmt(n->kid(1)); loopDepth--;
            break;
        case NK::DoWhile:
            loopDepth++; visitStmt(n->kid(0)); loopDepth--;
            visitCond(n->kid(1));
            break;
        case NK::For:
            st.push();
            visitStmt(n->kid(0));
            if (n->kid(1)->kind != NK::Empty) visitCond(n->kid(1));
            if (n->kid(2)->kind != NK::Empty) visitExpr(n->kid(2));
            if (n->kid(3) && n->kid(3)->kind == NK::Empty)
                info("empty-statement", "Empty loop body", n->kid(3), "The ';' right after 'for (...)' is an empty loop body, so the loop only repeats its header.",
                     "Remove the ';' and put the loop body in { }, unless an empty loop is really intended.", "Low");
            loopDepth++; visitStmt(n->kid(3)); loopDepth--;
            st.pop();
            break;
        case NK::Switch: {
            visitExpr(n->kid(0));
            std::set<long long> seen;
            bool hasDefault = false, hasCase = false;
            for (auto& k : n->kid(1)->kids) {
                long long v;
                if (k->kind == NK::Default) hasDefault = true;
                if (k->kind == NK::Case) {
                    hasCase = true;
                    if (constEval(k->kid(0), macros, v) && !seen.insert(v).second)
                        err("duplicate-case", "Duplicate case", k.get(), "The case value " + std::to_string(v) + " appears more than once in this switch.", "Remove the duplicate or give it a different value.");
                }
            }
            if (hasCase && !hasDefault) info("switch-no-default", "Switch without default", n, "This switch has no 'default:' case, so values that match no case are silently ignored.", "Add 'default:' to handle unexpected values (even if it only reports an error).", "Low");
            switchDepth++;
            visitStmt(n->kid(1));
            switchDepth--;
            break;
        }
        case NK::Case: case NK::Default:
            if (switchDepth == 0)
                err("case-outside-switch", "Misplaced label", n, string("'") + (n->kind == NK::Case ? "case" : "default") + "' can only appear inside a switch.", "Move it inside a switch statement.");
            if (n->kind == NK::Case) visitExpr(n->kid(0));
            break;
        case NK::Return: {
            Node* e = n->kid(0);
            if (e) visitExpr(e);
            if (!curFn) break;
            if (e && e->kind == NK::Unary && e->text == "&" && e->kid(0)->kind == NK::Ident && e->kid(0)->sym >= 0 && !st.all[e->kid(0)->sym].isGlobal)
                warn("return-local-address", "Returning address of a local variable", e,
                     "'" + exprStr(e) + "' is the address of the local variable '" + e->kid(0)->text + "', which no longer exists after the function returns.",
                     "Return the value itself, or allocate memory with malloc, or pass a buffer in as a parameter.", "High");
            else if (e && e->kind == NK::Ident && e->sym >= 0 && st.all[e->sym].isArray && !st.all[e->sym].isGlobal && !st.all[e->sym].isParam)
                warn("return-local-address", "Returning address of a local array", e,
                     "'" + e->text + "' is a local array. It is destroyed when the function returns, so the returned pointer is invalid.",
                     "Use malloc for memory that must outlive the function, or have the caller pass the buffer in.", "High");
            bool isVoid = curFn->type == "void" && curFn->ptr == 0;
            if (isVoid && e)
                err("return-type", "Return type mismatch", n, "Function '" + curFn->text + "' is declared void but returns a value.", "Remove the value after 'return', or change the function's return type.");
            else if (!isVoid && !e)
                warn("return-type", "Return type mismatch", n, "Function '" + curFn->text + "' should return a value of type '" + typeStr(curFn->type, curFn->ptr) + "'.", "Add a value, e.g. 'return 0;'.");
            else if (e) checkAssign(TI{curFn->type, curFn->ptr, false, true}, e, n, "Returning");
            break;
        }
        case NK::Break: case NK::Continue:
            if (loopDepth == 0 && (n->kind == NK::Continue || switchDepth == 0))
                err("break-outside-loop", "Misplaced statement", n, string("'") + (n->kind == NK::Break ? "break" : "continue") + "' can only be used inside a loop" + (n->kind == NK::Break ? " or switch." : "."), "Remove it, or move it inside a for/while/do loop.");
            break;
        default: break;
    }
}

void Semantic::visitIdent(Node* n, bool read) {
    int id = st.lookup(n->text);
    if (id >= 0) {
        n->sym = id;
        if (read) st.all[id].used = true;
        return;
    }
    n->sym = -1;
    if (BUILTIN_CONSTS.count(n->text) || BUILTIN_FUNCS.count(n->text) || macros.count(n->text)) return;
    string best; int bd = 3;
    for (auto& cand : st.visibleNames()) {
        int d = lev(cand, n->text);
        if (d < bd && cand != n->text) { bd = d; best = cand; }
    }
    string sug = best.empty() ? "Declare it before using it, for example 'int " + n->text + ";'."
                              : "Did you mean '" + best + "'? Otherwise declare '" + n->text + "' before using it.";
    if (lenient || partial)
        warn("undeclared-identifier", "Undeclared identifier", n, "'" + n->text + "' is not declared in the part of the file that could be read" + (lenient ? " (it may come from a local header)." : " (a syntax error elsewhere may have hidden its declaration)."), sug, "Low");
    else
        err("undeclared-identifier", "Undeclared identifier", n, "'" + n->text + "' is used but has not been declared.", sug);
}

void Semantic::visitExpr(Node* n, bool read) {
    if (!n) return;
    switch (n->kind) {
        case NK::Ident: visitIdent(n, read); break;
        case NK::Assign: {
            Node* l = n->kid(0); Node* r = n->kid(1);
            bool plain = n->text == "=";
            if (l->kind == NK::Ident) {
                visitIdent(l, !plain);
                if (l->sym >= 0) st.all[l->sym].assignedLater = true;
            } else visitExpr(l);
            visitExpr(r);
            if (plain && l->kind == NK::Ident && l->sym >= 0 && st.all[l->sym].isArray && !st.all[l->sym].isParam)
                err("invalid-assignment", "Cannot assign to an array", n, "'" + l->text + "' is an array, and arrays cannot be assigned with '='.",
                    scalarClass(st.all[l->sym].type) == SC::Char ? "Copy text with strcpy(" + l->text + ", ...) (check the size!) or snprintf." : "Assign element by element, or use memcpy.");
            if (plain) checkAssign(typeOf(l), r, n, "Assigning");
            break;
        }
        case NK::Unary: {
            Node* k = n->kid(0);
            bool incdec = n->text == "++" || n->text == "--";
            if ((n->text == "&" || incdec) && k->kind == NK::Ident) {
                visitIdent(k, true);
                if (k->sym >= 0) {
                    st.all[k->sym].assignedLater = true;
                    if (n->text == "&") st.all[k->sym].addrTaken = true;
                }
            } else visitExpr(k);
            if (n->text == "*") {
                TI kt = typeOf(k);
                if (kt.known && kt.ptr == 0 && !kt.arr)
                    err("invalid-operands", "Cannot dereference a non-pointer", n, "'*" + exprStr(k) + "': '" + exprStr(k) + "' is a " + typeStr(kt.base, 0) + ", not a pointer, so it cannot be dereferenced.",
                        "Remove the '*', or declare the variable as a pointer (" + kt.base + " *).");
            }
            break;
        }
        case NK::Index: {
            for (auto& k : n->kids) visitExpr(k.get());
            TI bt = typeOf(n->kid(0));
            if (bt.known && bt.ptr == 0 && !bt.arr)
                err("invalid-operands", "Subscript on a non-array", n, "'" + exprStr(n->kid(0)) + "' is a " + typeStr(bt.base, 0) + ", not an array or pointer, so it cannot be indexed with [].",
                    "Index an array or pointer, or remove the [ ].");
            break;
        }
        case NK::Postfix: {
            Node* k = n->kid(0);
            visitExpr(k);
            if (k->kind == NK::Ident && k->sym >= 0) st.all[k->sym].assignedLater = true;
            break;
        }
        case NK::Binary:
            visitExpr(n->kid(0)); visitExpr(n->kid(1));
            if ((n->text == "==" || n->text == "!=") && (n->kid(0)->kind == NK::StringLit || n->kid(1)->kind == NK::StringLit))
                warn("string-compare", "String comparison", n,
                     "Comparing a string with '" + n->text + "' compares memory addresses, not the text.",
                     "Use strcmp(a, b) == 0 (from <string.h>) to compare strings.", "High");
            checkBinary(n);
            break;
        case NK::Call: visitCall(n); break;
        case NK::Sizeof: if (n->kid(0)) visitExpr(n->kid(0)); break;
        default: for (auto& k : n->kids) visitExpr(k.get()); break;
    }
}

void Semantic::visitCall(Node* n) {
    for (auto& a : n->kids) visitExpr(a.get());
    int s = st.lookup(n->text);
    n->sym = -1;
    if (s >= 0 && st.all[s].isFunc) {
        n->sym = s;
        Symbol& f = st.all[s];
        int given = (int)n->kids.size();
        if (f.paramCount >= 0 && !f.variadic && given != f.paramCount)
            err("arg-count", "Wrong number of arguments", n,
                "Function '" + f.name + "' expects " + std::to_string(f.paramCount) + " argument(s) but " + std::to_string(given) + (given == 1 ? " was" : " were") + " given.",
                "Match the call to the function's declaration on line " + std::to_string(f.line) + ".");
        else
            for (size_t i = 0; i < n->kids.size() && i < f.paramTypes.size(); i++)
                checkAssign(TI{f.paramTypes[i].first, f.paramTypes[i].second, false, true}, n->kids[i].get(), n->kids[i].get(), "Passing");
        f.used = true;
        if (f.line > n->line)
            warn("implicit-declaration", "Function used before its declaration", n,
                 "Function '" + f.name + "' is called here but only declared later, on line " + std::to_string(f.line) + ".",
                 "Add a prototype such as '" + typeStr(f.type, f.ptr) + " " + f.name + "(...);' above, or move the definition before its first use.", "Medium");
    } else if (s >= 0) {
        err("not-a-function", "Not a function", n, "'" + n->text + "' is a variable, not a function.", "Remove the parentheses, or call a real function.");
    } else if (BUILTIN_FUNCS.count(n->text)) {
        auto hdr = HEADER_OF.find(n->text);
        if (!lenient && !partial && hdr != HEADER_OF.end() && !includes.count(hdr->second))
            warn("missing-include", "Missing #include", n, "'" + n->text + "' is declared in <" + hdr->second + "> but that header is not included.",
                 "Add #include <" + hdr->second + "> at the top of the file.", "Medium");
        static const std::set<string> FORMATTED = {"printf", "fprintf", "sprintf", "snprintf", "scanf", "sscanf", "fscanf"};
        if (FORMATTED.count(n->text)) checkFormat(n);
        checkUnsafeCall(n);
    } else if (macros.count(n->text)) {
        // function-like macro: accepted
    } else {
        if (lenient || partial)
            warn("undeclared-function", "Undeclared function", n, "Function '" + n->text + "' is not declared in the part of the file that could be read.",
                 "Declare it, define it, or make sure the header provides it.", "Low");
        else
            err("undeclared-function", "Undeclared function", n, "Function '" + n->text + "' is called but never declared.",
                "Declare it before use, define it, or include the header that provides it (e.g. #include <stdio.h>).", "Medium");
    }
}

// ---------------------------------------------------------------------------------------------
// Format strings: printf / fprintf / sprintf / snprintf and scanf / sscanf / fscanf
// ---------------------------------------------------------------------------------------------

void Semantic::checkFormat(Node* n) {
    const string& fn = n->text;
    bool scan = fn == "scanf" || fn == "sscanf" || fn == "fscanf";
    size_t fi = (fn == "fprintf" || fn == "sprintf" || fn == "sscanf" || fn == "fscanf") ? 1 : fn == "snprintf" ? 2 : 0;
    if (n->kids.size() <= fi) {
        err("arg-count", "Wrong number of arguments", n, "'" + fn + "' needs at least " + std::to_string(fi + 1) + " argument(s) but " + std::to_string(n->kids.size()) + " were given.",
            "Give " + fn + "() a format string, e.g. " + fn + "(\"%d\", value).");
        return;
    }
    Node* f = n->kids[fi].get();
    int given = (int)n->kids.size() - (int)(fi + 1);

    if (f->kind != NK::StringLit) {
        if (!scan && given == 0 && f->kind == NK::Ident)
            warn("format-string", "Format string is not a literal", f,
                 "'" + f->text + "' is used directly as the format of " + fn + "(). If its text contains a '%', " + fn + "() treats it as a placeholder and reads memory that was never passed.",
                 "Write " + fn + "(\"%s\", " + f->text + ");", "Medium");
        return;
    }

    std::vector<Spec> specs = parseFormat(f->text, scan);
    bool anyInvalid = false;
    for (auto& sp : specs) {
        if (sp.valid) continue;
        anyInvalid = true;
        Node at(NK::StringLit, f->line, f->col + (int)sp.pos);
        err("format-string", "Invalid format specifier", &at,
            "The format specifier '" + sp.text + "' in " + fn + "() is not valid: " + sp.problem + ".",
            "After '%' write a conversion letter such as %d (int), %f (double), %s (string) or %c (char). To print a literal percent sign write %%.");
    }
    if (anyInvalid) return;   // the argument positions can no longer be trusted

    int needed = 0;
    for (auto& sp : specs) needed += scan ? (sp.suppress ? 0 : 1) : 1 + sp.stars;
    if (needed != given) {
        string what = scan ? "pointer(s)" : "value(s)";
        if (needed > given)
            err(scan ? "scanf-args" : "printf-args", "Format string mismatch", n,
                "The format string of " + fn + "() has " + std::to_string(needed) + " placeholder(s) and needs " + std::to_string(needed) + " " + what + ", but only " + std::to_string(given) +
                    " were given. The missing ones are read from random memory (undefined behavior).",
                scan ? "Pass &variable for every % conversion in scanf." : "Pass one value for every % placeholder, or remove the extra placeholder(s).");
        else
            warn(scan ? "scanf-args" : "printf-args", "Format string mismatch", n,
                 std::to_string(given) + " " + what + " were given to " + fn + "(), but the format string only has " + std::to_string(needed) + " placeholder(s). The extra argument(s) are ignored.",
                 "Add the missing % placeholder, or remove the unused argument(s).", "Medium");
    }

    size_t ai = fi + 1;
    for (auto& sp : specs) {
        if (!scan) ai += sp.stars;
        if (scan && sp.suppress) continue;
        if (ai >= n->kids.size()) break;
        Node* arg = n->kids[ai++].get();
        TI t = typeOf(arg);
        if (!t.known) continue;
        SC sc = scalarClass(t.base);
        bool scalar = t.ptr == 0 && !t.arr;
        int ep = t.arr ? t.ptr + 1 : t.ptr;   // pointer depth of what the value refers to
        bool charPtr = ep == 1 && sc == SC::Char;
        string a = "'" + exprStr(arg) + "'";
        string tn = typeStr(t.base, t.ptr, t.arr);
        char cv = sp.conv;
        string conv = "%" + sp.len + string(1, cv);

        if (!scan) {
            bool isFloatArg = scalar && (sc == SC::Float || sc == SC::Double || sc == SC::LDouble);
            bool isIntArg = scalar && (sc == SC::Char || sc == SC::Short || sc == SC::Int || sc == SC::Long);
            if (string("diouxXc").find(cv) != string::npos) {
                if (isFloatArg)
                    warn("printf-type", "Format/argument type mismatch", arg, conv + " prints an integer, but " + a + " is a " + tn + ". The value is reinterpreted and the output is wrong.",
                         "Use %f (or %.2f) for " + tn + " values, or cast to int if you only want the integer part.", "High");
                else if (ep > 0 && cv == 'c' && charPtr)
                    warn("printf-type", "Format/argument type mismatch", arg, "%c prints one character, but " + a + " is a string.", "Use %s to print a whole string.", "High");
                else if (ep > 0)
                    warn("printf-type", "Format/argument type mismatch", arg, conv + " expects an integer, but " + a + " is a pointer/string (" + tn + "). It prints the memory address, not the contents.",
                         charPtr ? "Use %s to print text." : "Dereference the pointer (*p) to print the value, or use %p to print the address.", "High");
                else if (isIntArg && cv != 'c' && sc == SC::Long && sp.len.empty())
                    warn("printf-type", "Format/argument type mismatch", arg, a + " is a long, but " + conv + " prints an int. The value can be cut off.", "Use %ld for long values.", "Medium");
                else if (isIntArg && cv != 'c' && sc != SC::Long && sp.len == "l")
                    warn("printf-type", "Format/argument type mismatch", arg, conv + " expects a long, but " + a + " is an " + tn + ". printf reads more bytes than were passed.", "Use %d, or make the variable a long.", "Medium");
            } else if (string("fFeEgGaA").find(cv) != string::npos) {
                if (isIntArg)
                    warn("printf-type", "Format/argument type mismatch", arg, conv + " prints a floating-point number, but " + a + " is an " + tn + ". The output is garbage (often 0.000000).",
                         "Use %d for integers, or cast the value: (double)" + exprStr(arg) + ".", "High");
                else if (ep > 0)
                    warn("printf-type", "Format/argument type mismatch", arg, conv + " expects a number, but " + a + " is a pointer/string (" + tn + ").", "Pass the value itself, not its address.", "High");
            } else if (cv == 's') {
                if (scalar)
                    warn("printf-type", "Format/argument type mismatch", arg, "%s expects a string (char *), but " + a + " is a " + tn + ". The value is used as a memory address and the program will likely crash.",
                         sc == SC::Char ? "A single character is printed with %c." : "Use %d (int) or %f (double) to print numbers.", "High");
                else if (ep >= 1 && !charPtr)
                    warn("printf-type", "Format/argument type mismatch", arg, "%s expects a char string, but " + a + " has type " + tn + ".", "Pass a char array or char *.", "Medium");
            }
            continue;
        }

        // scanf family: every argument must be a pointer to the right kind of variable
        if (cv == 's' || cv == '[') {
            bool ok = charPtr;
            if (scalar)
                warn("scanf-address", "Missing '&' in scanf", arg, "scanf needs a char array or char pointer for " + conv + ", but " + a + " is a plain " + tn + ".", "Pass a char array, e.g. char name[50]; scanf(\"%49s\", name);", "High");
            else if (!ok)
                warn("scanf-type", "Format/argument type mismatch", arg, conv + " stores text, but " + a + " has type " + tn + ", not char * / char[].", "Use a char array to hold the text.", "High");
            else if (cv == 's' && !sp.hasWidth) {
                string w = (t.arr && st.lookup(arg->text) >= 0 && st.all[st.lookup(arg->text)].arrLen > 0) ? std::to_string(st.all[st.lookup(arg->text)].arrLen - 1) : "N";
                warn("unsafe-function", "Unbounded %s in scanf", arg, "%s has no width limit, so input longer than the buffer overflows " + a + ".", "Limit the input: scanf(\"%" + w + "s\", " + exprStr(arg) + ");", "Medium");
            }
            continue;
        }
        if (scalar) {
            warn("scanf-address", "Missing '&' in scanf", arg, "scanf needs the address of the variable for " + conv + ", but " + a + " is passed by value.",
                 arg->kind == NK::Ident ? "Write &" + arg->text + " instead of " + arg->text + "." : "Pass the address of a variable, e.g. &x.", "High");
            continue;
        }
        if (t.arr || t.ptr != 1) continue;
        SC want;
        if (string("diouxX").find(cv) != string::npos) want = sp.len == "hh" ? SC::Char : sp.len == "h" ? SC::Short : (sp.len == "l" || sp.len == "ll") ? SC::Long : SC::Int;
        else if (string("fFeEgGaA").find(cv) != string::npos) want = sp.len == "l" ? SC::Double : sp.len == "L" ? SC::LDouble : SC::Float;
        else if (cv == 'c') want = SC::Char;
        else continue;
        if (sc == SC::Other || sc == want) continue;
        warn("scanf-type", "Format/argument type mismatch", arg,
             conv + " stores a " + scName(want) + ", but " + a + " points to a " + scName(sc) + ". Writing the wrong number of bytes corrupts the variable or the memory next to it.",
             string("For a ") + scName(sc) + " variable use " + scanLetterFor(sc) + (sc == SC::Double ? " (scanf needs %lf for double, unlike printf)" : "") + ", or change the variable's type.", "High");
    }
}

void Semantic::checkUnsafeCall(Node* n) {
    const string& fn = n->text;
    if (fn == "gets") {
        warn("unsafe-function", "Unsafe function: gets", n, "gets() cannot limit how much it reads, so any long input overflows the buffer. It was removed from the C standard (C11).",
             "Use fgets(buffer, sizeof buffer, stdin) instead.", "High");
    } else if (fn == "strcpy" || fn == "strcat") {
        Node* dst = n->kid(0);
        Node* src = n->kid(1);
        if (dst && src && dst->kind == NK::Ident && dst->sym >= 0 && src->kind == NK::StringLit) {
            const Symbol& d = st.all[dst->sym];
            size_t need = litLen(src->text) + 1;
            if (fn == "strcpy" && d.isArray && !d.isParam && d.arrLen >= 0 && (long long)need > d.arrLen) {
                err("buffer-overflow", "Buffer overflow", n,
                    "strcpy copies " + std::to_string(need) + " bytes (" + std::to_string(need - 1) + " characters plus the terminating '\\0') into '" + d.name + "', which only holds " + std::to_string(d.arrLen) + ".",
                    "Make '" + d.name + "' at least " + std::to_string(need) + " bytes long (e.g. char " + d.name + "[" + std::to_string(need) + "];), or copy a shorter string.");
                return;
            }
        }
        info("unsafe-function", "Unchecked copy: " + fn, n, fn + "() copies until it finds a '\\0' and never checks the size of the destination.",
             fn == "strcpy" ? "Prefer snprintf(dest, sizeof dest, \"%s\", src) or strncpy with an explicit terminator." : "Check that the destination has room first, or use snprintf.");
    } else if (fn == "sprintf") {
        info("unsafe-function", "Unchecked write: sprintf", n, "sprintf() writes as much as the result needs, with no check against the size of the buffer.", "Use snprintf(buffer, sizeof buffer, ...).");
    }
}

Semantic::TI Semantic::typeOf(const Node* n) const {
    TI t;
    if (!n) return t;
    switch (n->kind) {
        case NK::IntLit: {
            bool isLong = false;
            for (char ch : n->text) if (ch == 'l' || ch == 'L') isLong = true;
            long long v = strtoll(n->text.c_str(), nullptr, 0);
            if (isLong || v > 2147483647LL || v < -2147483648LL) return {"long", 0, false, true};
            return {"int", 0, false, true};
        }
        case NK::FloatLit: {
            char last = n->text.empty() ? 0 : n->text.back();
            return {last == 'f' || last == 'F' ? "float" : "double", 0, false, true};
        }
        case NK::CharLit: return {"char", 0, false, true};
        case NK::StringLit: return {"char", 1, false, true};
        case NK::Ident: {
            if (n->sym >= 0 && !st.all[n->sym].isFunc) { const Symbol& s = st.all[n->sym]; TI r{s.type, s.ptr, s.isArray, true, s.isArray ? (s.dims > 0 ? s.dims : 1) : 0}; return r; }
            long long v;
            if (n->sym < 0 && macroValue(n->text, macros, v) && n->text != "NULL") return {"int", 0, false, true};
            return t;
        }
        case NK::Call: {
            if (n->sym >= 0) { const Symbol& s = st.all[n->sym]; return {s.type, s.ptr, false, true}; }
            auto it = BUILTIN_RET.find(n->text);
            if (it != BUILTIN_RET.end()) return {it->second.first, it->second.second, false, true};
            return t;
        }
        case NK::Assign: return typeOf(n->kid(0));
        case NK::Cast: return {n->type, n->ptr, false, true};
        case NK::Ternary: return typeOf(n->kid(1));
        case NK::Postfix: return typeOf(n->kid(0));
        case NK::Sizeof: return {"int", 0, false, true};
        case NK::Unary: {
            TI k = typeOf(n->kid(0));
            if (n->text == "!") return {"int", 0, false, true};
            if (!k.known) return t;
            if (n->text == "*") {
                if (k.arr) { k.arr = false; return k; }
                if (k.ptr > 0) { k.ptr--; return k; }
                return t;
            }
            if (n->text == "&") { if (k.arr) { k.arr = false; k.ptr++; } k.ptr++; return k; }
            return k;
        }
        case NK::Index: {
            TI k = typeOf(n->kid(0));
            if (!k.known) return t;
            if (k.arr) { if (k.dims > 1) { k.dims--; return k; } k.arr = false; k.dims = 0; return k; }
            if (k.ptr > 0) { k.ptr--; return k; }
            return t;
        }
        case NK::Binary: {
            const string& o = n->text;
            if (o == "==" || o == "!=" || o == "<" || o == ">" || o == "<=" || o == ">=" || o == "&&" || o == "||") return {"int", 0, false, true};
            TI a = typeOf(n->kid(0)), b = typeOf(n->kid(1));
            if (o == ",") return b;
            if (!a.known || !b.known) return t;
            if (o == "+" || o == "-") {
                if (a.arr) { a.arr = false; a.ptr++; }
                if (b.arr) { b.arr = false; b.ptr++; }
                if (a.ptr > 0 && b.ptr == 0) return a;
                if (b.ptr > 0 && a.ptr == 0 && o == "+") return b;
                if (a.ptr > 0 && b.ptr > 0) return {"int", 0, false, true};
            }
            if (a.ptr > 0 || b.ptr > 0 || a.arr || b.arr) return t;
            return {isFloatBase(a.base) || isFloatBase(b.base) ? "double" : "int", 0, false, true};
        }
        default: return t;
    }
}

void Semantic::checkAssign(const TI& dst, const Node* src, const Node* at, const string& ctx) {
    if (!src || src->kind == NK::InitList) return;
    TI s = typeOf(src);
    if (!dst.known || !s.known) return;
    TI d = dst;
    if (d.arr) { d.arr = false; d.ptr++; }
    if (s.arr) { s.arr = false; s.ptr++; }
    long long cv;
    bool isZero = constEval(src, macros, cv) && cv == 0;
    bool dp = d.ptr > 0, sp = s.ptr > 0;
    string dn = tstr(d.base, d.ptr), sn = tstr(s.base, s.ptr);
    if (!dp && !sp && cat(d.base) == 'F' && src->kind == NK::Binary && src->text == "/") {
        TI a = typeOf(src->kid(0)), b = typeOf(src->kid(1));
        if (a.known && b.known && !a.arr && !b.arr && a.ptr == 0 && b.ptr == 0 && cat(a.base) == 'I' && cat(b.base) == 'I')
            info("integer-division", "Integer division", src, "'" + exprStr(src) + "' divides two integers, so the fractional part is thrown away before the result is stored in the " + dn + ".",
                 "Convert one operand first: (" + d.base + ")" + exprStr(src->kid(0)) + " / " + exprStr(src->kid(1)) + ".");
    }
    if (!dp && !sp && cat(d.base) == 'I' && cat(s.base) == 'I' && constEval(src, macros, cv)) {
        SC dc = scalarClass(d.base);
        bool uns = d.base.find("unsigned") != string::npos || d.base == "size_t" || d.base[0] == 'u';
        long long lo = uns ? 0 : (dc == SC::Char ? -128 : dc == SC::Short ? -32768 : -2147483648LL);
        long long hi = dc == SC::Char ? 255 : dc == SC::Short ? 65535 : dc == SC::Int ? 4294967295LL : (1LL << 62);
        if (dc != SC::Other && dc != SC::Long && (cv < lo || cv > hi))
            warn("constant-overflow", "Constant does not fit", at, ctx + " the value " + std::to_string(cv) + " to '" + dn + "' does not fit: it will be wrapped to a different number.",
                 uns && cv < 0 ? "An unsigned variable cannot hold a negative number; use a signed type." : "Use a larger type (e.g. long) or a smaller value.", "High");
    }
    if (!dp && sp) {
        err("type-mismatch", "Incompatible types", at, ctx + " '" + sn + "' to '" + dn + "' is not allowed: a string or pointer cannot be stored in a plain " + dn + ".",
            "Use a char array or char * for text, or check that you used the right variable.");
    } else if (dp && !sp && !isZero && cat(s.base) != 'V') {
        warn("type-mismatch", "Incompatible types", at, ctx + " an integer ('" + sn + "') to pointer '" + dn + "' without a cast.",
             "Take the address with & or cast explicitly if this is intended.", "Medium");
    } else if (dp && sp && d.ptr == s.ptr && d.base != s.base && d.base != "void" && s.base != "void") {
        warn("type-mismatch", "Incompatible pointer types", at, ctx + " '" + sn + "' to '" + dn + "' mixes different pointer types.",
             "Make both pointers point to the same type, or add an explicit cast.", "Medium");
    } else if (!dp && !sp && cat(d.base) == 'I' && cat(s.base) == 'F') {
        warn("type-mismatch", "Possible loss of precision", at, ctx + " a '" + sn + "' value to '" + dn + "' drops the fractional part.",
             "Use a float/double variable, or add an explicit cast such as (int) if truncation is intended.", "Medium");
    }
}

void Semantic::reportUnused() {
    if (partial) return;   // statements may be missing, so "unused" would be unreliable
    for (auto& s : st.all) {
        if (s.isParam && !s.used && s.scope != "main") {
            Node pat(NK::Ident, s.line, s.col);
            info("unused-parameter", "Unused parameter", &pat, "Parameter '" + s.name + "' of function '" + s.scope + "' is never used.",
                 "Use it, remove it, or (if the signature is fixed) mark it with (void)" + s.name + ";.");
            continue;
        }
        if (s.isFunc || s.isParam || s.isGlobal || s.used) continue;
        Node at(NK::Ident, s.line, s.col);
        string msg = s.assignedLater ? "Variable '" + s.name + "' is assigned a value but its value is never read."
                                     : "Variable '" + s.name + "' is declared but never used.";
        warn("unused-variable", "Unused variable", &at, msg,
             "Remove the variable if it is not needed, or use it in your logic.", "High");
    }
}

void Semantic::checkBinary(Node* n) {
    const string& o = n->text;
    Node* l = n->kid(0);
    Node* r = n->kid(1);
    if (!l || !r) return;
    TI lt = typeOf(l), rt = typeOf(r);
    bool lf = lt.known && lt.ptr == 0 && !lt.arr && cat(lt.base) == 'F';
    bool rf = rt.known && rt.ptr == 0 && !rt.arr && cat(rt.base) == 'F';
    bool cmp = o == "==" || o == "!=" || isCmpOp(o);

    if ((o == "==" || o == "!=") && (lf || rf))
        warn("float-equality", "Comparing floating-point numbers with " + o, n,
             "'" + exprStr(n) + "' compares floating-point values exactly. They are stored rounded (0.1 + 0.2 is not exactly 0.3), so this is often false when it should be true.",
             "Compare with a tolerance: fabs(a - b) < 1e-9.", "Medium");
    if (o == "%" && (lf || rf))
        err("invalid-operands", "Invalid operands to %", n, "The % operator only works on integers, but '" + exprStr(n) + "' uses a floating-point value.", "Use fmod(a, b) from <math.h> for floating-point remainders.");
    if (cmp && l->kind == NK::Binary && !l->paren && (isCmpOp(l->text) || l->text == "==" || l->text == "!=") && (isCmpOp(o) || isCmpOp(l->text)))
        warn("chained-comparison", "Chained comparison", n,
             "'" + exprStr(n) + "' does not test whether the middle value is between the outer ones: '" + exprStr(l) + "' becomes 0 or 1 first, and that is compared with '" + exprStr(r) + "'.",
             "Split it with &&, e.g. (a < b && b < c).", "High");

    long long cv;
    if ((o == "<<" || o == ">>") && constEval(r, macros, cv)) {
        int width = (lt.known && lt.base.find("long") != string::npos) ? 64 : 32;
        if (cv < 0 || cv >= width)
            warn("shift-overflow", "Shift out of range", n, "Shifting by " + std::to_string(cv) + " is undefined: the shift count must be between 0 and " + std::to_string(width - 1) + " for this type.",
                 "Use a shift count inside the width of the type, or a wider type.", "High");
    }

    long long a = 0, b = 0;
    bool ca = constEval(l, macros, a), cb = constEval(r, macros, b);
    if (!(ca && cb)) {
        string x = exprStr(ca ? r : l), e = exprStr(n);
        if (o == "*" && ((cb && b == 0) || (ca && a == 0)))
            warn("redundant-operation", "Result is always 0", n, "'" + e + "' is always 0, whatever the value of '" + x + "' is.", "Replace it with 0, or check that the 0 is really intended.", "High");
        else if (o == "*" && ((cb && b == 1) || (ca && a == 1)))
            info("redundant-operation", "Multiplication by 1", n, "'" + e + "' multiplies by 1, which does not change the value.", "Simplify it to '" + x + "'.");
        else if (o == "+" && ((cb && b == 0) || (ca && a == 0)))
            info("redundant-operation", "Addition of 0", n, "'" + e + "' adds 0, which does not change the value.", "Simplify it to '" + x + "'.");
        else if (o == "-" && cb && b == 0)
            info("redundant-operation", "Subtraction of 0", n, "'" + e + "' subtracts 0, which does not change the value.", "Simplify it to '" + x + "'.");
        else if (o == "/" && cb && b == 1)
            info("redundant-operation", "Division by 1", n, "'" + e + "' divides by 1, which does not change the value.", "Simplify it to '" + x + "'.");
        else if (o == "%" && cb && b == 1)
            warn("redundant-operation", "Result is always 0", n, "'" + e + "' is always 0: every integer is divisible by 1.", "Check the right-hand side of %; you probably meant a larger number.", "High");
        else if (o == "&&" && ((cb && b == 0) || (ca && a == 0)))
            warn("redundant-operation", "Condition is always false", n, "'" + e + "' is always false because one side is the constant 0.", "Remove the constant, or fix the condition.", "High");
        else if (o == "||" && ((cb && b != 0) || (ca && a != 0)))
            warn("redundant-operation", "Condition is always true", n, "'" + e + "' is always true because one side is a non-zero constant.", "Remove the constant, or fix the condition.", "High");
    }
    if (pureExpr(l) && pureExpr(r) && exprStr(l) == exprStr(r) && !(ca && cb)) {
        string x = exprStr(l), e = exprStr(n);
        if (o == "-" || o == "^" || o == "%")
            warn("redundant-operation", "Result is always 0", n, "'" + e + "' is always 0 because both sides are the same value.", "Did you mean a different variable on one side?", "High");
        else if (o == "/")
            info("redundant-operation", "Result is always 1", n, "'" + e + "' is always 1 (and undefined if '" + x + "' is 0).", "Replace it with 1, or use a different variable on one side.");
        else if (o == "==" || o == "<=" || o == ">=")
            warn("redundant-operation", "Condition is always true", n, "'" + e + "' compares a value with itself, so it is always true" + (lf ? " (except for NaN)." : "."), "Did you mean a different variable on one side?", "High");
        else if (o == "!=" || o == "<" || o == ">")
            warn("redundant-operation", "Condition is always false", n, "'" + e + "' compares a value with itself, so it is always false" + (lf ? " (except for NaN)." : "."), "Did you mean a different variable on one side?", "High");
        else if (o == "&&" || o == "||" || o == "&" || o == "|")
            info("redundant-operation", "Repeated operand", n, "'" + e + "' uses '" + x + "' on both sides, so one side is redundant.", "Did you mean a different variable on one side?");
    }
}

// A statement that only computes a value does nothing: "x + 1;", "x == 5;", "5;"
void Semantic::checkStmtEffect(Node* e) {
    if (!e) return;
    switch (e->kind) {
        case NK::Binary:
            if (e->text == ",") return;
            if (e->text == "==")
                warn("no-effect-statement", "Comparison used as a statement", e, "'" + exprStr(e) + ";' compares two values and throws the result away, so it changes nothing.", "If you wanted to assign, write '" + exprStr(e->kid(0)) + " = " + exprStr(e->kid(1)) + ";'.", "High");
            else
                warn("no-effect-statement", "Statement has no effect", e, "'" + exprStr(e) + ";' computes a value but does not store or use it.", "Assign the result to a variable, or remove the statement.", "High");
            break;
        case NK::Ident: case NK::IntLit: case NK::FloatLit: case NK::CharLit: case NK::StringLit: case NK::Index: case NK::Cast:
            warn("no-effect-statement", "Statement has no effect", e, "'" + exprStr(e) + ";' does nothing.", "Remove it, or turn it into a call or an assignment.", "High");
            break;
        case NK::Unary:
            if (e->text == "-" || e->text == "+" || e->text == "!" || e->text == "~" || e->text == "*")
                warn("no-effect-statement", "Statement has no effect", e, "'" + exprStr(e) + ";' computes a value but does not use it.", "Remove it, or assign the result to a variable.", "High");
            break;
        default: break;
    }
}
