# AI-First Source Formatting

This document describes the intended source-formatting ideology for the native formatter used by `format.cmd`.

The formatter leaves minimal freedom to authors. Line breaks, indentation, wrapping, and grouping are tool decisions, and authors should not hand-shape code to create visual layout. Each run computes layout from syntax and these rules; previous ordinary line breaks are not honored. Source blank-line separators and comments are the deliberate semantic escape hatch: use comments to explain intent or add empty lines to split conceptual groups when the formatter's structural grouping and blank-line separators are not enough.

## Goals

- Keep formatting deterministic enough that agents and humans do not spend review time on local style choices.
- Keep formatting simple to implement natively instead of cloning clang-format's option surface.
- Preserve the project readability rules: hard 120-column width, 4-space indentation, no tabs, and no vertical alignment.
- Let syntax structure drive wrapping and indentation.

## Ownership

- `format.cmd` owns the maintained formatting entrypoint and delegates to `CaseDashTools.exe format`.
- `CaseDashTools.exe format` owns native formatter execution, source discovery, parsing, checking, fixing, and stdout rendering.
- `src\tools\format.cpp` owns native formatting logic and hardcoded style behavior.
- `tools\format_config.json` owns project constants and parser support data, including line width, indent width, tab width, macro names accepted by the grammar, and include-sorting group definitions. It does not own style choices such as wrapping shapes or spacing rules.
- `src\tools\vendor\tree-sitter\` owns the vendored tree-sitter C and C++ grammar inputs plus generated C++ parser sources compiled into `CaseDashTools.exe`.
- `tools\regenerate_tree_sitter_grammar.py` owns explicit regeneration of vendored tree-sitter C++ grammar outputs from the vendored grammar source and `tools\format_config.json`. Normal builds do not regenerate grammar files.

## Parser Grammar

The native formatter uses tree-sitter core from vcpkg and a vendored C++ grammar under `src\tools\vendor\tree-sitter\tree-sitter-cpp\`. The C grammar under `src\tools\vendor\tree-sitter\tree-sitter-c\` is kept as the C++ grammar dependency for provenance and regeneration.

Regenerate parser outputs only as an explicit maintenance step after editing the vendored grammar source or parser macro categories:

```bat
python tools\regenerate_tree_sitter_grammar.py
```

The tool writes `case_dash_macro_config.js` from `tools\format_config.json`, runs the pinned tree-sitter CLI, and updates generated files under `src\tools\vendor\tree-sitter\tree-sitter-cpp\src\`. Pass `--tree-sitter-cli <path>` to use an existing CLI; otherwise the tool downloads the pinned Windows CLI under `build\`.

## General Rules

- Use the configured 4 spaces for each indent level. Tabs are never emitted; the configured tab width is only for measuring existing tab characters before they are replaced.
- Do not increase indentation for namespace bodies. A namespace declaration owns braces and blank-line grouping, but declarations inside it stay at the same indentation level as the namespace declaration.
- Separate namespace opening lines and namespace closing braces from neighboring declarations with one empty line.
- Remove trailing whitespace from every line, including comment-only and trailing-comment lines.
- Preserve the source token sequence except for include sorting. Outside include sorting, the formatter only changes spaces and line breaks, and never adds or removes braces by itself.
- Preserve one source blank line when it separates already closed declarations or statements at the same structural level, including inside function, method, and lambda bodies. Multiple source blank lines collapse to one. Blank lines at the beginning of a block, class, struct, enum, namespace, or immediately before its closing brace are not preserved.
- Never vertically align tokens, comments, assignments, parameters, ternary arms, or consecutive declarations.
- Enforce the configured hard 120-column line width for generated code whenever the formatter can safely break the syntax.
- Do not break string literals, character literals, numeric constants, or comments only to satisfy the line width.
- Preserve comments in place. The formatter may change spaces and line breaks around comments, and may trim trailing whitespace from comment lines, but it does not move comments across code tokens or reflow comment text.
- Keep a `//` comment that starts on the line after a declaration as a standalone comment line. Only a `//` comment that is physically on the same source line as a statement or declaration may become that line's trailing comment.
- Insert one empty line between top-level logical groups such as classes, free functions, namespace-level declarations, and method implementations.
- Insert one empty line between neighboring `class`, `struct`, and `enum class` declarations and their sibling declarations. Non-empty function definitions are also separated from neighboring declarations by one empty line.
- Insert one empty line between neighboring declarations of different kinds. Fields, methods, `class` declarations, `struct` declarations, `enum` declarations, namespace declarations, and macro definitions are separate kinds. Consecutive single-line fields and consecutive macro definitions may stay grouped.
- Separate a multi-line field declaration from neighboring declarations with one empty line, even when the neighbors are also fields.
- Separate fields from neighboring methods by one empty line, including empty inline method definitions, defaulted or deleted method declarations, pure virtual declarations, and method declarations without bodies. Trailing comments on fields do not change the separation rule.
- Keep consecutive method declarations in one method group without a structural blank line, including constructors, destructors, conversion functions, and operator overloads ending in `= default`, `= delete`, or `= 0`.
- Apply declaration-separation blank lines only in declaration scopes: top level, namespaces, classes, structs, and enums. Function, method, and lambda bodies are executable scopes and do not get declaration-separation blank lines.
- Insert required structural blank lines even when the source omits them. Preserve optional source blank-line separators only where the separator rule allows them.
- Put block-opening braces for code blocks at the end of the introducing line, then line break immediately after the brace.
- Treat a standalone braced statement block as a code block. Its opening brace and closing brace occupy their own structural lines, and the closing brace does not attach to the following statement.
- Keep empty braces as `{}`.
- Treat empty delimiter pairs as atomic text. The formatter never breaks inside empty `()`, empty `{}`, or empty `<>`; it breaks an owning expression or chain instead.
- Do not force a line break after an initializer-list opening brace unless the initializer list itself wraps.
- Format enum bodies as structural comma lists with one top-level enumerator per line. The closing brace stays on its own line, and a source trailing comma remains attached to the final enumerator.
- When a class, struct, or enum body is followed by a declarator for the type just declared, keep the declarator attached to the closing brace as one declaration: `struct Context { ... } context{...};`.
- Line break after a code-block closing brace, except that same-statement continuation forms such as `else`, `catch`, and only the `while` that closes a do-while statement stay on the same line as the closing brace. A separate `while` statement after an `if` block starts on its own line.
- Line break after each statement-terminating semicolon.
- Keep preprocessor directives at column zero. Macro continuation backslashes, spaces before continuation backslashes, and continuation newlines are formatter-owned layout. For multi-line macro definitions, normalize the replacement list as code, collapse it onto one indented continuation line when it fits, and insert continuation backslashes only on emitted macro lines that continue onto another emitted macro line.
- Put one empty line after `#pragma once` before the next include, preprocessor directive, comment, or declaration.
- Put one empty line before and after each `#undef` directive. `#undef` directives are cleanup boundaries, not part of consecutive macro-definition groups.
- Single-line lambda bodies are supported when the complete lambda fits the configured line width. A lambda body that does not fit splits after `{`, formats its body as normal code indented one level deeper, and closes on its own line.
- When a wrapped lambda is assigned to a variable, keep the assignment prefix and lambda opener on the same line if the complete opener fits. If the opener does not fit but the assignment prefix plus `[` fits, keep `= [` on the assignment line and split the capture list like a multi-line function declaration without adding a second continuation indent.

