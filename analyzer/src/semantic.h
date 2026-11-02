#pragma once
#include <map>
#include <set>
#include <string>
#include "ast.h"
#include "diagnostics.h"
#include "lexer.h"
#include "symtab.h"

// Semantic analysis: builds the symbol table and checks that the program "makes sense".
class Semantic {
public:
    Semantic(Diagnostics& d, SymbolTable& s, const std::map<std::string, Macro>& m) : diags(d), st(s), macros(m) {}
    void run(Node* program);
    void setIncludes(const std::set<std::string>& inc) { includes = inc; }
    void setLenient(bool v) { lenient = v; }   // a local #include exists: unknown names may come from it
    void setPartial(bool v) { partial = v; }   // the program has syntax errors: the tree is incomplete

private:
    struct TI { std::string base; int ptr = 0; bool arr = false; bool known = false; int dims = 0; };
    Diagnostics& diags;
    SymbolTable& st;
    const std::map<std::string, Macro>& macros;
    Node* curFn = nullptr;
    int loopDepth = 0, switchDepth = 0;
    std::set<std::string> includes;
    bool lenient = false, partial = false;

    void err(const std::string& rule, const std::string& title, const Node* at, const std::string& msg, const std::string& sug, const std::string& conf = "High");
    void warn(const std::string& rule, const std::string& title, const Node* at, const std::string& msg, const std::string& sug, const std::string& conf = "High");
    std::string scopeLabel() const;
    void declareFunction(Node& n);
    void visitFunction(Node& n);
    void visitVarDecl(Node& n, bool global);
    void visitStmt(Node* n);
    void visitCond(Node* c);
    void visitExpr(Node* n, bool read = true);
    void visitIdent(Node* n, bool read);
    void visitCall(Node* n);
    void info(const std::string& rule, const std::string& title, const Node* at, const std::string& msg, const std::string& sug, const std::string& conf = "Medium");
    void checkBinary(Node* n);
    void checkStmtEffect(Node* e);
    void checkFormat(Node* n);
    void checkUnsafeCall(Node* n);
    TI typeOf(const Node* n) const;
    void checkAssign(const TI& dst, const Node* src, const Node* at, const std::string& ctx);
    void reportUnused();
};
