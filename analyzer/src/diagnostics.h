#pragma once
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

enum class Severity { Error, Warning, Info };
inline const char* sevName(Severity s) {
    return s == Severity::Error ? "error" : s == Severity::Warning ? "warning" : "info";
}

struct Diagnostic {
    std::string rule, title;
    Severity severity;
    int line, col;
    std::string message, suggestion, confidence, phase;
    std::string why, category, snippet;   // filled in by the diagnostic engine
};

// Diagnostic engine: every pass reports a raw finding here; the engine enriches it with the
// rule's category, a "why this matters" explanation and the source line, and removes duplicates.
inline const std::pair<const char*, const char*>& ruleInfo(const std::string& rule) {
    static const std::map<std::string, std::pair<const char*, const char*>> C = {
        {"lex-error", {"Lexical", "The lexer could not turn these characters into valid C tokens, so nothing after this point can be trusted."}},
        {"syntax-error", {"Syntax", "The code breaks the grammar of C. A compiler will refuse to build it, and later analysis stages cannot run."}},
        {"unsupported", {"Syntax", "C-Lint analyzes a subset of C. This construct is skipped instead of analyzed."}},
        {"undeclared-identifier", {"Semantic", "A name must be declared before use. Using an unknown name is a compile error and is often a typo."}},
        {"redeclaration", {"Semantic", "Two things with the same name in one scope are ambiguous, so the compiler rejects the second one."}},
        {"type-mismatch", {"Semantic", "Storing a value of one type in another can corrupt data, lose precision, or crash the program."}},
        {"arg-count", {"Semantic", "Calling a function with the wrong number of arguments reads garbage or fails to compile."}},
        {"undeclared-function", {"Semantic", "Calling a function the compiler has not seen is an error in modern C; the call may use the wrong types."}},
        {"implicit-declaration", {"Semantic", "Calling a function before it is declared makes the compiler guess its signature, which can silently pass wrong types."}},
        {"missing-include", {"Semantic", "The function belongs to a standard header you did not include, so the compiler does not know its signature."}},
        {"not-a-function", {"Semantic", "Only functions can be called with parentheses."}},
        {"break-outside-loop", {"Semantic", "break and continue only make sense inside a loop (break also inside a switch)."}},
        {"case-outside-switch", {"Semantic", "case and default labels are only meaningful inside a switch statement."}},
        {"duplicate-case", {"Semantic", "Two case labels with the same value make one of them unreachable and are a compile error."}},
        {"return-type", {"Semantic", "A function must return what its declared type promises; otherwise callers receive garbage."}},
        {"invalid-type", {"Semantic", "This type cannot be used here."}},
        {"assignment-in-condition", {"Logic", "= assigns and == compares. Using = inside a condition is a classic bug that changes the program's behaviour."}},
        {"string-compare", {"Logic", "== on strings compares memory addresses, so two equal texts usually compare as different."}},
        {"printf-args", {"Logic", "printf reads one argument per % placeholder. Too few arguments print garbage or crash."}},
        {"scanf-address", {"Logic", "scanf writes into memory, so it needs the variable's address (&x). Passing the value corrupts memory."}},
        {"unused-variable", {"Code quality", "Unused variables add clutter and often reveal a typo or a forgotten piece of logic."}},
        {"uninitialized-variable", {"Static analysis", "A local variable that was never assigned holds an unpredictable value, so results become random."}},
        {"division-by-zero", {"Static analysis", "Integer division by zero is undefined behaviour and normally crashes the program."}},
        {"potential-division-by-zero", {"Static analysis", "If the divisor can be 0 at run time, the division crashes. Checking it first avoids that."}},
        {"null-dereference", {"Static analysis", "Reading or writing through a NULL pointer crashes the program (segmentation fault)."}},
        {"potential-null-dereference", {"Static analysis", "malloc and fopen return NULL on failure. Using the result without a check crashes when that happens."}},
        {"unreachable-code", {"Static analysis", "Code that can never run is dead weight and usually means an earlier return/break/loop is wrong."}},
        {"infinite-loop", {"Static analysis", "A loop that never ends hangs the program unless it is meant to run forever."}},
        {"missing-return", {"Static analysis", "If a path reaches the end of a non-void function, the caller receives an undefined value."}},
        {"constant-condition", {"Code quality", "A condition that is always true or false means one branch is dead or the check is pointless."}},
        {"implicit-fallthrough", {"Logic", "Without break, a case continues into the next one. This is a very common switch bug."}},
        {"preprocessor-error", {"Preprocessor", "The preprocessor stops or misbehaves on this directive, so the code that follows may not mean what it looks like."}},
        {"preprocessor-warning", {"Preprocessor", "This directive is suspicious or not understood."}},
        {"unknown-header", {"Preprocessor", "C-Lint only knows the standard C headers. An unknown header may be a typo."}},
        {"local-header", {"Preprocessor", "Only the submitted file is analyzed, so declarations from your own headers are unknown. Related errors are reported with lower confidence."}},
        {"macro-redefinition", {"Preprocessor", "Redefining a macro with a different value changes the meaning of the code after that point."}},
        {"zero-numerator", {"Static analysis", "When the numerator is 0 the result is always 0, so the divisor has no effect on the value. The divisor still must not be 0, because dividing by zero is undefined even when the numerator is 0."}},
        {"array-out-of-bounds", {"Static analysis", "Reading or writing outside an array corrupts other memory or crashes (undefined behaviour). C does not check array bounds for you."}},
        {"empty-statement", {"Logic", "A stray semicolon ends the statement, so the code you meant to control runs unconditionally (or the loop does nothing)."}},
        {"format-string", {"Logic", "printf/scanf read the format string character by character. A '%' that is not followed by a valid conversion letter is undefined behaviour and prints or reads garbage."}},
        {"undefined-reference", {"Semantic", "A function that is declared but never defined compiles, but the linker fails with 'undefined reference'."}},
        {"zero-dividend", {"Static analysis", "When the dividend is 0 the result is always 0, so the divisor has no effect on the value. The divisor still must not be 0, because dividing by zero is undefined even when the dividend is 0."}},
        {"redundant-operation", {"Code quality", "The operation does not change the value (or always gives the same value). It usually means a typo, a leftover from editing, or a misunderstanding of what the code computes."}},
        {"self-assignment", {"Logic", "Assigning a variable to itself does nothing. It usually means you meant to assign a different variable."}},
        {"no-effect-statement", {"Logic", "The statement computes a value and throws it away, so it does nothing. It is often a typo, for example == written instead of =."}},
        {"empty-body", {"Logic", "A stray semicolon ends the statement, so the code you meant to control runs unconditionally."}},
        {"array-bounds", {"Static analysis", "Reading or writing outside an array corrupts other memory or crashes (undefined behaviour). C does not check array bounds for you."}},
        {"invalid-array-size", {"Semantic", "An array needs a positive size."}},
        {"buffer-overflow", {"Static analysis", "Copying more bytes than the buffer holds overwrites neighbouring memory. This is a classic crash and security bug."}},
        {"unsafe-function", {"Security", "This function cannot limit how much data it writes, so long input overflows the buffer."}},
        {"printf-type", {"Logic", "printf trusts the format letter. A mismatch with the argument's real type prints garbage or crashes."}},
        {"scanf-type", {"Logic", "scanf writes through the pointer using the size given by the format letter. A mismatch corrupts the variable or neighbouring memory."}},
        {"scanf-args", {"Logic", "scanf needs one pointer per % conversion. Missing pointers make it write to random memory."}},
        {"no-return-value-used", {"Code quality", "The returned value is ignored."}},
        {"memory-leak", {"Static analysis", "Memory from malloc that is never freed stays reserved until the program ends. In long-running programs this exhausts memory."}},
        {"use-after-free", {"Static analysis", "After free() the memory belongs to the system. Using the pointer reads or writes memory that may already be reused (undefined behaviour)."}},
        {"double-free", {"Static analysis", "Freeing the same block twice corrupts the memory allocator and usually crashes."}},
        {"uninitialized-pointer", {"Static analysis", "A pointer that was never assigned points to a random address. Using it crashes or corrupts memory (a 'wild pointer')."}},
        {"return-local-address", {"Static analysis", "Local variables are destroyed when the function returns, so the returned address points to memory that is no longer valid."}},
        {"float-equality", {"Logic", "Floating-point numbers are rounded, so values that look equal (like 0.1 + 0.2 and 0.3) usually compare as different."}},
        {"chained-comparison", {"Logic", "a < b < c does not mean 'b is between a and c'. It evaluates (a < b) first, giving 0 or 1, and compares that with c."}},
        {"invalid-operands", {"Semantic", "The operator cannot be applied to operands of these types, so the compiler rejects it."}},
        {"invalid-assignment", {"Semantic", "This kind of target cannot be assigned to."}},
        {"shift-overflow", {"Static analysis", "Shifting by a negative amount or by the number of bits in the type or more is undefined behaviour."}},
        {"constant-overflow", {"Static analysis", "The value does not fit in the variable's type, so it is silently wrapped or truncated into a different number."}},
        {"integer-division", {"Logic", "Dividing two integers discards the fractional part before the result is stored, even if the target is a float."}},
        {"shadowed-variable", {"Code quality", "A variable with the same name hides the outer one inside this scope. It is easy to read or change the wrong variable by mistake."}},
        {"unused-parameter", {"Code quality", "A parameter that is never used may mean the function ignores part of its input or has a wrong signature."}},
        {"switch-no-default", {"Code quality", "Without a default case, unexpected values are silently ignored."}},
        {"missing-main", {"Semantic", "Every C program starts in main(). Without it the program cannot be linked or run."}},
        {"main-return-type", {"Semantic", "The C standard says main returns int (the exit status). Other types are non-portable."}},
        {"unbalanced-conditional", {"Preprocessor", "#if/#else/#endif must be properly paired."}},
    };
    static const std::pair<const char*, const char*> none = {"General", ""};
    auto it = C.find(rule);
    return it == C.end() ? none : it->second;
}