## Namespaces

Namespaces are grouping syntax, not indentation syntax. The formatter keeps namespace opening and closing braces at the current structural indentation and formats the contained declarations as if they were declared at that same level. Nested namespaces follow the same rule. The namespace opening line and namespace closing brace are separated from contained declarations by one empty line.

```cpp
namespace app {
    
class Widget {
public:
    void Paint();
};

namespace detail {
    
void Helper();

}

}
```

## Macro Definitions

Macro continuation backslashes are formatter-owned. A multi-line macro definition is parsed as a single replacement list, then emitted with continuation backslashes on every physical macro line except the last replacement line.

When a function-like macro has a parameter listed in `macro_categories.statement_like_parameters` in `tools\format_config.json`, top-level invocations of that parameter inside the replacement list are statement-like macro elements. Each invocation is emitted as its own continuation line, regardless of whether several invocations could fit on one line. The formatter still applies normal spacing inside each invocation.

```cpp
#define CASEDASH_METRIC_DISPLAY_STYLE_ITEMS(X) \
    X(Scalar, "scalar") \
    X(Percent, "percent") \
    X(Memory, "memory")
```

## Spacing Rules

Spacing follows the native formatter policy with the project's explicit no-alignment rules:

- Put one space between control keywords and their opening parenthesis: `if (...)`, `for (...)`, `while (...)`, `switch (...)`, and `catch (...)`.
- Do not put a space between a function, method, constructor, destructor, macro-like call, or declaration name and `(`.
- Do not put spaces just inside parentheses, brackets, template angle brackets, or one-line initializer braces: `(value)`, `items[index]`, `std::vector<int>`, and `{a, b}`.
- Treat template and cast angle brackets as delimiter syntax, not comparison operators: `template <typename T>`, `std::vector<int>`, `std::vector<std::pair<std::string, std::string>>`, and `static_cast<int>(value)`.
- Keep empty braces as `{}` with no inner space.
- Put one space before a code-block opening brace after function declarations, class declarations, namespace declarations, control statements, lambdas, `try`, `else`, and `catch`.
- Put spaces around lambda trailing-return arrows: `[](int value) -> int { return value; }`.
- Put one space between `return` and the returned expression: `return value;`, `return !ready;`, and `return {};`. Bare returns keep no space before the statement semicolon: `return;`.
- Do not insert a visual-padding space before braced initializer braces or braced constructor calls: `Widget widget{config};` and `std::string_view{}`.
- Put one space after commas, and no space before commas.
- Put one space after semicolons that separate non-empty `for` header parts, and no space before semicolons. Empty forever-loop headers stay compact: `for (;;)`.
- Put no space before a statement-terminating semicolon.
- Put spaces around assignment, comparison, arithmetic, bitwise, logical, and compound-assignment binary operators, including assignment to an empty braced value: `const Context& context = {};`.
- Treat `operator` plus a following symbolic operator as one composite function name, with no spaces inside the name or before the parameter list: `operator==(...)`, `operator&(...)`, and `operator[](...)`.
- Treat destructor `~` plus the following type name as one composite function name, but keep declaration specifiers separated from it: `virtual ~Widget() = default;`.
- Do not put a space between a C-style cast and the expression it prefixes: `(void)StopServiceIfRunning(...)` and `(int)value`.
- Do not put spaces between unary operators and their operands: `!ready`, `++index`, `value--`, `*ptr`, `&value`, `-value`, and `+value`.
- Format pointer and reference declarators left-bound to the type: `Type* value`, `const Type& value`, `const Type&`, `Type&& value`, `virtual State& Method() = 0`, `const auto& [name, value]`, `void* (*callback)(...)`, `bool (*predicate)(...)`, and `reinterpret_cast<ColorConfig*>(address)`.
- Treat `&&` as a logical binary operator with spaces when it follows an expression, including qualified enum values: `field.kind == ValueKind::HexColor && IsNamedColor(name)`.
- Do not treat a preceding parenthesized or bracketed expression as a type for pointer or reference spacing. Multiplication and bitwise-and after such expressions keep binary-operator spaces: `(to.l - from.l) * amount` and `values[index] & mask`.
- Put spaces around ternary `?` and `:`.
- Put spaces around the range-for colon and the constructor-initializer colon, including braced range expressions: `for (const auto& item : items)`, `for (double candidate : {a, b})`, and `Widget() : member(value) {}`.
- Put no space before access-specifier, label, or `case` colons: `public:`, `label:`, and `case Value:`.
- Put no spaces around namespace, member-access, and pointer-member-access operators: `Namespace::Type`, `object.field`, `pointer->field`, `object.*member`, and `pointer->*member`.
- Put one space between `template` and its parameter list: `template <typename T>`.
- Put two spaces before a trailing `//` comment after code. Do not vertically align trailing comments.
- Put one space after a preprocessor directive keyword before its operand: `#include <vector>` and `#define NAME value`.

