#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "tools/format_lexer.h"

namespace tools::format {

struct ParseResult {
    bool ok = false;
    bool hasErrors = false;
    std::string errorNodeType;
    int errorLine = 0;
    int errorColumn = 0;
    std::string errorSnippet;
};

enum class SourceLayoutKind {
    Root,
    TemplateDeclaration,
    Assignment,
    DeclarationValue,
    Lambda,
    ConstructorInitializer,
    OperatorChain,
    StringLiteralSequence,
    IncludeRun,
    Group,
};

struct SourceLayoutNode {
    SourceLayoutKind kind = SourceLayoutKind::Root;
    size_t begin = kNoTokenIndex;
    size_t end = kNoTokenIndex;
    size_t index = kNoTokenIndex;
    size_t groupOpen = kNoTokenIndex;
    size_t groupClose = kNoTokenIndex;
    bool stopChildren = false;
    int depth = 0;
    size_t order = 0;
    std::vector<SourceLayoutNode> children;
};

struct FormatModel {
    ParseResult parse;
    std::vector<Token> tokens;
    SourceLayoutNode layout;
};

struct GroupPair {
    size_t open = 0;
    size_t close = 0;
};

enum class ChainKind {
    None,
    Ternary,
    Logical,
    Bitwise,
    Equality,
    Relational,
    Shift,
    Additive,
    Multiplicative,
};

enum class LayoutNodeKind {
    TemplateDeclaration,
    Assignment,
    DeclarationValue,
    Lambda,
    ConstructorInitializer,
    OperatorChain,
    StringLiteralSequence,
    Group,
};

struct LayoutNode {
    LayoutNodeKind kind = LayoutNodeKind::Group;
    size_t index = 0;
    GroupPair group{};
    int depth = 0;
    size_t order = 0;
};

struct LayoutTree {
    std::vector<LayoutNode> breakNodes;
};

}  // namespace tools::format
