# AI-First Source Formatting

This document describes the intended source-formatting ideology for the experimental native formatter. `format.cmd` remains the maintained formatting entrypoint until the native formatter is promoted.

The formatter leaves minimal freedom to authors. Line breaks, indentation, wrapping, and grouping are tool decisions, and authors should not hand-shape code to create visual layout. Each run computes layout from syntax and these rules; previous line breaks are not honored. Comments are the deliberate escape hatch: use comments to explain intent or split conceptual groups when the formatter's structural grouping is not enough.

## Goals

- Keep formatting deterministic enough that agents and humans do not spend review time on local style choices.
- Keep formatting simple to implement natively instead of cloning clang-format's option surface.
- Preserve the project readability rules: hard 120-column width, 4-space indentation, no tabs, and no vertical alignment.
- Let syntax structure drive wrapping and indentation.

## Ownership

- `format.cmd` owns the maintained formatting behavior today.
- `CaseDashTools.exe format` owns the experimental native formatter behavior.
- `src\tools\format.cpp` owns native formatting logic and hardcoded style behavior.
- `tools\format_config.json` owns project constants and parser support data, including line width, indent width, tab width, macro names accepted by the grammar, and include-sorting group definitions. It does not own style choices such as wrapping shapes or spacing rules.

## General Rules

- Use the configured 4 spaces for each indent level. Tabs are never emitted; the configured tab width is only for measuring existing tab characters before they are replaced.
- Remove trailing whitespace from every line, including comment-only and trailing-comment lines.
- Preserve the source token sequence except for include sorting. Outside include sorting, the formatter only changes spaces and line breaks, and never adds or removes braces by itself.
- Never vertically align tokens, comments, assignments, parameters, ternary arms, or consecutive declarations.
- Enforce the configured hard 120-column line width for generated code whenever the formatter can safely break the syntax.
- Do not break string literals, character literals, numeric constants, or comments only to satisfy the line width.
- Preserve comments in place. The formatter may change spaces and line breaks around comments, and may trim trailing whitespace from comment lines, but it does not move comments across code tokens or reflow comment text.
- Insert one empty line between top-level logical groups such as classes, free functions, namespace-level declarations, and method implementations.
- Do not insert blank lines inside a group only for visual rhythm. Use comments when a group needs an explicit conceptual split.
- Put block-opening braces for code blocks at the end of the introducing line, then line break immediately after the brace.
- Keep empty braces as `{}`.
- Do not force a line break after an initializer-list opening brace unless the initializer list itself wraps.
- Line break after a code-block closing brace, except that same-statement continuation forms such as `else`, `catch`, and do-while `while` stay on the same line as the closing brace.
- Line break after each statement-terminating semicolon.

## Spacing Rules

Spacing follows the maintained `.clang-format` behavior with the project's explicit no-alignment rules:

- Put one space between control keywords and their opening parenthesis: `if (...)`, `for (...)`, `while (...)`, `switch (...)`, and `catch (...)`.
- Do not put a space between a function, method, constructor, destructor, macro-like call, or declaration name and `(`.
- Do not put spaces just inside parentheses, brackets, template angle brackets, or one-line initializer braces: `(value)`, `items[index]`, `std::vector<int>`, and `{a, b}`.
- Keep empty braces as `{}` with no inner space.
- Put one space before a code-block opening brace after function declarations, class declarations, namespace declarations, control statements, lambdas, `try`, `else`, and `catch`.
- Do not insert a visual-padding space before braced initializer braces: `Widget widget{config};`.
- Put one space after commas, and no space before commas.
- Put one space after semicolons that separate `for` header parts, and no space before semicolons.
- Put no space before a statement-terminating semicolon.
- Put spaces around assignment, comparison, arithmetic, bitwise, logical, and compound-assignment binary operators.
- Do not put spaces between unary operators and their operands: `!ready`, `++index`, `value--`, `*ptr`, and `&value`.
- Format pointer and reference declarators left-bound to the type: `Type* value`, `const Type& value`, and `Type&& value`.
- Put spaces around ternary `?` and `:`.
- Put spaces around the range-for colon and the constructor-initializer colon: `for (const auto& item : items)` and `Widget() : member(value) {}`.
- Put no space before access-specifier, label, or `case` colons: `public:`, `label:`, and `case Value:`.
- Put no spaces around namespace, member-access, and pointer-member-access operators: `Namespace::Type`, `object.field`, and `pointer->field`.
- Put one space between `template` and its parameter list: `template <typename T>`.
- Put two spaces before a trailing `//` comment after code. Do not vertically align trailing comments.
- Put one space after a preprocessor directive keyword before its operand: `#include <vector>` and `#define NAME value`.