## Include Sorting

Include sorting follows project policy: include blocks are regrouped, sorted case-insensitively inside each group, and separated by one empty line between groups.

Sorting includes is the formatter's only source-token reorder. It may move `#include` directive lines within sortable include blocks, but it does not add includes, remove includes, rewrite include spelling, or move comments. Comments stay at their original source positions; a standalone comment inside an include area acts as a boundary for the sortable include run around it.

The include group definitions live in `tools\format_config.json` instead of formatter source. The configured group order is:

- main quoted include for the current source file
- Windows socket headers such as `<winsock2.h>` and `<ws2tcpip.h>`
- `<windows.h>`
- other angle-bracket system includes
- quoted vendored includes such as `"vendor/..."`
- other quoted project includes

```cpp
#include "package/source_file.h"

#include <winsock2.h>

#include <windows.h>

#include <algorithm>
#include <vector>

#include "vendor/package/header.h"

#include "package/other_header.h"
#include "util/text_format.h"
```

## Wrapping Model

Wrappable structures use one of two shapes:

- Keep the whole structure on one line when it fits within the configured 120-column line width.
- Put each element on its own line when it does not fit.

Wrapped contents are indented one level deeper than the construct that owns them. Opening and closing delimiters become their own structural boundaries when the construct wraps. The formatter recalculates this shape on every run, so a structure that becomes short enough collapses back to one line.

