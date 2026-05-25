# Source Formatting

This document specifies the source layout produced by `format.cmd`.

The formatter owns whitespace, line breaks, indentation, wrapping, include ordering, trailing-comma normalization, and required control-statement braces. Source line breaks are not style input. Comments and allowed blank-line separators are grouping input.

## Main Tenets

- Never use vertical alignment.
- When a wrapped construct closes, the matching closing delimiter begins a line at the owning indent.
- Keep lists and same-operator chains either fully compact or split item-by-item.
- Use indentation changes as visual group borders.
- Use one indentation size for every indentation change.

## Spacing Rules

- Put one space between a control keyword and `(`, e.g. `if (`.
- Put no space between a call, macro-like call, declaration name, constructor, destructor, or operator name and `(`, e.g. `Run(`.
- Put no padding inside parentheses, brackets, template angle brackets, or one-line initializer braces, e.g. `call(value)`.
- Keep empty braces as `{}`, e.g. `return {};`.
- Put one space before a code-block `{`, e.g. `if (ok) {`.
- Put spaces around lambda trailing-return arrows, e.g. `[]() -> int`.
- Put one space before trailing function qualifiers, e.g. `Run() const`.
- Put one space between `template` and `<`, e.g. `template <typename T>`.
- Keep declaration modifiers compact and separate them from the modified type with one space, e.g. `alignas(8) int`.
- Separate a declaration type from its declarator with one space, e.g. `int value`.
- Put no space between a string or character literal prefix and the literal, e.g. `L"text"`.
- Put no padding before braced initializer braces, e.g. `std::string{}`.
- Put one space after commas and no space before commas, e.g. `a, b`.
- Put one space after non-empty `for` header semicolons. Put no space before semicolons. Keep `for (;;)` compact, e.g. `for (int i = 0; i < n; ++i)`.
- Put spaces around binary and ternary operators, e.g. `a + b`.
- Put no spaces around unary operators, e.g. `!ok`.
- Bind type declarator symbols to the type, e.g. `int* value`.
- Treat `operator` plus a following symbolic operator as one function name, e.g. `operator==(`.
- Treat destructor `~` plus the following type name as one function name, e.g. `~Widget(`.
- Put no space between a C-style cast and the expression it prefixes, e.g. `(void)value`.
- Put spaces around range-for and constructor-initializer colons, e.g. `for (auto item : items)`.
- Put no space before access-specifier, label, or `case` colons, e.g. `public:`.
- Put no spaces around namespace, member-access, or pointer-member-access operators, e.g. `std::string`.
- Put two spaces before a trailing `//` comment after code, e.g. `value;  // note`.
- Put one space after a preprocessor directive keyword before its operand, e.g. `#pragma once`.

## Structural Lines

- Remove trailing whitespace from every line.
- Use 4 spaces per indent level. Do not emit tabs.
- Preserve comments in source order. A trailing comment stays trailing only when it was trailing in source. A standalone comment stays standalone.
- Preserve one source blank line when it separates already closed declarations or statements at the same structural level. Collapse multiple blank lines to one.
- Drop blank lines at the beginning of a block and immediately before a closing brace.
- Insert required structural blank lines even when source omits them.
- Remove trailing commas except in enum bodies.
- Break after each statement-terminating semicolon.
- Put block-opening braces at the end of the introducing line, then break.
- Break after a code-block closing brace unless the following token is `else`, `catch`, `finally`, or the `while` that closes a do-while statement.
- Treat a standalone braced statement block as a block. Its closing brace does not attach to the following statement.

```cpp
if (ready) {
    return;
} else {
    Reset();
}

do {
    Poll();
} while (running);

{
    const Lock lock(mutex);
    value = next;
}

if (value != next) {
    Update(value);
}
```

## Lists

Lists use compact or split form.

```cpp
call(first, second, third);

call(
    first,
    second,
    third
);
```

The rule applies to function arguments, template arguments, braced initializer lists, subscript lists, declaration parameter lists, enum bodies, and similar comma-separated syntax.

When a template list wraps, `<` stays with the owner when it fits, each top-level argument occupies one line, and the closing `>` starts the continuation line.

Nested braced initializer and braced constructor elements stay compact when they fit their own line. Nested elements split only when their compact line does not fit.

