#pragma once
#include <iosfwd>
#include <map>
#include <string>
#include <vector>
#include "ast.h"
#include "diagnostics.h"
#include "lexer.h"

struct CfgEdge { int to; std::string label; };
struct CfgBlock {
    int id = 0;
    std::string title;
    std::vector<std::string> lines;   // statements in the block, as text
    std::vector<Node*> stmts;
    std::vector<CfgEdge> succ;
    bool reachable = false;
    int line = 0;
};
struct LoopInfo { Node* node; int header, after, last; bool constTrue; };
struct Cfg {
    std::string function;
    int line = 0;
    std::vector<CfgBlock> blocks;
    int entry = 0, exit = 1, fallBlock = -1;
    std::vector<LoopInfo> loops;
    std::vector<std::pair<int, Node*>> fallthroughs;   // (block that falls through, next case label)
};

Cfg buildCfg(Node* fn, const std::map<std::string, Macro>& macros);
void analyzeCfg(Cfg& g, Node* fn, Diagnostics& d);   // reachability + unreachable/missing-return/infinite-loop rules
void cfgToJson(const Cfg& g, std::ostream& os);
