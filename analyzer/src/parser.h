#pragma once
#include "ast.h"
#include "diagnostics.h"
#include "lexer.h"

// Recursive-descent parser: one function per grammar rule.
class Parser {
public:
    Parser(std::vector<Token>& tokens, Diagnostics& diags);
    NodePtr parseProgram();
private:
    struct ParseError { std::string msg, hint, rule; int line, col; };
    std::vector<Token>& toks;
    Diagnostics& diags;
    size_t p = 0;
    int errorCount = 0;

    const Token& cur() const;
    bool is(const char* t) const;
    bool isAt(size_t off, const char* t) const;
    bool isTypeStart() const;
    bool isTypeAt(size_t off) const;
    bool isUnsupported() const;
    bool accept(const char* t);
    std::string desc() const;
    [[noreturn]] void fail(const std::string& msg, const std::string& hint, int line, int col,
                           const std::string& rule = "syntax-error") const;
    [[noreturn]] void failUnsupported() const;
    void expect(const char* t, const std::string& what, const std::string& hint = "");
    void report(const ParseError& e);
    void sync();
    void skipConstruct();
    NodePtr mk(NK k, const Token& t) const;

    void parseExternal(Node& prog);
    void parseParams(Node& fn);
    std::string parseTypeSpec();
    bool parseDeclarators(const std::string& base, std::vector<NodePtr>& out);   // false = recovered from an error
    void skipToSemicolon();
    NodePtr parseInit();
    NodePtr parseBlock();
    void parseStatementInto(Node& parent);
    NodePtr parseStatement();
    NodePtr parseExpr();
    NodePtr parseAssign();
    NodePtr parseTernary();
    NodePtr parseBinary(int minPrec);
    NodePtr parseUnary();
    NodePtr parsePostfix();
    NodePtr parsePrimary();
};
