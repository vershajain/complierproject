#pragma once
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

enum class NK {
    Program, Function, Param, Block, DeclList, VarDecl, InitList,
    If, While, DoWhile, For, Switch, Case, Default, Return, Break, Continue, ExprStmt, Empty,
    Binary, Unary, Postfix, Assign, Call, Index, Ident, IntLit, FloatLit,
    CharLit, StringLit, Cast, Ternary, Sizeof
};

struct Node;
using NodePtr = std::unique_ptr<Node>;

// One generic node type keeps the tree easy to build, walk and print.
struct Node {
    NK kind;
    std::string text;   // operator, identifier name or literal text
    std::string type;   // declared / cast type (base name, no stars)
    int ptr = 0;        // pointer depth of `type`
    bool isArray = false, hasBody = false, variadic = false, paren = false;
    int line = 0, col = 0, endLine = 0, endCol = 0;
    int sym = -1;       // symbol-table id, filled by the semantic pass
    int dims = 0;          // number of array dimensions in the declaration
    long long arrLen = -1; // declared length of a 1-D array when known (else -1)
    std::vector<NodePtr> kids;
    Node(NK k, int l, int c) : kind(k), line(l), col(c) {}
    Node* kid(size_t i) const { return i < kids.size() ? kids[i].get() : nullptr; }
};

const char* nkName(NK k);
std::string typeStr(const std::string& base, int ptr, bool arr = false);
std::string exprStr(const Node* n);   // pretty-prints an expression
std::string declStr(const Node* n);   // pretty-prints a VarDecl
std::string jsonEscape(const std::string& s);
void astToJson(const Node* n, std::ostream& os);
