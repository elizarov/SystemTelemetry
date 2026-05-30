# Source Formatting

This document specifies the source layout produced by `format.cmd`.

The formatter owns whitespace, line breaks, indentation, wrapping, include ordering, trailing-comma normalization, and control-brace normalization. Source line breaks are not style input. Comments and allowed blank-line separators are grouping input.

## Main Tenets

- Never use vertical alignment.
- Keep formatter-owned chains and lists compact or split item-by-item.
- Use no heuristics or weights; use only the general line-break optimization rule.
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

## Parenthesized Initializer Ambiguity

C++ block-scope declarations can use a token shape that is both a parenthesized direct initializer and a function declaration without type information.

```cpp
int product(a * b, c & d);
ResultType local(FirstType* first, SecondType& second);
```

The formatter has no symbol table and follows the parser's declaration interpretation for that ambiguous shape. Parenthesized initialization is rare in project code, so the formatter keeps the generic declaration rule and leaves source disambiguation to the author. Prefer braced initialization by default. Use parenthesized initialization only when braces have different C++ semantics, such as vector length construction:

```cpp
std::vector<int> values(count);
std::vector<int> oneValue{count};
```

When a parenthesized direct initializer needs expression operands that could parse as declarators, add extra parentheses around those operands. This is the preferred fix:

```cpp
int product((a * b), (c & d));
```

## Line Hygiene

- Remove trailing whitespace from every line.
- Use 4 spaces per indent level. Do not emit tabs.
- Preserve comments in source order. A trailing comment stays trailing only when it was trailing in source. A standalone comment stays standalone.
- Preserve one source blank line when it separates already closed declarations or statements at the same structural level. Collapse multiple blank lines to one.
- Drop blank lines at the beginning of a block and immediately before a closing brace.
- Insert required structural blank lines even when source omits them.
- Remove trailing commas except in enum bodies.

## Mandatory Line Breaks

Mandatory line breaks are structural boundaries. The break is always taken before optional wrapping is considered.

- Break between complete statements and declarations, including after each statement-terminating semicolon.
- The single-statement lambda is an exception only when the whole lambda stays on one physical line.
- Put block-opening braces at the end of the introducing line, then break.
- Break after a code-block closing brace unless the following token is `else`, `catch`, `finally`, or the `while` that closes a do-while statement.
- Treat a standalone braced statement block as a block. Its closing brace does not attach to the following statement.
- Break around preprocessor directives and macro continuation lines.
- Break between enum enumerators; enum bodies keep one enumerator per line.
- Break between declaration groups where declaration-scope separation rules require a blank line.
- Break multi-statement lambda bodies after `{`, format each body statement with normal mandatory statement breaks, and put the closing `}` on its own line.
- Preserve a standalone line comment on its own line.

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

## Line Break Opportunities

Line break opportunities are optional boundaries that the optimizer may take when formatting one segment between mandatory breaks. The formatter sends these opportunities to a dynamic-programming optimization solver; see **Break Selection Algorithm** for the solver objective and tie-break rules.

- After assignment operators and after binary or ternary operators.
- After delimiter openers and before matching closers for `()`, `[]`, `{}`, and template `<>`.
- After commas in lists, including call arguments, declaration parameters, template arguments, braced initializer elements, subscript lists, base-class lists, and enum bodies.
- Between a declaration type and its direct-initialized declarator value, whether the initializer is braced or parenthesized and whether the declaration is local or a field.
- After semicolons inside `for` and control headers.
- Around lambda captures, lambda parameter lists, lambda bodies, constructor initializer lists, and adjacent string literal sequences.

## Indent Economy

Indent economy lets nested delimiter groups share one body indentation level when their opener and closer placement stays visually unambiguous. It applies to broken `()`, `[]`, `{}`, and parsed template `<>` delimiter groups. It is a legality rule for candidate layouts; the optimizer still chooses among legal layouts with the normal dynamic-programming objective.

For any broken delimiter stack:

