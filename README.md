# C-Lint

Compiler-based syntax checker and static analyzer for C programs.

## Run it (Windows, VS Code terminal)

```
npm run setup        # installs everything and compiles the C++ analyzer (needs g++ on PATH)
copy backend\.env.example backend\.env     # optional: add ANTHROPIC_API_KEY for AI Fix
npm run dev          # backend on :5000, website on http://localhost:5173
npm test             # runs the analyzer against tests/c_samples
```

## Layout

| Folder | What it is |
|---|---|
| `analyzer/src` | C++ analyzer: `lexer`, `parser` (recursive descent), `ast`, `symtab`, `semantic`, `flow` (uninitialized / division by zero / zero numerator / array bounds / self-assignment / null / loops), `cfg` (unreachable code, missing return, infinite loops), `main` (JSON output) |
| `backend` | Express + TypeScript API: `POST /api/analyze`, `POST /api/ai-fix` |
| `frontend` | React + TypeScript + Monaco: editor, diagnostics, AST tree, CFG graph, symbol table, tokens, AI Fix |
| `tests` | `c_samples/*.c` with the rule ids each must trigger in `expected/*.json` |

## Pipeline

source -> lexer (tokens) -> parser (AST) -> semantic analysis (symbol table) -> flow analysis + CFG -> diagnostics -> JSON -> website

## AI Fix

`POST /api/ai-fix` sends your code and C-Lint's findings to an AI model. Put **one** key in `backend/.env`
(copy `backend/.env.example`): `ANTHROPIC_API_KEY` (Claude), `OPENAI_API_KEY` or `GEMINI_API_KEY`. Restart `npm run dev` afterwards.
The fix is **re-analyzed by C-Lint**. If errors/warnings remain or new ones appear, the AI gets one more try with that feedback,
and the better of the two answers is shown, together with what was resolved / still present / new.

## What every finding contains

Line and column, severity (error / warning / info), **what is wrong**, **why it is a problem**, **how to fix it**, and a confidence level.

## Supported C subset

int/char/float/double/void/long/short/unsigned, pointers, arrays, functions, if/else, while, do-while, for, return, break, continue,
expressions, casts, `#define` constants. Not supported yet: struct, union, enum, typedef, switch, goto (reported as "unsupported").

## Limits (be honest in the review)

Static analysis cannot find every bug. Infinite-loop detection catches only obvious patterns (halting problem),
uninitialized/null/division checks are path-based approximations, hence the confidence level on each finding.

## What the analyzer checks

Every finding has a line and column, a severity (error / warning / info), a confidence level, **what is wrong**, **why it is a problem** and **how to fix it**.

| Group | Rules |
|---|---|
| Syntax | grammar errors (several per file, parsing continues after each), invalid characters, unsupported constructs |
| Format strings | `format-string` (invalid `%` specifier such as `"%\n"`), `printf-args` / `scanf-args` (wrong number of values), `printf-type` / `scanf-type` (`%d` with a double, `%f` with an int, `%f` for a double in scanf...), `scanf-address` (missing `&`), non-literal format strings |
| Declarations & types | undeclared names / functions, redeclaration, wrong argument count and argument types, return type, incompatible types, constant that does not fit (`char c = 300`), integer division stored in a float, `%` on floats, assignment to an array, `*` or `[]` on a non-pointer, missing `main`, function used before its declaration, declared but never defined |
| Expressions | `=` in a condition, string compared with `==`, float compared with `==`, `a < b < c`, shift out of range, `x * 0`, `x - x`, `x == x`, self-assignment, statements with no effect (`x == 3;`), stray `;` after `if`/`while`/`for` |
| Static analysis | unused variable / parameter, uninitialized variable and pointer, division by zero, zero numerator (`0 / b`), possible division by zero, null dereference, array index out of bounds (also in loops that run one step too far), unreachable code, missing return, infinite loops (also a counter that moves the wrong way), constant conditions, switch fall-through / duplicate case |
| Memory | memory leak, use after free, double free, return of a local variable's address |
| Safety | `gets`, `strcpy`/`sprintf` into a buffer that is too small, `scanf("%s")` without a width |

**Syntax errors and logic errors are now reported together.** When the file has syntax errors, everything that can be checked statement by statement still runs. The checks that need the whole program (flow analysis, CFG: uninitialized variables, unreachable code, missing return, loops, memory) are skipped and the UI says so, because a missing statement would make them report false problems.

No static analyzer can find every bug (for example, the halting problem makes infinite-loop detection impossible in general), so findings that depend on guesses carry a lower confidence level.
