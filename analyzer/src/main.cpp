// C-Lint analyzer: reads a C file, runs the full pipeline and prints one JSON document.
#include <fstream>
#include <iostream>
#include <algorithm>
#include <set>
#include <sstream>
#include "ast.h"
#include "cfg.h"
#include "diagnostics.h"
#include "flow.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "symtab.h"

int main(int argc, char* argv[]) {
    if (argc < 2) { std::cerr << "Usage: clint <file.c>\n"; return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::cerr << "Cannot open file: " << argv[1] << "\n"; return 2; }
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string src = buf.str();

    Diagnostics diags;
    diags.setSource(src);
    Lexer lexer(src, diags);
    std::vector<Token> tokens = lexer.tokenize();

    // Parse even when the lexer found problems, so syntax errors elsewhere are still reported.
    Parser parser(tokens, diags);
    NodePtr program = parser.parseProgram();
    // A bad character usually causes a syntax error on the same line; keep only the real cause.
    {
        std::set<int> lexLines;
        for (auto& d : diags.items) if (d.phase == "lexical") lexLines.insert(d.line);
        auto& v = diags.items;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const Diagnostic& d) { return d.phase == "syntax" && lexLines.count(d.line); }), v.end());
    }

    // With syntax errors the tree is incomplete. Checks that look at single statements still run
    // (semantic analysis); checks that need the whole program (flow, CFG) are skipped.
    bool broken = diags.countPhase("lexical", Severity::Error) + diags.countPhase("syntax", Severity::Error) > 0;
    SymbolTable symbols;
    std::vector<Cfg> cfgs;
    {
        Semantic sem(diags, symbols, lexer.macros);
        sem.setIncludes(lexer.includes);
        sem.setLenient(lexer.hasLocalInclude);
        sem.setPartial(broken);
        sem.run(program.get());
    }
    if (!broken) {
        Flow(diags, symbols, lexer.macros).run(program.get());
        for (auto& k : program->kids) {
            if (k->kind != NK::Function || !k->hasBody) continue;
            Cfg g = buildCfg(k.get(), lexer.macros);
            analyzeCfg(g, k.get(), diags);
            cfgs.push_back(std::move(g));
        }
    }
    diags.sort();

    int errors = diags.count(Severity::Error), warnings = diags.count(Severity::Warning), infos = diags.count(Severity::Info);
    int lines = 1;
    for (char c : src) if (c == '\n') lines++;
    int score = 100 - 15 * errors - 5 * warnings - infos;
    if (score < 0) score = 0;

    std::ostringstream o;
    o << "{\"success\":" << (broken ? "false" : "true")
      << ",\"summary\":{\"errors\":" << errors << ",\"warnings\":" << warnings << ",\"infos\":" << infos
      << ",\"lines\":" << lines << ",\"tokens\":" << tokens.size() - 1 << ",\"score\":" << score << "}";

    o << ",\"diagnostics\":[";
    for (size_t i = 0; i < diags.items.size(); i++) {
        const Diagnostic& d = diags.items[i];
        o << (i ? "," : "") << "{\"rule\":\"" << d.rule << "\",\"title\":\"" << jsonEscape(d.title)
          << "\",\"severity\":\"" << sevName(d.severity) << "\",\"line\":" << d.line << ",\"col\":" << d.col
          << ",\"message\":\"" << jsonEscape(d.message) << "\",\"suggestion\":\"" << jsonEscape(d.suggestion)
          << "\",\"confidence\":\"" << d.confidence << "\",\"phase\":\"" << d.phase
          << "\",\"category\":\"" << jsonEscape(d.category) << "\",\"why\":\"" << jsonEscape(d.why)
          << "\",\"snippet\":\"" << jsonEscape(d.snippet) << "\"}";
    }
    o << "],\"tokens\":[";
    size_t limit = std::min<size_t>(tokens.size() - 1, 3000);
    for (size_t i = 0; i < limit; i++)
        o << (i ? "," : "") << "{\"type\":\"" << tokTypeName(tokens[i].type) << "\",\"text\":\"" << jsonEscape(tokens[i].text)
          << "\",\"line\":" << tokens[i].line << ",\"col\":" << tokens[i].col << "}";
    o << "],\"symbols\":[";
    for (size_t i = 0; i < symbols.all.size(); i++) {
        const Symbol& s = symbols.all[i];
        o << (i ? "," : "") << "{\"name\":\"" << jsonEscape(s.name) << "\",\"type\":\"" << jsonEscape(typeStr(s.type, s.ptr, s.isArray))
          << "\",\"kind\":\"" << (s.isFunc ? "function" : s.isParam ? "parameter" : s.isArray ? "array" : "variable")
          << "\",\"scope\":\"" << jsonEscape(s.scope) << "\",\"line\":" << s.line << ",\"col\":" << s.col
          << ",\"initialized\":" << (s.hasInit ? "true" : "false") << ",\"used\":" << (s.used || s.isFunc ? "true" : "false") << "}";
    }
    o << "],\"ast\":";
    if (program->kids.empty() && broken) o << "null"; else astToJson(program.get(), o);
    o << ",\"cfgs\":[";
    for (size_t i = 0; i < cfgs.size(); i++) { if (i) o << ","; cfgToJson(cfgs[i], o); }
    o << "]}";
    std::cout << o.str() << "\n";
    return 0;
}