```cpp
Widget rows[] = {
    {first, second},
    {third, fourth}
};

Widget rows[] = {
    {
        veryLongFirst,
        veryLongSecond
    }, {
        veryLongThird,
        veryLongFourth
    }
};
```

Enum bodies always split one enumerator per line and keep a trailing comma on the final enumerator.

```cpp
enum class ValueFormat : std::uint8_t {
    String,
    Integer,
    FloatingPoint,
};
```

When a class, struct, or enum body is followed by a declarator for the declared type, keep the declarator attached to the closing brace.

```cpp
struct Context {
    const Config* config = nullptr;
    size_t count = 0;
} context{&config, 0};
```

## Operator Chains

Formatter-owned operator chains use compact or split form.

```cpp
int value = a + b + c;

int value =
    first +
    second +
    third;
```

Ternary chains are one flat chain.

```cpp
const char* key =
    firstCondition ? firstKey :
    secondCondition ? secondKey :
    fallbackKey;
```

Logical chains split by `&&` or `||`. Inside `if` and `while`, split logical parts stay at condition indentation. Inside a split `for` header, a wrapped logical chain inside one semicolon part uses continuation indentation.

Stream-shift chains split before `<<` or `>>`. If the shifted tail fits on one continuation line, keep it compact. Otherwise, put each continued shift segment at the start of a continuation line.

```cpp
std::cout
    << "name=" << name << " value=" << value << "\n";
```

Stream configuration methods listed in `.cpp-format` stay on the same continuation line as the following streamed value when the combined line fits.

Adjacent string literals are an implicit concatenation chain. When the sequence stays split, the first literal uses expression indentation and later fragments use one additional indent.

```cpp
std::string trace = FormatText(
    "layout=\"%s\" editing=%s moving=%s "
        "tooltip_visible=%s capture=\"%s\"",
    layoutName,
    editingText,
    tooltipText,
    captureText
);
```

Line-fragment strings ending with escaped `\n` or `\r\n` stay separate. A boundary such as `"\xB0" "C"` stays split when textual joining would change escape parsing.

Repeated `+` and repeated `*` are formatter-owned arithmetic chains. `-`, `/`, and `%` are ordinary binary operators.

## Greedy Wrapping

The formatter uses a greedy outer-first algorithm.

- Emit mandatory statement, block, declaration-group, and comment boundaries.
- Try compact form for the current syntax owner.
- Keep compact form when it fits.
- Keep generated lines within 120 columns when syntax provides a safe break.
- Split the outermost wrappable owner when compact form overflows.
- Reformat each child at its new indentation.

```cpp
render(first, second, transform(inputA, inputB), third);

render(
    first,
    second,
    transform(inputA, inputB),
    third
);
```

If a child still overflows after the parent splits, split the child.

```cpp
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

When a split operator-chain part is followed by an expression that also must split, keep the next expression's opener on the operator line when that opener fits. This opener economy applies uniformly to parenthesized groups, calls and member-call chains, subscripts, and braced constructions.

```cpp
selected = Matches(region) || (
    special != nullptr &&
    IsActive(*special)
);

value = first + BuildValue(
    left,
    right
);
```

Assignment-like expressions split after the assignment when the right-hand side fits at continuation indentation. If the right-hand side must split and its first split line fits after the assignment prefix, attach that first split line to the assignment.

```cpp
HBITMAP colorBitmap =
    CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);