Examples:

```cpp
funCall(a, b, c);

veryLongFunctionCall(
    veryLongArgA,
    veryLongArgB,
    veryLongArgC
);

std::vector<int> values{a, b, c};

std::vector<int> longValues{
    longExprA,
    longExprB,
    longExprC
};
```

The same shape applies to function arguments, template arguments, braced initializer lists, array or subscript lists, and similar comma-separated syntax.

When a braced initializer list wraps, the opening brace stays with the declaration or assignment when it fits, every top-level initializer element is emitted on its own structural line, and nested initializer elements with comma-separated contents split structurally instead of staying compact. When consecutive nested initializer elements both split, the previous element close and comma share a line with the next element opener as `}, {`; the nested element contents remain indented one level inside that element.

## Greedy Wrapping Algorithm

The formatter uses a greedy wrapping algorithm over syntax-owned layout groups. Each wrappable group has a compact form and a split form. The compact form is the group printed on one line with all descendants also using their compact form when possible. The split form is the group printed using its structural multi-line shape.

The formatter decides wrapping in this order:

- Emit mandatory structural breaks first, such as statement boundaries, block boundaries, and already-separated top-level groups.
- For each remaining line owner, try the compact form.
- If the compact form fits within the configured line width, keep it compact.
- If the compact form overflows, split the outermost wrappable group that owns the overflowing line.
- After splitting that group, format each child greedily at its new indentation. A child stays compact when it now fits, and splits only when its own compact form still overflows.
- When a child expression inside any split comma list must split an operator chain, continuation parts of that child are indented one more level than the child line. This applies to function arguments, constructor arguments, braced initializer elements, array or subscript elements, template arguments, and similar list elements.
- Prefer splitting a non-fitting member-access chain before the top-level `.`, `->`, `.*`, or `->*` member access before splitting a compact receiver expression. This keeps `receiver()` compact when the outer member call is the wider expression.
- When an outer call or initializer has exactly one nested call or initializer argument, share the outer and inner opener when that combined line fits, and share the inner and outer closers. This avoids adding a line and an indent level only for delimiters.
- Never split a group partially. A comma list, operator chain, declaration parameter list, or control header is either fully compact or fully split.
- Never choose an empty delimiter pair as a wrapping target. Empty calls, empty braced values, and empty template argument lists stay compact, and the formatter breaks the nearest non-empty owning structure instead.
- Never split inside a function-pointer declarator group such as `(*)`. If the full function-pointer type does not fit, split the following parameter list instead.
- Never split inside a parenthesized callee group such as `(std::max)`. If the call does not fit, split the following argument list instead.
- Never split inside compiler declaration prefix groups such as `__declspec(noinline)`. If the full declaration does not fit, split the following declaration parameter list instead.
- Never split defaulted, deleted, or pure-virtual method markers away from the method declaration tail. If a declaration ending in `= default`, `= delete`, or `= 0` does not fit, split the method parameter list and keep the marker on the closing-parameter line.
- Treat an end-of-line comment attached to one element of a list or chain as a forced split for the whole owning list or chain.
- If a line has no legal wrappable group because the overflow is inside an unbreakable token, string literal, numeric constant, or preserved comment, allow that line to exceed the configured line width.

