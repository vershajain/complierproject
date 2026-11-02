#include "lexer.h"
#include <cctype>
#include <set>

static const std::set<std::string> KEYWORDS = {
    "int", "char", "float", "double", "void", "long", "short", "unsigned", "signed", "const",
    "static", "extern", "volatile", "register", "if", "else", "while", "do", "for", "return",
    "break", "continue", "sizeof", "struct", "union", "enum", "typedef", "switch", "case",
    "default", "goto", "size_t", "bool", "FILE", "ssize_t", "int8_t", "int16_t", "int32_t",
    "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t"};

const char* tokTypeName(TokType t) {
    switch (t) {
        case TokType::Keyword: return "keyword"; case TokType::Ident: return "identifier";
        case TokType::IntLit: return "integer"; case TokType::FloatLit: return "float";
        case TokType::CharLit: return "char"; case TokType::StringLit: return "string";
        case TokType::Op: return "operator"; case TokType::End: return "end";
    }
    return "?";
}

Lexer::Lexer(const std::string& s, Diagnostics& d) : src(s), diags(d) {}

char Lexer::peek(size_t o) const { return pos + o < src.size() ? src[pos + o] : '\0'; }

char Lexer::advance() {
    char c = src[pos++];
    if (c == '\n') { line++; col = 1; atLineStart = true; } else col++;
    return c;
}

void Lexer::error(int l, int c, const std::string& msg, const std::string& hint) {
    diags.add("lex-error", "Lexical error", Severity::Error, l, c, msg, hint, "High", "lexical");
}

// Reads a whole '#...' line. Only '#define' is remembered; the rest (#include...) is skipped.
void Lexer::directive() {
    advance();
    std::string text;
    while (pos < src.size() && peek() != '\n') {
        if (peek() == '\\' && peek(1) == '\n') { advance(); advance(); text += ' '; continue; }
        if (peek() == '\\' && peek(1) == '\r' && peek(2) == '\n') { advance(); advance(); advance(); text += ' '; continue; }
        char c = advance();
        if (c != '\r') text += c;
    }
    size_t i = 0;
    auto skipws = [&] { while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) i++; };
    skipws();
    if (text.compare(i, 7, "include") == 0) {
        i += 7;
        skipws();
        if (i < text.size() && text[i] == '<' && text.find('>', i) != std::string::npos) includes.insert(text.substr(i + 1, text.find('>', i) - i - 1));
        else if (i < text.size() && text[i] == '"' && text.find('"', i + 1) != std::string::npos) hasLocalInclude = true;
        return;
    }
    if (text.compare(i, 6, "define") != 0) return;
    i += 6;
    skipws();
    std::string name;
    while (i < text.size() && (isalnum((unsigned char)text[i]) || text[i] == '_')) name += text[i++];
    if (name.empty()) return;
    Macro m;
    if (i < text.size() && text[i] == '(') {
        m.isFunction = true;
        while (i < text.size() && text[i] != ')') i++;
        if (i < text.size()) i++;
    }
    skipws();
    std::string body = text.substr(i);
    size_t cm = body.find("//");
    if (cm != std::string::npos) body = body.substr(0, cm);
    while (!body.empty() && (body.back() == ' ' || body.back() == '\t')) body.pop_back();
    m.body = body;
    macros[name] = m;
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> toks;
    auto push = [&](TokType t, const std::string& s, int l, int c) { toks.push_back({t, s, l, c}); };
    static const char* OPS3[] = {"<<=", ">>=", "..."};
    static const char* OPS2[] = {"->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
                                 "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^="};
    const std::string singles = "+-*/%=<>!~&|^?:,;(){}[].";

    while (pos < src.size()) {
        char c = peek();
        if (c == '\n' || c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { advance(); continue; }
        if (c == '/' && peek(1) == '/') { while (pos < src.size() && peek() != '\n') advance(); continue; }
        if (c == '/' && peek(1) == '*') {
            int sl = line, sc = col;
            advance(); advance();
            bool closed = false;
            while (pos < src.size()) {
                if (peek() == '*' && peek(1) == '/') { advance(); advance(); closed = true; break; }
                advance();
            }
            if (!closed) error(sl, sc, "Unterminated comment: '/*' is never closed.", "Add '*/' to close the comment.");
            continue;
        }
        if (c == '#' && atLineStart) { directive(); continue; }

        int sl = line, sc = col;
        atLineStart = false;

        if (isalpha((unsigned char)c) || c == '_') {
            std::string t;
            while (isalnum((unsigned char)peek()) || peek() == '_') t += advance();
            push(KEYWORDS.count(t) ? TokType::Keyword : TokType::Ident, t, sl, sc);
            continue;
        }
        if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)peek(1)))) {
            std::string t;
            bool isFloat = false;
            if (c == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                t += advance(); t += advance();
                while (isxdigit((unsigned char)peek())) t += advance();
            } else {
                while (isdigit((unsigned char)peek())) t += advance();
                if (peek() == '.') {
                    isFloat = true; t += advance();
                    while (isdigit((unsigned char)peek())) t += advance();
                }
                if (peek() == 'e' || peek() == 'E') {
                    char n = peek(1);
                    if (isdigit((unsigned char)n) || ((n == '+' || n == '-') && isdigit((unsigned char)peek(2)))) {
                        isFloat = true; t += advance();
                        if (peek() == '+' || peek() == '-') t += advance();
                        while (isdigit((unsigned char)peek())) t += advance();
                    }
                }
            }
            while (peek() == 'u' || peek() == 'U' || peek() == 'l' || peek() == 'L' || (isFloat && (peek() == 'f' || peek() == 'F'))) t += advance();
            if (isalnum((unsigned char)peek()) || peek() == '_') {
                while (isalnum((unsigned char)peek()) || peek() == '_') t += advance();
                error(sl, sc, "Invalid number or identifier '" + t + "'.", "Identifiers cannot start with a digit; check for a typo.");
            }
            push(isFloat ? TokType::FloatLit : TokType::IntLit, t, sl, sc);
            continue;
        }
        if (c == '"' || c == '\'') {
            char q = c;
            std::string t;
            t += advance();
            bool closed = false;
            while (pos < src.size() && peek() != '\n') {
                char d = peek();
                if (d == '\\') { t += advance(); if (pos < src.size() && peek() != '\n') t += advance(); continue; }
                t += advance();
                if (d == q) { closed = true; break; }
            }
            if (!closed)
                error(sl, sc, std::string(q == '"' ? "String" : "Character") + " literal is not closed.",
                      std::string("Add the closing ") + q + " before the end of the line.");
            push(q == '"' ? TokType::StringLit : TokType::CharLit, t, sl, sc);
            continue;
        }
        bool matched = false;
        for (auto o : OPS3) if (src.compare(pos, 3, o) == 0) { advance(); advance(); advance(); push(TokType::Op, o, sl, sc); matched = true; break; }
        if (matched) continue;
        for (auto o : OPS2) if (src.compare(pos, 2, o) == 0) { advance(); advance(); push(TokType::Op, o, sl, sc); matched = true; break; }
        if (matched) continue;
        if (singles.find(c) != std::string::npos) { advance(); push(TokType::Op, std::string(1, c), sl, sc); continue; }
        advance();
        error(sl, sc, std::string("Invalid character '") + c + "'.", "Remove this character; it is not part of the C language.");
    }
    push(TokType::End, "", line, col);
    return toks;
}