target = RenderPoint{
    x,
    y
};
```

A wrapped parenthesized group with one expression keeps the compact expression on one continuation line when it fits. This includes control conditions, so the outer condition or call break wins before a nested single-argument call.

```cpp
metrics.definitions.push_back(
    MetricDefinitionConfig{"gpu.load", MetricDisplayStyle::Percent, true, 0.0, "%", "Load"}
);
```

A split receiver closing with `)`, `]`, or `}` may keep a trailing member chain or expression suffix on the closing line when it fits.

```cpp
const int width = MeasureTextWidthForControl(
    hwnd,
    controlId,
    ReadDialogControlText(hwnd, controlId)
) + 8;
```

Inside a plain parenthesized expression, split chains stay at the expression indent.

Do not split inside empty delimiter pairs, function-pointer declarator groups, parenthesized callees, or compiler declaration prefix groups.

Defaulted, deleted, and pure-virtual method markers stay with the declaration tail.

An end-of-line comment attached to one element forces the owning list or chain into split form.

```cpp
update(
    first,
    second,  // note
    third
);
```

If no legal wrapping point exists, the line may exceed 120 columns.

## Declaration Groups

Declaration separation applies only in declaration scopes.

- Separate top-level logical groups with one empty line.
- Separate neighboring type declarations from siblings with one empty line.
- Separate neighboring declarations of different kinds with one empty line.
- Keep consecutive fields grouped when wrapping only moves an initializer to a continuation line.
- Separate a field or type alias from neighbors when its initializer or alias target owns a multi-line delimiter list whose closing delimiter returns to declaration indentation.
- Separate fields from neighboring methods with one empty line.
- Keep consecutive method declarations in one method group.

Access specifiers are class-level labels. Members under them stay one indent level deeper.

```cpp
class Widget {
public:
    void Paint();

private:
    int value;
};
```

Namespaces are grouping syntax, not indentation syntax. Declarations inside a namespace stay at the same indentation level as the namespace declaration. Separate the namespace opening line and closing brace from contained declarations with one empty line.

```cpp
namespace app {

class Widget {
public:
    void Paint();
};

}
```

## Declaration And Control Headers

Function and method declarations use list layout for parameters.

```cpp
void Func(int x) {
    Run(x);
}

void FuncLong(
    LongTypeA veryLongA,
    LongTypeB veryLongB
) {
    Run(veryLongA, veryLongB);
}
```

Template prefixes are emitted before the introduced declaration. A short `requires(...)` clause may stay on the same line as `template <...>`. A long `requires` clause moves to a subordinate line and wraps structurally.

```cpp
template <typename T> requires(HasValue<T>)
void Use(T& value);

template <typename Callable>
    requires(
        !std::is_same_v<std::remove_cvref_t<Callable>, FunctionRef> &&
        std::is_invocable_r_v<Result, Callable&&, Args...>
    )
FunctionRef(Callable&& callable);
```

Constructor initializer lists use compact or split form. A long initializer list keeps `) :` on the header line, or `) noexcept :` when a trailing qualifier is present. Non-empty bodies put the opening body brace on its own line after the initializer list. Empty bodies keep `{}` compact.

```cpp
Widget::Widget(int value) : value_(value) {}

DashboardApp::DashboardApp(
    const DiagnosticsOptions& diagnosticsOptions,
    bool bringToFrontOnRun
) :
    renderer_(trace_),
    diagnosticsOptions_(diagnosticsOptions),
    bringToFrontOnRun_(bringToFrontOnRun)
{
    renderer_.SetLiveAnimationEnabled(true);
}
```

Every `if`, `else`, `for`, `while`, `do`, and `switch` body is a braced block. `else if` remains a direct chain.

```cpp
if (ready) {
    return;
} else if (pending) {
    Queue();
} else {
    Reset();
}
```

Control headers use list layout. Headers with init-statements split at top-level semicolons before nested calls.

```cpp
for (
    int index = 0;
    index < limit;
    ++index
) {
    Run(index);
}

if (
    const auto current = FindCurrentValue(config);
    current.has_value() && *current != nullptr
) {
    Use(*current);
}
```

A long control condition that is one nested call keeps the control opener and nested call opener on the same line when that line fits. Nested call arguments use one indent. The call close and control close combine on the body-opening line.

```cpp
if (!::ConfigureDisplay(
    updatedConfig,
    telemetryDump,
    fittedScale,
    traceLog,
    hwnd
)) {
    return false;
}
```

`else`, `catch`, `finally`, and do-while `while` attach to the preceding closing block brace.

```cpp
try {
    Run();
} catch (const std::exception& exception) {
    Report(exception);
} finally {
    Cleanup();
}