## Include Sorting

Include sorting follows the maintained `.clang-format` behavior: include blocks are regrouped, sorted case-insensitively inside each group, and separated by one empty line between groups.

Sorting includes is the formatter's only source-token reorder. It may move `#include` directive lines within sortable include blocks, but it does not add includes, remove includes, rewrite include spelling, or move comments. Comments stay at their original source positions; a standalone comment inside an include area acts as a boundary for the sortable include run around it.

The include group definitions live in `tools\format_config.json` instead of formatter source. The configured group order mirrors the current `.clang-format` categories:

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

## Greedy Wrapping Algorithm

The formatter uses a greedy wrapping algorithm over syntax-owned layout groups. Each wrappable group has a compact form and a split form. The compact form is the group printed on one line with all descendants also using their compact form when possible. The split form is the group printed using its structural multi-line shape.

The formatter decides wrapping in this order:

- Emit mandatory structural breaks first, such as statement boundaries, block boundaries, and already-separated top-level groups.
- For each remaining line owner, try the compact form.
- If the compact form fits within the configured line width, keep it compact.
- If the compact form overflows, split the outermost wrappable group that owns the overflowing line.
- After splitting that group, format each child greedily at its new indentation. A child stays compact when it now fits, and splits only when its own compact form still overflows.
- Never split a group partially. A comma list, operator chain, declaration parameter list, or control header is either fully compact or fully split.
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

Assignment-like outer expressions follow the same rule. The assignment line splits first; the right-hand expression then stays compact or splits based on the width available at its new indentation.

```cpp
auto value = buildValue(firstValue, transform(secondValueA, secondValueB), thirdValue);

auto value =
    buildValue(
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

Function and method declarations or definitions use the same compact-or-fully-split shape as function calls. When the parameter list wraps, the opening parenthesis ends the first line, each parameter occupies its own line, and the closing parenthesis starts the line that continues the declaration or opens the body.

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
```

Control-statement headers use the same treatment. This applies to `if`, `else if`, `while`, `for`, `switch`, `catch`, and similar parenthesized control constructs. A short header stays on one line. A long header puts the opening parenthesis at the end of the keyword line, formats the whole condition or header as structural contents, and puts the closing parenthesis on the line that opens the body.

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

## Operators

Chains of same-priority binary operators use the same all-on-one-line or all-parts-split rule as argument lists. The formatter must not break an operator chain at whichever point happens to match the line width. If any part of the chain needs to wrap, all parts of the chain are formatted on separate structural lines.

```cpp
int value = a + b + c;

int longValue =
    longExprA +
    longExprB +
    longExprC;
```

Ternary expressions are one operator chain. A chained ternary does not add another indent level for the second, third, or later condition arm; every wrapped part belongs to the same flat chain.

```cpp
int value = condition ? one : two;

int longValue =
    firstCondition ? firstValue :
    secondCondition ? secondValue :
    fallbackValue;
```

## Structural Indentation

Indentation follows syntax nesting, not the visual position of an earlier token. Extra delimiters create extra structure and can therefore change indentation.

```cpp
bool flag =
    veryLongCondition ?
    firstValue :
    secondValue;

bool flag = funcCall(
    veryLongCondition ?
    firstValue :
    secondValue
);
```

## Configuration

Configuration is limited to project-specific constants and data that let the formatter understand and organize project source without hardcoding project-specific names in the formatter implementation. Line width, indent width, tab width, macro categories, macro names accepted by the grammar, and include-sorting groups belong in `tools\format_config.json`.

Configuration does not expose formatting style options. Brace behavior, wrapping behavior, spacing, and alignment behavior are fixed in formatter source so the project has one formatting language instead of a configurable family of dialects.