// The wording the project already used for these rules. It takes priority over the generic catalog below.
inline const std::string* userWhy(const std::string& ruleIn) {
    static const std::map<std::string, std::string> w = {
        {"lex-error", "The compiler cannot even read this character, so the program cannot be compiled at all."},
        {"syntax", "The code breaks the grammar of C, so a compiler will refuse to build it. Later checks are skipped until this is fixed."},
        {"unsupported", "C-Lint only understands a subset of C. This construct is skipped, so problems inside it may go unreported."},
        {"redeclaration", "Declaring the same name twice in one scope is a compile error and makes it unclear which variable or function is meant."},
        {"invalid-type", "A variable needs a real size in memory; 'void' has none, so the compiler rejects it."},
        {"undeclared-identifier", "The compiler does not know this name. Using it will fail to compile (or silently pick the wrong thing if there is a typo)."},
        {"undeclared-function", "Calling a function that was never declared can compile with wrong assumptions about its arguments and return value, causing crashes or wrong results."},
        {"not-a-function", "Only functions can be called with (...). Calling a variable is a compile error."},
        {"arg-count", "Passing too few or too many arguments to a function is a compile error, or leaves parameters with garbage values."},
        {"return-type", "The returned value does not match the function's declared type, so the caller can receive a wrong or meaningless value."},
        {"break-outside-loop", "'break' and 'continue' only make sense inside a loop (break also in switch). Elsewhere they are compile errors."},
        {"type-mismatch", "Mixing incompatible types can lose data (e.g. float to int drops decimals, big to small types overflow) or is not allowed by C at all."},
        {"assignment-in-condition", "'=' stores a value, '==' compares. Using '=' inside a condition overwrites the variable and the condition no longer tests what you meant."},
        {"string-compare", "Comparing char pointers with == compares memory addresses, not the text, so equal strings usually compare as different."},
        {"printf-args", "printf reads its arguments according to the format string. A mismatch in count or type prints garbage or can crash the program."},
        {"scanf-address", "scanf must be given the ADDRESS of the variable to fill. Passing the variable itself writes to a random memory location and can crash."},
        {"unused-variable", "A variable that is never used is dead code: it wastes space, confuses readers and often hides a typo or a forgotten step."},
        {"uninitialized-variable", "A local variable that was never given a value holds random leftover memory. Reading it gives unpredictable results that change between runs."},
        {"division-by-zero", "Dividing an integer by zero is undefined behavior in C: the program usually crashes (or the compiler may produce nonsense results)."},
        {"potential-division-by-zero", "If this value is ever 0 at runtime the division is undefined behavior and normally crashes the program. The analyzer cannot prove it is non-zero."},
        {"zero-numerator", "When the numerator is 0 the result is always 0 (for any non-zero divisor), so the divisor does nothing except introduce a possible crash if it is 0."},
        {"null-dereference", "Reading or writing through a NULL pointer crashes the program (segmentation fault)."},
        {"potential-null-dereference", "malloc, calloc, realloc and fopen return NULL on failure. Using the result without checking can crash the program exactly when resources run low."},
        {"array-out-of-bounds", "Accessing memory outside the array is undefined behavior: it can overwrite other variables, corrupt data, or crash, and bugs may appear far from the cause."},
        {"constant-condition", "A condition that is always true or always false means one branch can never run, which usually points to a logic mistake."},
        {"unreachable-code", "Code after return, break, continue or exit() can never run, so it is dead code and probably not what you intended."},
        {"missing-return", "A non-void function that reaches its end without returning gives the caller a garbage value, which is undefined behavior if it is used."},
        {"infinite-loop", "The loop can never finish, so the program hangs (or the code after it can never run)."},
        {"empty-statement", "A stray ';' directly after if/while/for makes that statement do nothing, and the block that follows always runs. This is a classic bug."},
        {"self-assignment", "Assigning a variable to itself changes nothing. It is normally a typo where a different variable or value was meant."},
        {"shadowed-variable", "The inner variable hides the outer one with the same name. Changes affect only the inner copy, which is a common source of confusing bugs."},
    };
    auto it = w.find(ruleIn == "syntax-error" ? "syntax" : ruleIn);
    return it == w.end() ? nullptr : &it->second;
}

