#pragma once
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct Symbol {
    int id = 0;
    std::string name, type, scope;
    int ptr = 0;
    bool isArray = false, isFunc = false, isParam = false, isGlobal = false;
    bool hasBody = false, variadic = false;
    bool hasInit = false, used = false, assignedLater = false, addrTaken = false;
    int line = 0, col = 0, paramCount = -1;
    std::vector<std::pair<std::string, int>> paramTypes;   // (base type, pointer depth) of each parameter
    int dims = 0;
    long long arrLen = -1;   // declared length of a 1-D array when known (else -1)
};

// Stack of scopes. All symbols stay in `all` so they can be listed after analysis.
class SymbolTable {
public:
    std::vector<Symbol> all;
    SymbolTable() { push(); }
    void push() { scopes.emplace_back(); }
    void pop() { scopes.pop_back(); }
    int depth() const { return (int)scopes.size(); }
    int lookup(const std::string& n) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(n);
            if (f != it->end()) return f->second;
        }
        return -1;
    }
    int lookupCurrent(const std::string& n) const {
        auto f = scopes.back().find(n);
        return f == scopes.back().end() ? -1 : f->second;
    }
    int declare(Symbol s) {
        s.id = (int)all.size();
        all.push_back(s);
        scopes.back()[s.name] = s.id;
        return s.id;
    }
    std::vector<std::string> visibleNames() const {
        std::vector<std::string> v;
        for (auto& sc : scopes) for (auto& kv : sc) v.push_back(kv.first);
        return v;
    }
private:
    std::vector<std::unordered_map<std::string, int>> scopes;
};