This outer-first rule keeps nested constructs compact when breaking their parent already solves the width problem.

```cpp
render(first, second, transform(inputA, inputB), third);

render(
    first,
    second,
    transform(inputA, inputB),
    third
);
```

If a nested child still exceeds the line width after the parent is split, the child then splits using the same rule.

```cpp
render(first, second, transform(veryLongInputA, veryLongInputB), third);

render(
    first,
    second,
    transform(
        veryLongInputA,
        veryLongInputB
    ),
    third
);
```

Assignment-like outer expressions follow the same rule. When a right-hand function call does not fit with the assignment prefix, but the complete call fits at continuation indentation, split after the assignment and keep the call compact. If the call itself must wrap and the assignment prefix plus call opener fits, keep the opener on the assignment line.

```cpp
auto value = buildValue(firstValue, transform(secondValueA, secondValueB), thirdValue);

HBITMAP colorBitmap =
    CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);

auto value = buildValue(
    firstValue,
    transform(secondValueA, secondValueB),
    thirdValue
);
```

End-of-line comments attached to elements preserve their source position and force the owning chain or list into the split form, even when the compact form would otherwise fit.

```cpp
update(firstValue, secondValue, thirdValue);

update(
    firstValue,
    secondValue,  // preserves the note on this element
    thirdValue
);

int value = firstValue + secondValue + thirdValue;

int value =
    firstValue +
    secondValue +  // preserves the note on this operator-chain part
    thirdValue;
```

## Declaration And Control Headers

Template prefixes are always emitted on their own line before the declaration they introduce. Function and method declarations or definitions use the same compact-or-fully-split shape as function calls. When the parameter list wraps, the opening parenthesis ends the first line, each parameter occupies its own line, and the closing parenthesis starts the line that continues the declaration or opens the body.

```cpp
void func(int x) {
    // code
}

void funcLong(
    longTypeA veryLongA,
    longTypeB veryLongB
) {
    // code
}

void declaredLong(
    longTypeA veryLongA,
    longTypeB veryLongB
);

template <typename UpdateKeyFn>
void SaveBoardSectionDifferences(
    const BoardConfig& board,
    const BoardConfig* compareBoard,
    const std::string& sectionName,
    UpdateKeyFn& updateKey
) {
    updateKey(board, compareBoard, sectionName);
}
```

Constructor initializer lists follow the same compact-or-fully-split rule. A short constructor initializer list stays with the declaration. A long constructor initializer list keeps `) :` on the header line, then emits each initializer on its own structural line indented one level deeper. For a non-empty body, the opening body brace is emitted on its own line after the initializer list. Empty bodies keep `{}` compact.

```cpp
Widget::Widget(int value) : value_(value) {}

DashboardApp::DashboardApp(
    const DiagnosticsOptions& diagnosticsOptions,
    bool bringToFrontOnRun
) :
    renderer_(trace_),
    diagnosticsOptions_(diagnosticsOptions),
    layoutEditController_(*this),
    shellUi_(std::make_unique<DashboardShellUi>(*this)),
    bringToFrontOnRun_(bringToFrontOnRun)
{
    renderer_.SetLiveAnimationEnabled(true);
}
```

Control-statement headers use the same treatment. This applies to `if`, `else if`, `while`, `for`, `switch`, `catch`, and similar parenthesized control constructs. A short header stays on one line. A long header puts the opening parenthesis at the end of the keyword line, formats the whole condition or header as structural contents, and puts the closing parenthesis on the line that opens the body.