- The opening line ends with an opener or opener sequence, so the body break is immediately after an opener.
- If the opening line starts with an opener or opener sequence after indentation, the whole line contains only openers.
- The closing line starts with all closers for the stack combined. Syntax that belongs after the stack, such as `;`, `,`, another closer, or the next opener in a list boundary, may follow those closers.

This allows wrapper and nested delimiter groups to share indentation:

```cpp
render(transform(
    first,
    second
));

Widget rows[] = {{
    first,
    second
}};
```

A broken delimiter body cannot put item content after a line-start opener:

```cpp
POINT points[] = {
    {rect.left,
        rect.top
    }
};
```

The formatter produces a separate opener line instead:

```cpp
POINT points[] = {
    {
        rect.left,
        rect.top
    }, {
        rect.right,
        rect.bottom
    }
};
```

Delimiter item boundaries may coalesce generically only for direct close-comma-open boundaries where both neighboring items use split delimiter expansions, such as `}, {` or `), (`. The boundary line starts with the combined closers for the previous item, then keeps the comma and the next item's opener before the next body break.

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

The rule applies to function arguments, template arguments, braced initializer lists, subscript lists, declaration parameter lists, base-class lists, enum bodies, and similar comma-separated syntax.

Compact comma-separated lists may keep leading items on the opener line while the final item uses an indent-economy delimiter expansion. The final item may be any expression, such as a braced initializer or call. If any earlier item splits, or if the final item only splits at an operator, the whole list uses split form.

```cpp
call(first, second, [](int value) {
    return value + 1;
});
```

When a template list wraps, `<` stays with the owner and each top-level argument occupies one line.

Nested braced initializer and braced constructor elements are independent structural parts. Each nested element uses the same compact-or-split optimization as any other segment and follows indent-economy delimiter placement.

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

Binary chain operators are the operators whose usual source meaning is an associative, mostly commutative aggregation. Formatting them as a chain avoids implying that the first operand owns a subordinate "rest of expression" branch. Stream shifts and comma expressions are also chain-shaped because their syntax is a repeated separator sequence, but they do not use the associativity rationale.

- Chain operators are token-class defined.
- Binary chain operators are `+`, `*`, `&`, `|`, `^`, `&&`, and `||`.
- Comma is a chain operator only in comma expressions.
- Stream-shift chain operators are `<<` and `>>`.
- Operators outside the chain-operator token class are ordinary operators. Examples include `==`, `-`, `/`, `%`, and comparisons.
- Chain classification is independent of operand count. A chain with two operands is still a chain.
- Chains use compact or split form.
- Compact chains may keep leading operands on one line while the final operand uses an indent-economy delimiter expansion.
- A final operand that only splits at another operator does not qualify for compact chain form.
- Split chains take every top-level chain opportunity.
- Chain parts use the chain item indentation, not an additional continuation indentation.
- If an outer context applies continuation indentation, that context defines the chain's base indentation.

```cpp
int total = (
    firstLongValue +
    secondLongValue
);

bool ready = (
    firstCondition &&
    secondCondition
);

int total = first + second + BuildValue(
    firstLongArgument, 
    secondLongArgument
);
```

- Logical chains split by `&&` or `||`.
- Inside `if` and `while`, split logical chain parts stay at condition indentation.
- Inside a split `for` header, a wrapped logical chain inside one semicolon part uses continuation indentation.
- Stream-shift chains split before `<<` or `>>`.
- A stream-shift chain may split once between the receiver and a compact shifted tail.
- If the compact shifted tail does not fit, each continued shift segment starts a continuation line.
- Stream configuration methods listed in `.cpp-format` bind to the following shifted value; no `<<` or `>>` break is taken between a configured manipulator run and that value.
- Adjacent string literals are an implicit concatenation chain.
- When a call argument string sequence stays split, the first literal uses expression indentation and later fragments use one additional indent.
- When a forced multi-line string-fragment sequence is the direct initializer in an assignment or declaration, the assignment breaks first and all fragments align at the assignment continuation indentation.
- Line-fragment strings ending with escaped `\n` or `\r\n` stay physically split.
- A boundary such as `"\xB0" "C"` stays token-separated to preserve escape parsing, but it may remain on one physical line when the adjacent-literal chain fits.
- Ternary chains are flat chains.
- A nested ternary chain either breaks after every `:` or stays compact.
- A single ternary may break after `?`, after `:`, after both, or inside either branch while keeping the selected branch attached to its `?` or `:` marker.