class Diagnostics {
public:
    std::vector<Diagnostic> items;

    void setSource(const std::string& src) {
        lines.clear();
        std::string cur;
        for (char c : src) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur += c;
        }
        lines.push_back(cur);
    }

    void add(const std::string& rule, const std::string& title, Severity sev, int line, int col,
             const std::string& message, const std::string& suggestion,
             const std::string& confidence, const std::string& phase) {
        for (auto& d : items)
            if (d.rule == rule && d.line == line && d.col == col && d.message == message) return;
        Diagnostic d{rule, title, sev, line, col, message, suggestion, confidence, phase, "", "", ""};
        const auto& info = ruleInfo(rule);
        d.category = info.first;
        const std::string* uw = userWhy(rule);
        d.why = uw ? *uw : std::string(info.second);
        if (line >= 1 && (size_t)line <= lines.size()) d.snippet = lines[line - 1];
        items.push_back(d);
    }
    int count(Severity s) const {
        int n = 0;
        for (auto& d : items) if (d.severity == s) n++;
        return n;
    }
    int countPhase(const std::string& phase, Severity s) const {
        int n = 0;
        for (auto& d : items) if (d.phase == phase && d.severity == s) n++;
        return n;
    }
    void sort() {
        std::stable_sort(items.begin(), items.end(), [](const Diagnostic& a, const Diagnostic& b) {
            return a.line != b.line ? a.line < b.line : a.col < b.col;
        });
    }
private:
    std::vector<std::string> lines;
};