Long conditions split by logical-chain element before splitting nested calls or comparisons. A simple comparison such as `value > 0` stays atomic when the surrounding `&&` or `||` chain wraps.

When a long control condition consists of one nested call expression, keep the control opener and nested call opener on the same line when that line fits. The nested call arguments use one structural indent inside the control statement, and the call close plus control close combine on the body-opening line.

Short unbraced statement bodies may stay on one line when the full statement fits. The formatter does not add braces to unbraced statements and does not remove braces from braced statements.

```cpp
if (ready) {
    // code
}

if (ok) return;

if (ready) {
    // code
} else {
    // code
}

if (
    veryLongConditionA &&
    veryLongConditionB
) {
    // code
}

while (
    veryLongConditionA &&
    veryLongConditionB
) {
    // code
}

if (!::ConfigureDisplay(
        updatedConfig,
        state.telemetryUpdate.dump,
        option.fittedScale,
        shell.TraceLog(),
        shell.WindowHandle()
)) {
    // code
}

for (
    int index = 0;
    index < veryLongLimit;
    ++index
) {
    // code
}

try {
    // code
} catch (const std::exception& exception) {
    // code
}

do {
    // code
} while (running);
```

## Labels And Switches

Access specifiers are class-level labels. They align with the class member indentation level, and members under them stay one indent level deeper.

```cpp
class Widget {
public:
    void Paint();

private:
    int value;
};
```

Switch labels are structurally inside the switch block. Statements belonging to a `case` or `default` label are indented one level deeper than the label.

```cpp
switch (value) {
    case 1:
        return one;
    case 2: {
        int local = two;
        return local;
    }
    default:
        return fallback;
}
```

Nested switches restore the enclosing switch's active case indentation after the inner switch closes.

If a `case` or `default` label opens a braced scope, keep the opening brace on the label line. The scoped statements stay at the same statement indentation as an unbraced case body, and the closing brace aligns with the `case` label.

## Operators

Chains of same-priority binary operators use the same all-on-one-line or all-parts-split rule as argument lists. The formatter must not break an operator chain at whichever point happens to match the line width. If any part of the chain needs to wrap, all parts of the chain are formatted on separate structural lines.

```cpp
int value = a + b + c;

int longValue =
    longExprA +
    longExprB +
    longExprC;
```

Ternary expressions are one operator chain. A chained ternary does not add another indent level for the second, third, or later condition arm; every wrapped arm belongs to the same flat chain. When a ternary chain wraps, each non-final arm keeps its `condition ? value :` text together on one line when that arm fits the line width. If that arm does not fit, split the arm after the top-level `?` before splitting nested calls inside the condition. If an assignment owns the wrapped ternary chain, break after the assignment operator before formatting the flat chain.

```cpp
int value = condition ? one : two;

int longValue =
    firstCondition ? firstValue :
    secondCondition ? secondValue :
    fallbackValue;

std::string currentValue =
    currentIt != names.end() && !currentIt->second.empty() ? currentIt->second :
    logicalName;

trace(
    condition ? firstValue :
        secondValue,
    firstAddend +
        secondAddend +
        thirdAddend
);

WidgetRow rows[] = {
    {
        "row",
        firstFlag |
            secondFlag |
            thirdFlag
    }
};
```

## Structural Indentation

Indentation follows syntax nesting, not the visual position of an earlier token. Extra delimiters create extra structure and can therefore change indentation.

```cpp
bool flag =
    veryLongCondition ? firstValue :
    secondValue;

bool flag = funcCall(
    veryLongCondition ? firstValue :
    secondValue
);
```

## Configuration

Configuration is limited to project-specific constants and data that let the formatter understand and organize project source without hardcoding project-specific names in the formatter implementation. Line width, indent width, tab width, macro categories, statement-like macro parameter names, macro names accepted by the grammar, and include-sorting groups belong in `tools\format_config.json`.

Configuration does not expose formatting style options. Brace behavior, wrapping behavior, spacing, and alignment behavior are fixed in formatter source so the project has one formatting language instead of a configurable family of dialects.