```cpp
const char* key = firstCondition ? firstKey :
    secondCondition ? secondKey :
    fallbackKey;
```

- Ordinary binary operators use continuation indentation for the right operand when they split, including inside `(...)`.

```cpp
bool installed = (
    RegEnumKeyExA(key, index, name, &nameLength, nullptr, nullptr, nullptr, nullptr) ==
        ERROR_SUCCESS
);

int ratio = (
    firstLongValue /
        secondLongValue
);
```

- Plain non-call parentheses contain one expression group.
- A plain non-call parenthesis group adds only body indentation.
- Lists and formatter-owned chain parts inside that group keep their elements at that body level.
- Nested ordinary binary operators still introduce continuation indentation.
- Unary operators and declarator `*` or `&` are token facts, not chain break points.
- An end-of-line comment attached to one chain part forces the chain into split form.

## Break Selection Algorithm

For each parsed segment between mandatory line breaks, the formatter uses a tree-sitter-derived layout model built directly from the parsed syntax tree. Tree nodes represent text leaves, sequences, delimiter groups, lists, operator structures, adjacent string literal sequences, lambda headers and bodies, and comments. The formatter rejects inputs whose tree-sitter parse contains errors or missing nodes; formatter-supported syntax is added to the grammar instead of falling back to token-span recovery.

Each node exposes its legal compact and split layouts. The optimizer chooses which optional breaks to take with dynamic programming:

- Minimize the largest overflow beyond the configured column limit; layouts with no overflowing physical line have zero overflow.
- On equal maximum overflow, minimize the number of physical lines that overflow.
- On equal overflow cost, minimize the physical line count.
- On equal line count, prefer the layout whose deepest taken break renders at the shallower indentation level; if still tied, prefer the structurally shallower deepest taken break, then source-order-stable compact behavior.

The optimizer treats the column limit as bounded input and caches each subproblem by node and normalized layout context, including indentation, prefix, suffix, and continuation mode.

Delimiter-group legality is defined by indent economy. The legality rules restrict candidate layouts without forcing a local break choice; the optimizer chooses among the legal compact, split, and indent-economy layouts.

Function signatures may break after the complete return type before breaking inside the return type. The function name is indented one continuation level. Split parameters may keep the return type and function name together when that line fits. Functions and lambdas deliberately share one callable-header model. Function definitions whose return-type prefix is split away from the function name start the body `{` on its own line at declaration indentation. Assigned lambdas whose assignment prefix is split away from the lambda header expose both attached and declaration-indented body-header layouts to the optimizer. A callable whose only header continuation is a split parameter list must keep `) {` together.

```cpp
std::vector<std::string>
    ParseItems(const std::vector<ConfigLine>& lines, size_t& index);

std::vector<std::string>
    ParseItems(
        const std::vector<ConfigLine>& lines,
        size_t& index
    )
{
    return {};
}

std::set<std::string> RequireSuffixGroup(
    const std::map<std::string, std::set<std::string>>& suffixGroups,
    std::string_view configPath,
    std::string_view groupName
) {
    return {};
}

render(
    first,
    transform(
        veryLongInputA,
        veryLongInputB
    ),
    third
);
```

Do not split inside empty delimiter pairs, function-pointer declarator groups, parenthesized callees, compiler declaration prefix groups, `__declspec` groups, operator function names, or template-angle tokens that are not template argument lists.

Defaulted, deleted, and pure-virtual method markers stay with the declaration tail.

An end-of-line comment attached to one list element forces the owning list into split form. A source blank line or standalone comment between list elements also forces the owning list into split form. Lists still split all top-level comma opportunities together, and a single empty line directly before or after a standalone list comment is preserved.

```cpp
update(
    first,
    second,  // note
    third
);
```

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

