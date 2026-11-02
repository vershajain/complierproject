#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "diagnostics.h"

enum class TokType { Keyword, Ident, IntLit, FloatLit, CharLit, StringLit, Op, End };
struct Token { TokType type; std::string text; int line, col; };
struct Macro { std::string body; bool isFunction = false; };
const char* tokTypeName(TokType t);

class Lexer {
public:
    Lexer(const std::string& source, Diagnostics& diags);
    std::vector<Token> tokenize();
    std::set<std::string> includes;   // header names from #include lines
    bool hasLocalInclude = false;     // #include "file.h" (cannot be read)
    std::map<std::string, Macro> macros;   // collected from #define lines
private:
    const std::string& src;
    Diagnostics& diags;
    size_t pos = 0;
    int line = 1, col = 1;
    bool atLineStart = true;
    char peek(size_t o = 0) const;
    char advance();
    void directive();
    void error(int l, int c, const std::string& msg, const std::string& hint);
};