do {
    Poll();
} while (running);
```

## Labels And Switches

Switch labels are inside the switch block. Statements under a `case` or `default` label are indented one level deeper. A scoped case keeps `{` on the label line and aligns `}` with the label.

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

Nested switches restore the enclosing switch case indentation after the inner switch closes.

## Lambdas

Single-statement lambda bodies may stay on one line when the complete lambda fits. Multi-statement lambda bodies split after `{`, format each statement on its own line, and close on their own line.

When a wrapped lambda is assigned to a variable, keep the assignment prefix and lambda opener on the same line if the opener fits.

If the opener does not fit, first keep the capture list compact and split between `]` and `(` when both lines fit.

When the lambda parameter list splits, keep the body `{` on the closing-parameter line.

```cpp
const auto updateKey = [&](
    const std::string& sectionName,
    const std::string& key,
    const std::string& value
) {
    Update(sectionName, key, value);
};
```

Split a multi-parameter parameter list before splitting captures. Split captures last.

## Preprocessor And Macros

Preprocessor directives stay at column zero.

Put one empty line after `#pragma once`. Put one empty line before and after each `#undef`.

Macro continuation backslashes, spaces before continuation backslashes, and continuation newlines are formatter-owned. A multi-line macro definition is parsed as one replacement list, then emitted with continuation backslashes on all continued macro lines.

Macro replacement lists that form declaration fragments are recursively formatted before continuation backslashes are added.

Function-like macro parameters listed in `.cpp-format` `MacroCategories.StatementLikeParameters` are emitted one invocation per continuation line.

```cpp
#define CASEDASH_METRIC_DISPLAY_STYLE_ITEMS(X) \
    X(Scalar, "scalar") \
    X(Percent, "percent") \
    X(Memory, "memory")
```

## Include Sorting

Include sorting may move `#include` lines within sortable include blocks. It does not add includes, remove includes, rewrite include spelling, or move comments.

Comments inside an include area bound the sortable include run around them.

Include blocks are regrouped, sorted case-insensitively inside each group, and separated by one empty line between groups. Group definitions live in `.cpp-format`. Group order:

- main quoted include for the current source file
- Windows socket headers
- `<windows.h>`
- other angle-bracket system includes
- quoted vendored includes
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

## Token Preservation

The formatter preserves source token order except for:

- include sorting
- trailing-comma normalization
- required control-statement brace insertion
- safe adjacent ordinary string-literal concatenation

String literals ending with escaped `\n` or `\r\n` are line-fragment boundaries and are not joined with the following literal. A trailing escape such as `\xB0` that would consume the next fragment's first character after textual joining also prevents joining.

Outside the listed changes, the formatter changes only spaces and line breaks.

## Tooling Ownership

- `format.cmd` owns repository source discovery for all, changed, and staged formatting runs, then delegates the discovered newline-separated file list to `CaseDashTools.exe format --files`.
- `CaseDashTools.exe format` owns clang-format-like stdin, direct file arguments, newline file lists passed with `--files <path>` or `--files=<path>`, `-i`, `--dry-run`, `--style=file`, `--style=<path>`, and `--style=file:<path>` handling, parsing, checking, fixing, ignore-file filtering, and stdout rendering.
- `src\tools\format.cpp` owns fixed formatting logic.
- `.cpp-format` owns only the formatter inputs that are intentionally configurable: `ColumnLimit`, `IndentWidth`, `TabWidth`, `IncludeCategories`, `MainIncludeChar`, `IncludeIsMainRegex`, `MacroCategories`, and `StreamShift`.
- `.cpp-format-ignore` owns simple literal formatter exclusions. Entries are directory names, file names, or slash-separated relative paths. Glob patterns and negation are not supported.
- `src\tools\vendor\tree-sitter\` owns vendored tree-sitter grammar inputs and generated parser sources.
- `tools\regenerate_tree_sitter_grammar.py` owns parser regeneration.

The formatter uses tree-sitter core from vcpkg and the vendored C++ grammar under `src\tools\vendor\tree-sitter\tree-sitter-cpp\`. The C grammar under `src\tools\vendor\tree-sitter\tree-sitter-c\` is kept for provenance and regeneration.

Regenerate parser outputs only after editing vendored grammar source or parser macro categories:

```bat
python tools\regenerate_tree_sitter_grammar.py
```

The regeneration tool writes `case_dash_macro_config.js` from `.cpp-format`, runs the pinned tree-sitter CLI, and updates generated files under `src\tools\vendor\tree-sitter\tree-sitter-cpp\src\`. Pass `--tree-sitter-cli <path>` to use an existing CLI. Otherwise it downloads the pinned Windows CLI under `build\`.

Configuration is intentionally narrow and does not expose style policy knobs. Brace behavior, wrapping behavior, spacing, alignment behavior, and other layout ideology are fixed in formatter source.