Template prefixes are emitted before the introduced declaration. A `requires` clause stays on the same line as `template <...>` only when the complete template prefix and compact clause fit on that line. Otherwise, including when the clause owns a forced break, the `requires` clause moves to a subordinate line and wraps structurally.

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

Constructor initializer lists use compact or split form. A long initializer list keeps `) :` on the header line, or `) noexcept :` when a trailing qualifier is present. Initializer count alone does not force the constructor parameter list to split. Non-empty bodies keep the opening body brace with the initializer list when the complete line fits; otherwise the brace moves to its own line after the initializer list. Empty bodies keep `{}` compact.

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

Control-brace normalization makes every `if`, `else`, `for`, `while`, `do`, and `switch` body a braced block. It also emits an `else` block whose only statement is an `if` statement as a direct `else if` chain. Compact empty control bodies stay `{}` and finish their own control-body line before a following block-attachment keyword.

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

Lambdas intentionally format like functions. A lambda is a callable for all header/body placement decisions: the capture list, parameter list, and optional trailing return type form the callable header, and an assignment prefix such as `const auto name =` behaves like a function return-type prefix.

Single-statement lambda bodies may keep their braces and statement compact only when the complete lambda capture, parameter list, optional trailing return type, and body fit on one physical line. The compact single-statement form is limited to statements whose subtree contains no compound block, so a statement such as `if (condition) { work(); }` uses the same broken-body form as other block-bearing lambda bodies. If a lambda breaks anywhere, its body breaks after `{`, formats the body one indentation step deeper than the declaration, and closes on its own line. Multi-statement lambda bodies always use that broken-body form. Any delimited list containing a broken lambda body splits like any other list containing a multi-line item.

When an assigned lambda keeps the assignment prefix and lambda header together, a split parameter list must keep the body opener attached as `) {`, matching function definitions whose only header continuation is a split parameter list.

Lambda captures and lambda parameters are separate break opportunities. Captures and parameters use the same compact-or-split optimization as other delimiter groups.

When an assigned lambda splits the assignment prefix away from the lambda header and the lambda body is broken, the body opener starts on its own line at declaration indentation so body statements are one indentation step deeper than the declaration.

```cpp
const auto updateKey = [&](
    const std::string& sectionName,
    const std::string& key,
    const std::string& value
) {
    Update(sectionName, key, value);
};

const auto findValue =
    [&config](std::string_view name) -> std::optional<Value>
{
    return LookupValue(config, name);
};
```

Multi-parameter lambda parameter lists and capture lists split all-or-nothing.

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
- control-brace normalization
- safe adjacent ordinary string-literal concatenation

String literals ending with escaped `\n` or `\r\n` are line-fragment boundaries and are not joined with the following literal. A trailing escape such as `\xB0` that would consume the next fragment's first character after textual joining also prevents joining.

Outside the listed changes, the formatter changes only spaces and line breaks.

## Tooling Ownership

- `format.cmd` owns repository source discovery for all, changed, and staged formatting runs, then delegates the discovered newline-separated file list to `CaseDashTools.exe format --files`. It forwards `--concurrency <n>` when a caller overrides native formatter worker count.
- `CaseDashTools.exe format` owns clang-format-like stdin, direct file arguments, newline file lists passed with `--files <path>` or `--files=<path>`, `-i`, `--dry-run`, `--style=file`, `--style=<path>`, `--style=file:<path>`, `--concurrency <n>` handling, parsing, parse-error rejection, checking, fixing, ignore-file filtering, and stdout rendering. Interactive file runs show completed file count and elapsed time while work is active, and final summaries report completed files and elapsed time.
- `src\tools\format.cpp` owns formatter command orchestration; the internal `src\tools\impl\format_*` formatter modules own the parser setup, model definitions, tree-sitter model builder helpers, preprocessor model helpers, and pretty printer.
- `CaseDashTools.exe format_model_dump` owns ad hoc formatter model debugging. It takes one source file path and writes YAML to stdout with each `SyntaxNode` represented by `kind`, plus `text` only when node text is non-empty and `children` only when child nodes exist; text values up to 60 bytes are YAML-quoted, and longer text values are emitted as their byte length.
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
