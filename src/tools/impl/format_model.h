#pragma once

#include <string>

struct ParseResult {
    bool ok = false;
    bool hasErrors = false;
    std::string errorNodeType;
    int errorLine = 0;
    int errorColumn = 0;
    std::string errorSnippet;
};

struct SyntaxNode {
    // TODO
};

struct FormatModel {
    ParseResult parse;
    SyntaxNode* root;
};
