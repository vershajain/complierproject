#pragma once
#include <map>
#include <set>
#include <string>
#include "ast.h"
#include "diagnostics.h"
#include "lexer.h"
#include "symtab.h"

// Walks each function in execution order, tracking what is known at every point:
// which variables are initialized, which hold a known constant, which are known non-zero,
// and which pointers might be NULL. Used for: uninitialized variables, division by zero,
// null-pointer dereference, constant conditions and loops that make no progress.
class Flow {
public:
    Flow(Diagnostics& d, SymbolTable& s, const std::map<std::string, Macro>& m) : diags(d), st(s), macros(m) {}
    void run(Node* program);

private:
    struct State {
        bool dead = false;                 // code after return/break/exit()
        std::set<int> init, nz, maybeNull; // definitely-initialized / known non-zero / may be NULL
        std::set<int> freed, freedMay;     // pointers freed on every path / on at least one path
        std::map<int, long long> consts;   // variables with a known constant value
    };
    Diagnostics& diags;
    SymbolTable& st;
    const std::map<std::string, Macro>& macros;
    std::set<int> everAssigned, reported;
    std::vector<State> brk;   // states reaching a `break`, one entry per enclosing loop/switch
    std::map<int, const Node*> allocSites;   // pointer variable -> its malloc/calloc/realloc call
    std::map<int, int> freeLine;             // pointer variable -> line of its free()

    static State join(const State& a, const State& b);
    void rep(const std::string& rule, const std::string& title, Severity sev, const Node* at, const std::string& msg, const std::string& sug, const std::string& conf);
    bool constS(const Node* n, const State& s, long long& v);
    bool isFloaty(const Node* n);
    void stmt(Node* n, State& s);
    void expr(Node* n, State& s);
    void assign(Node* n, State& s);
    void readIdent(Node* n, State& s);
    void setValue(int sym, Node* rhs, State& s);
    void invalidate(Node* k, State& s);
    void derefCheck(Node* base, State& s);
    void boundsCheck(Node* idx, State& s);
    void checkDivisorNode(Node* divisor, Node* at, State& s);
    bool splitCmp(const Node* b, const State& s, int& sym, std::string& op, long long& c);
    void applyFacts(Node* c, bool truth, State& s);
    void kill(State& s, const std::set<int>& vars);
    void checkLoopDirection(Node* loop, State& s);
    void leakCheck(Node* fn);
    static void scanEscapes(Node* n, Node* parent, const std::map<int, const Node*>& sites, std::set<int>& freed, std::set<int>& escaped);
    void checkNoProgress(Node* loop, Node* cond, Node* body, Node* incr);
    static void collectAssigned(const Node* n, std::set<int>& out);
    static void collectIdents(const Node* n, std::set<int>& out);
    static bool hasKind(const Node* n, NK k);
    static bool hasCall(const Node* n);
    static bool hasExitCall(const Node* n);
};
