#include "tools/impl/format_model_builder.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tools/impl/tools_common.h"

std::string SingleLineSnippet(std::string_view text, uint32_t startByte, uint32_t endByte) {
    if (startByte >= text.size()) {
        return {};
    }
    if (endByte <= startByte || endByte > text.size()) {
        endByte = static_cast<uint32_t>(std::min(text.size(), static_cast<size_t>(startByte) + 120));
    }
    std::string snippet(text.substr(startByte, std::min<uint32_t>(endByte - startByte, 120)));
    for (char& ch : snippet) {
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            ch = ' ';
        }
    }
    return tools::Trim(snippet);
}

TSNode FindFirstErrorNode(TSNode node) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        if (ts_node_has_error(child)) {
            return FindFirstErrorNode(child);
        }
    }
    if (std::strcmp(ts_node_type(node), "ERROR") == 0 || ts_node_is_missing(node)) {
        return node;
    }
    return node;
}

bool IsNewlineByte(char ch) {
    return ch == '\r' || ch == '\n';
}

size_t AdvanceNewline(std::string_view text, size_t index) {
    if (index < text.size() && text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
        return index + 2;
    }
    return std::min(index + 1, text.size());
}

bool IsPreprocessorLineStart(std::string_view text, size_t index) {
    if (index > text.size() || index != 0 && !IsNewlineByte(text[index - 1])) {
        return false;
    }
    while (index < text.size() && IsSpaceButNotNewline(text[index])) {
        ++index;
    }
    return index < text.size() && text[index] == '#';
}

size_t PreprocessorLineEnd(std::string_view text, size_t index) {
    while (index < text.size()) {
        const size_t lineStart = index;
        while (index < text.size() && !IsNewlineByte(text[index])) {
            ++index;
        }
        size_t lineEnd = index;
        while (lineEnd > lineStart && IsSpaceButNotNewline(text[lineEnd - 1])) {
            --lineEnd;
        }
        const bool continued = lineEnd > lineStart && text[lineEnd - 1] == '\\';
        if (!continued) {
            return index;
        }
        if (index < text.size()) {
            index = AdvanceNewline(text, index);
        }
    }
    return index;
}

size_t SkipFollowingNewline(std::string_view text, size_t index) {
    if (index < text.size() && IsNewlineByte(text[index])) {
        return AdvanceNewline(text, index);
    }
    return index;
}

std::string SourceText(std::string_view text, uint32_t begin, uint32_t end) {
    if (begin > text.size()) {
        begin = static_cast<uint32_t>(text.size());
    }
    if (end < begin) {
        end = begin;
    }
    if (end > text.size()) {
        end = static_cast<uint32_t>(text.size());
    }
    return std::string(text.substr(begin, end - begin));
}

struct SourceTokenLookup {
    std::map<size_t, size_t> byBegin;
    const std::vector<Token>* tokens = nullptr;
};

struct SourceLayoutBuildContext {
    std::string_view text;
    std::vector<Token>& tokens;
    SourceTokenLookup lookup;
};

bool IsGroupClose(std::string_view text) {
    return text == ")" || text == "]" || text == "}";
}

size_t SkipWhitespaceAndComments(std::string_view text, size_t index) {
    while (index < text.size()) {
        if (IsSpaceButNotNewline(text[index]) || IsNewlineByte(text[index])) {
            ++index;
            continue;
        }
        if (index + 1 < text.size() && text[index] == '/' && text[index + 1] == '/') {
            index += 2;
            while (index < text.size() && !IsNewlineByte(text[index])) {
                ++index;
            }
            continue;
        }
        if (index + 1 < text.size() && text[index] == '/' && text[index + 1] == '*') {
            index += 2;
            while (index + 1 < text.size() && (text[index] != '*' || text[index + 1] != '/')) {
                ++index;
            }
            if (index + 1 < text.size()) {
                index += 2;
            }
            continue;
        }
        break;
    }
    return index;
}

bool IsTrailingCommaToken(std::string_view text, const Token& token) {
    if (token.text != "," || token.sourceEnd == kNoTokenIndex) {
        return false;
    }
    const size_t next = SkipWhitespaceAndComments(text, token.sourceEnd);
    return next < text.size() && IsGroupClose(std::string_view(text).substr(next, 1));
}

void AppendModelToken(SourceLayoutBuildContext& context, Token token) {
    if (IsTrailingCommaToken(context.text, token)) {
        return;
    }
    if (token.sourceBegin != kNoTokenIndex) {
        context.lookup.byBegin.emplace(token.sourceBegin, context.tokens.size());
    }
    context.tokens.push_back(std::move(token));
}

void AppendSyntheticToken(SourceLayoutBuildContext& context, std::string text) {
    AppendModelToken(context, {TokenKind::Symbol, std::move(text)});
}

void AppendPreprocessorDirective(SourceLayoutBuildContext& context, size_t& index) {
    const size_t start = index;
    const size_t directiveEnd = PreprocessorLineEnd(context.text, index);
    AppendModelToken(
        context,
        {TokenKind::Preprocessor, std::string(context.text.substr(start, directiveEnd - start)), start, directiveEnd}
    );
    index = SkipFollowingNewline(context.text, directiveEnd);
}

size_t AppendSourceTrivia(SourceLayoutBuildContext& context, size_t begin, size_t end) {
    size_t index = begin;
    while (index < end) {
        if (IsPreprocessorLineStart(context.text, index)) {
            AppendPreprocessorDirective(context, index);
            continue;
        }
        if (IsNewlineByte(context.text[index])) {
            const size_t newlineEnd = AdvanceNewline(context.text, index);
            AppendModelToken(context, {TokenKind::Newline, "\n", index, newlineEnd});
            index = newlineEnd;
            continue;
        }
        ++index;
    }
    return index;
}

bool IsSimplePreprocessorNode(std::string_view type) {
    return type == "preproc_include" ||
        type == "preproc_def" ||
        type == "preproc_function_def" ||
        type == "preproc_call" ||
        type == "preproc_using";
}

bool IsAtomicTreeNode(std::string_view type, uint32_t childCount) {
    return childCount == 0 ||
        type == "comment" ||
        type == "char_literal" ||
        type == "string_literal" ||
        type == "raw_string_literal" ||
        type == "number_literal";
}

TokenKind ClassifyTreeToken(std::string_view type, std::string_view text) {
    if (type == "comment") {
        return tools::StartsWith(text, "//") ? TokenKind::LineComment : TokenKind::BlockComment;
    }
    if (type == "char_literal") {
        return TokenKind::CharLiteral;
    }
    if (type == "string_literal" || type == "raw_string_literal") {
        return TokenKind::StringLiteral;
    }
    if (type == "number_literal" || (!text.empty() && IsDigit(text.front()))) {
        return TokenKind::Number;
    }
    if (!text.empty() && IsIdentifierStart(text.front())) {
        return TokenKind::Word;
    }
    return TokenKind::Symbol;
}

void TrimLineCommentTerminator(std::string& text) {
    while (!text.empty() && IsNewlineByte(text.back())) {
        text.pop_back();
    }
}

bool TryAppendTreeToken(TSNode node, SourceLayoutBuildContext& context) {
    const std::string_view type = ts_node_type(node);
    const uint32_t childCount = ts_node_child_count(node);
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (start >= end) {
        return true;
    }
    if (IsPreprocessorLineStart(context.text, start)) {
        size_t index = start;
        AppendPreprocessorDirective(context, index);
        return true;
    }
    if (IsSimplePreprocessorNode(type)) {
        AppendModelToken(context, {TokenKind::Preprocessor, SourceText(context.text, start, end), start, end});
        return true;
    }
    if (IsAtomicTreeNode(type, childCount)) {
        std::string tokenText = SourceText(context.text, start, end);
        if (type == "comment" && tools::StartsWith(tokenText, "//")) {
            TrimLineCommentTerminator(tokenText);
        }
        AppendModelToken(context, {ClassifyTreeToken(type, tokenText), tokenText, start, end});
        return true;
    }
    return false;
}

ParseResult ParseTreeResult(TSNode root, std::string_view text) {
    ParseResult result;
    result.ok = true;
    result.hasErrors = ts_node_has_error(root);
    if (result.hasErrors) {
        const TSNode errorNode = FindFirstErrorNode(root);
        const TSPoint point = ts_node_start_point(errorNode);
        result.errorNodeType = ts_node_type(errorNode);
        result.errorLine = static_cast<int>(point.row) + 1;
        result.errorColumn = static_cast<int>(point.column) + 1;
        result.errorSnippet = SingleLineSnippet(text, ts_node_start_byte(errorNode), ts_node_end_byte(errorNode));
    }
    return result;
}

SourceTokenLookup BuildSourceTokenLookup(const std::vector<Token>& tokens) {
    SourceTokenLookup lookup;
    lookup.tokens = &tokens;
    for (size_t index = 0; index < tokens.size(); ++index) {
        if (tokens[index].sourceBegin != kNoTokenIndex) {
            lookup.byBegin.emplace(tokens[index].sourceBegin, index);
        }
    }
    return lookup;
}

std::optional < std::pair<
    size_t,
    size_t
> > TokenSpanForByteRange(const SourceTokenLookup& lookup, size_t begin, size_t end) {
    if (lookup.tokens == nullptr || begin >= end) {
        return std::nullopt;
    }
    auto current = lookup.byBegin.lower_bound(begin);
    if (current == lookup.byBegin.end() || current->first >= end) {
        return std::nullopt;
    }
    size_t first = current->second;
    size_t last = current->second;
    for (; current != lookup.byBegin.end() && current->first < end; ++current) {
        first = std::min(first, current->second);
        last = std::max(last, current->second);
    }
    if (first > last) {
        return std::nullopt;
    }
    return std::pair<size_t, size_t>{first, last + 1};
}

std::optional<std::pair<size_t, size_t>> TokenSpanForTreeNode(const SourceTokenLookup& lookup, TSNode node) {
    return TokenSpanForByteRange(lookup, ts_node_start_byte(node), ts_node_end_byte(node));
}

std::optional<size_t> FindFirstSourceTokenText(
    const SourceTokenLookup& lookup,
    size_t begin,
    size_t end,
    std::string_view text
) {
    if (lookup.tokens == nullptr) {
        return std::nullopt;
    }
    for (
        auto current = lookup.byBegin.lower_bound(begin);
        current != lookup.byBegin.end() && current->first < end;
        ++current
    ) {
        if ((*lookup.tokens)[current->second].text == text) {
            return current->second;
        }
    }
    return std::nullopt;
}

std::optional<size_t> FindLastSourceTokenText(
    const SourceTokenLookup& lookup,
    size_t begin,
    size_t end,
    std::string_view text
) {
    if (lookup.tokens == nullptr) {
        return std::nullopt;
    }
    std::optional<size_t> result;
    for (
        auto current = lookup.byBegin.lower_bound(begin);
        current != lookup.byBegin.end() && current->first < end;
        ++current
    ) {
        if ((*lookup.tokens)[current->second].text == text) {
            result = current->second;
        }
    }
    return result;
}

std::optional<size_t> FindFirstSourceTokenAnyText(
    const SourceTokenLookup& lookup,
    size_t begin,
    size_t end,
    std::initializer_list<std::string_view> texts
) {
    if (lookup.tokens == nullptr) {
        return std::nullopt;
    }
    for (
        auto current = lookup.byBegin.lower_bound(begin);
        current != lookup.byBegin.end() && current->first < end;
        ++current
    ) {
        const std::string& tokenText = (*lookup.tokens)[current->second].text;
        for (std::string_view text : texts) {
            if (tokenText == text) {
                return current->second;
            }
        }
    }
    return std::nullopt;
}

std::optional<size_t> FindDirectChildSourceTokenAnyText(
    TSNode node,
    const SourceTokenLookup& lookup,
    std::initializer_list<std::string_view> texts
) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        const std::optional<std::pair<size_t, size_t>> span = TokenSpanForTreeNode(lookup, child);
        if (!span || span->second != span->first + 1 || lookup.tokens == nullptr) {
            continue;
        }
        const std::string& tokenText = (*lookup.tokens)[span->first].text;
        for (std::string_view text : texts) {
            if (tokenText == text) {
                return span->first;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> FindDirectChildSourceTokenText(
    TSNode node,
    const SourceTokenLookup& lookup,
    std::initializer_list<std::string_view> texts
) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        const std::optional<std::pair<size_t, size_t>> span = TokenSpanForTreeNode(lookup, child);
        if (!span || span->second != span->first + 1 || lookup.tokens == nullptr) {
            continue;
        }
        const std::string& tokenText = (*lookup.tokens)[span->first].text;
        for (std::string_view text : texts) {
            if (tokenText == text) {
                return tokenText;
            }
        }
    }
    return std::nullopt;
}

std::optional<size_t> FindLastSourceTokenAnyText(
    const SourceTokenLookup& lookup,
    size_t begin,
    size_t end,
    std::initializer_list<std::string_view> texts
) {
    if (lookup.tokens == nullptr) {
        return std::nullopt;
    }
    std::optional<size_t> result;
    for (
        auto current = lookup.byBegin.lower_bound(begin);
        current != lookup.byBegin.end() && current->first < end;
        ++current
    ) {
        const std::string& tokenText = (*lookup.tokens)[current->second].text;
        for (std::string_view text : texts) {
            if (tokenText == text) {
                result = current->second;
            }
        }
    }
    return result;
}

bool TreeNodeHasDirectChildType(TSNode node, std::string_view type) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        if (std::string_view(ts_node_type(ts_node_child(node, index))) == type) {
            return true;
        }
    }
    return false;
}

std::optional<TSNode> FindDirectChildType(TSNode node, std::string_view type) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        TSNode child = ts_node_child(node, index);
        if (std::string_view(ts_node_type(child)) == type) {
            return child;
        }
    }
    return std::nullopt;
}

bool IsTreeGroupNodeType(std::string_view type) {
    return type == "argument_list" ||
        type == "parameter_list" ||
        type == "requires_parameter_list" ||
        type == "condition_clause" ||
        type == "parenthesized_expression" ||
        type == "initializer_list" ||
        type == "subscript_argument_list" ||
        type == "lambda_capture_specifier" ||
        type == "structured_binding_declarator" ||
        type == "template_argument_list" ||
        type == "template_parameter_list";
}

bool IsTreeControlHeaderNodeType(std::string_view type) {
    return type == "if_statement" ||
        type == "for_statement" ||
        type == "for_range_loop" ||
        type == "while_statement" ||
        type == "switch_statement";
}

std::string_view TreeGroupOpenDelimiter(std::string_view type) {
    if (
        type == "subscript_argument_list" ||
        type == "lambda_capture_specifier" ||
        type == "structured_binding_declarator"
    ) {
        return "[";
    }
    if (type == "initializer_list") {
        return "{";
    }
    if (type == "template_argument_list" || type == "template_parameter_list") {
        return "<";
    }
    return "(";
}

bool IsTreeGroupCloseDelimiter(std::string_view type, std::string_view text) {
    if (
        type == "subscript_argument_list" ||
        type == "lambda_capture_specifier" ||
        type == "structured_binding_declarator"
    ) {
        return text == "]";
    }
    if (type == "initializer_list") {
        return text == "}";
    }
    if (type == "template_argument_list" || type == "template_parameter_list") {
        return text == ">" || text == ">>";
    }
    return text == ")";
}

bool IsAssignmentTreeNodeType(std::string_view type) {
    return type == "assignment_expression" || type == "init_declarator" || type == "condition_declaration";
}

std::optional<std::string> FindBinaryOperatorText(TSNode node, const SourceTokenLookup& lookup) {
    if (std::string_view(ts_node_type(node)) != "binary_expression") {
        return std::nullopt;
    }
    return FindDirectChildSourceTokenText(
        node,
        lookup,
        {"&&", "||", "|", "^", "==", "!=", "<", ">", "<=", ">=", "<<", ">>", "+", "*"}
    );
}

bool HasDirectBinaryChildWithOperator(TSNode node, const SourceTokenLookup& lookup, std::string_view text) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        const std::optional<std::string> childOperator = FindBinaryOperatorText(child, lookup);
        if (childOperator && *childOperator == text) {
            return true;
        }
    }
    return false;
}

std::optional<size_t> FindMatchingSourceCloseToken(const SourceTokenLookup& lookup, size_t openIndex, size_t endByte) {
    if (lookup.tokens == nullptr || openIndex >= lookup.tokens->size()) {
        return std::nullopt;
    }
    const std::string& openText = (*lookup.tokens)[openIndex].text;
    std::string closeText;
    if (openText == "(") {
        closeText = ")";
    } else if (openText == "[") {
        closeText = "]";
    } else if (openText == "{") {
        closeText = "}";
    } else {
        return std::nullopt;
    }
    int depth = 0;
    for (
        auto current = lookup.byBegin.lower_bound((*lookup.tokens)[openIndex].sourceBegin);
        current != lookup.byBegin.end() && current->first < endByte;
        ++current
    ) {
        const Token& token = (*lookup.tokens)[current->second];
        if (token.text == openText) {
            ++depth;
        } else if (token.text == closeText) {
            --depth;
            if (depth == 0) {
                return current->second;
            }
        }
    }
    return std::nullopt;
}

std::optional<SourceLayoutNode> MakeSourceLayoutNode(TSNode node, const SourceTokenLookup& lookup) {
    const std::string_view type = ts_node_type(node);
    const size_t nodeBegin = ts_node_start_byte(node);
    const size_t nodeEnd = ts_node_end_byte(node);
    const std::optional<std::pair<size_t, size_t>> span = TokenSpanForByteRange(lookup, nodeBegin, nodeEnd);
    if (!span) {
        return std::nullopt;
    }

    SourceLayoutNode source;
    source.begin = span->first;
    source.end = span->second;

    if (type == "template_declaration" || type == "requires_clause") {
        source.kind = SourceLayoutKind::TemplateDeclaration;
        return source;
    }
    if (type == "lambda_expression") {
        std::optional<size_t> bodyOpen;
        if (const std::optional<TSNode> body = FindDirectChildType(node, "compound_statement")) {
            bodyOpen = FindFirstSourceTokenText(lookup, ts_node_start_byte(*body), ts_node_end_byte(*body), "{");
        }
        if (!bodyOpen) {
            return std::nullopt;
        }
        source.kind = SourceLayoutKind::Lambda;
        source.index = *bodyOpen;
        return source;
    }
    if (type == "field_initializer_list") {
        const std::optional<size_t> colon = FindFirstSourceTokenText(lookup, nodeBegin, nodeEnd, ":");
        if (!colon) {
            return std::nullopt;
        }
        source.kind = SourceLayoutKind::ConstructorInitializer;
        source.index = *colon;
        return source;
    }
    if (IsAssignmentTreeNodeType(type)) {
        const std::optional<size_t> assignment = FindDirectChildSourceTokenAnyText(
            node,
            lookup,
            {"=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|=", "and_eq", "or_eq", "xor_eq"}
        );
        if (assignment) {
            source.kind = SourceLayoutKind::Assignment;
            source.index = *assignment;
            return source;
        }
        if (type == "init_declarator" && (
            TreeNodeHasDirectChildType(node, "initializer_list") || TreeNodeHasDirectChildType(node, "argument_list")
        )) {
            source.kind = SourceLayoutKind::DeclarationValue;
            source.index = span->first;
            return source;
        }
    }
    if (type == "conditional_expression") {
        source.kind = SourceLayoutKind::OperatorChain;
        source.stopChildren = TreeNodeHasDirectChildType(node, "conditional_expression");
        return source;
    }
    if (type == "binary_expression") {
        const std::optional<std::string> breakOperator = FindBinaryOperatorText(node, lookup);
        if (!breakOperator) {
            return std::nullopt;
        }
        source.kind = SourceLayoutKind::OperatorChain;
        source.stopChildren = HasDirectBinaryChildWithOperator(node, lookup, *breakOperator);
        return source;
    }
    if (type == "concatenated_string") {
        source.kind = SourceLayoutKind::StringLiteralSequence;
        return source;
    }
    if (IsTreeControlHeaderNodeType(type)) {
        const std::optional<size_t> open = FindFirstSourceTokenText(lookup, nodeBegin, nodeEnd, "(");
        if (!open || lookup.tokens == nullptr) {
            return std::nullopt;
        }
        const std::optional<size_t> close = FindMatchingSourceCloseToken(lookup, *open, nodeEnd);
        if (!close || *close <= *open || *close >= lookup.tokens->size()) {
            return std::nullopt;
        }
        source.kind = SourceLayoutKind::Group;
        source.begin = *open;
        source.end = *close + 1;
        source.groupOpen = *open;
        source.groupClose = *close;
        return source;
    }
    if (IsTreeGroupNodeType(type)) {
        const std::optional<size_t> open =
            FindFirstSourceTokenText(lookup, nodeBegin, nodeEnd, TreeGroupOpenDelimiter(type));
        std::optional<size_t> close;
        if (lookup.tokens != nullptr) {
            for (
                auto current = lookup.byBegin.lower_bound(nodeBegin);
                current != lookup.byBegin.end() && current->first < nodeEnd;
                ++current
            ) {
                if (IsTreeGroupCloseDelimiter(type, (*lookup.tokens)[current->second].text)) {
                    close = current->second;
                }
            }
        }
        if (!open || !close || *open >= *close) {
            return std::nullopt;
        }
        if (
            type == "template_parameter_list" &&
            lookup.tokens != nullptr &&
            *open > 0 &&
            (*lookup.tokens)[*open - 1].text == "template"
        ) {
            source.kind = SourceLayoutKind::TemplateDeclaration;
            source.begin = *open - 1;
            source.end = span->second;
            return source;
        }
        source.kind = SourceLayoutKind::Group;
        source.begin = *open;
        source.end = *close + 1;
        source.groupOpen = *open;
        source.groupClose = *close;
        return source;
    }
    return std::nullopt;
}

std::optional<TSNode> ChildByFieldName(TSNode node, const char* fieldName) {
    TSNode child = ts_node_child_by_field_name(node, fieldName, static_cast<uint32_t>(std::strlen(fieldName)));
    if (ts_node_is_null(child)) {
        return std::nullopt;
    }
    return child;
}

bool IsNodeEqual(TSNode left, TSNode right) {
    return ts_node_eq(left, right);
}

bool IsCompoundStatement(TSNode node) {
    return std::string_view(ts_node_type(node)) == "compound_statement";
}

bool ShouldSynthesizeControlBodyBraces(TSNode parent, TSNode child) {
    const std::string_view parentType = ts_node_type(parent);
    const std::string_view childType = ts_node_type(child);
    if (IsCompoundStatement(child)) {
        return false;
    }
    if (parentType == "if_statement") {
        const std::optional<TSNode> consequence = ChildByFieldName(parent, "consequence");
        return consequence && IsNodeEqual(*consequence, child);
    }
    if (parentType == "else_clause") {
        const std::optional<TSNode> body = ChildByFieldName(parent, "body");
        return body && IsNodeEqual(*body, child) && childType != "if_statement";
    }
    if (
        parentType == "while_statement" ||
        parentType == "for_statement" ||
        parentType == "for_range_loop" ||
        parentType == "switch_statement" ||
        parentType == "do_statement"
    ) {
        const std::optional<TSNode> body = ChildByFieldName(parent, "body");
        return body && IsNodeEqual(*body, child);
    }
    return false;
}

void AppendLayoutChildren(SourceLayoutNode& parent, std::vector<SourceLayoutNode> children) {
    parent.children.insert(
        parent.children.end(),
        std::make_move_iterator(children.begin()),
        std::make_move_iterator(children.end())
    );
}

void BuildSourceLayoutForNode(TSNode node, SourceLayoutBuildContext& context, SourceLayoutNode& parent);

void BuildSourceLayoutForChildren(TSNode node, SourceLayoutBuildContext& context, SourceLayoutNode& parent) {
    const uint32_t childCount = ts_node_child_count(node);
    size_t cursor = ts_node_start_byte(node);
    const size_t nodeEnd = ts_node_end_byte(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        const size_t childStart = ts_node_start_byte(child);
        const size_t childEnd = ts_node_end_byte(child);
        if (childEnd <= cursor) {
            continue;
        }
        const bool synthesizeBraces = ShouldSynthesizeControlBodyBraces(node, child);
        if (synthesizeBraces) {
            AppendSyntheticToken(context, "{");
        }
        if (childStart > cursor) {
            cursor = AppendSourceTrivia(context, cursor, childStart);
        }
        if (childStart >= cursor) {
            BuildSourceLayoutForNode(child, context, parent);
            cursor = std::max(cursor, childEnd);
        }
        if (synthesizeBraces) {
            AppendSyntheticToken(context, "}");
        }
    }
    if (cursor < nodeEnd) {
        (void)AppendSourceTrivia(context, cursor, nodeEnd);
    }
}

void BuildSourceLayoutForNode(TSNode node, SourceLayoutBuildContext& context, SourceLayoutNode& parent) {
    if (ts_node_start_byte(node) == ts_node_end_byte(node)) {
        return;
    }
    SourceLayoutNode childContainer;
    if (!TryAppendTreeToken(node, context)) {
        BuildSourceLayoutForChildren(node, context, childContainer);
    }
    std::optional<SourceLayoutNode> source = MakeSourceLayoutNode(node, context.lookup);
    if (source) {
        source->children = std::move(childContainer.children);
        parent.children.push_back(std::move(*source));
        return;
    }
    AppendLayoutChildren(parent, std::move(childContainer.children));
}

bool IsTopLevelIncludeNode(TSNode node) {
    return std::string_view(ts_node_type(node)) == "preproc_include";
}

void CloseIncludeRun(SourceLayoutNode& root, std::optional<size_t>& begin, size_t end) {
    if (!begin || *begin >= end) {
        begin.reset();
        return;
    }
    SourceLayoutNode includeRun;
    includeRun.kind = SourceLayoutKind::IncludeRun;
    includeRun.begin = *begin;
    includeRun.end = end;
    root.children.push_back(std::move(includeRun));
    begin.reset();
}

void BuildSourceLayoutRootChildren(TSNode root, SourceLayoutBuildContext& context, SourceLayoutNode& treeRoot) {
    const uint32_t childCount = ts_node_child_count(root);
    size_t cursor = ts_node_start_byte(root);
    const size_t rootEnd = ts_node_end_byte(root);
    std::optional<size_t> includeRunBegin;
    size_t includeRunEnd = 0;
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(root, index);
        const size_t childStart = ts_node_start_byte(child);
        const size_t childEnd = ts_node_end_byte(child);
        if (childEnd <= cursor) {
            continue;
        }
        const bool isInclude = IsTopLevelIncludeNode(child);
        if (!isInclude) {
            CloseIncludeRun(treeRoot, includeRunBegin, includeRunEnd);
        }
        if (childStart > cursor) {
            cursor = AppendSourceTrivia(context, cursor, childStart);
        }
        if (childStart < cursor) {
            continue;
        }
        if (isInclude && !includeRunBegin) {
            includeRunBegin = context.tokens.size();
        }
        BuildSourceLayoutForNode(child, context, treeRoot);
        cursor = std::max(cursor, childEnd);
        if (isInclude) {
            includeRunEnd = context.tokens.size();
        }
    }
    CloseIncludeRun(treeRoot, includeRunBegin, includeRunEnd);
    if (cursor < rootEnd) {
        (void)AppendSourceTrivia(context, cursor, rootEnd);
    }
}

void AssignSourceLayoutMetadata(SourceLayoutNode& parent, int depth, size_t& order) {
    for (SourceLayoutNode& child : parent.children) {
        child.depth = depth;
        child.order = order++;
        AssignSourceLayoutMetadata(child, depth + 1, order);
    }
}

SourceLayoutNode BuildSourceLayoutRoot(TSNode root, std::string_view text, std::vector<Token>& tokens) {
    SourceLayoutNode treeRoot;
    treeRoot.kind = SourceLayoutKind::Root;
    treeRoot.begin = 0;
    SourceLayoutBuildContext context{text, tokens, {}};
    context.lookup.tokens = &tokens;
    BuildSourceLayoutRootChildren(root, context, treeRoot);
    treeRoot.end = tokens.size();
    size_t order = 0;
    AssignSourceLayoutMetadata(treeRoot, 0, order);
    return treeRoot;
}

void AnnotateTokenIndexesAndGroups(std::vector<Token>& tokens);

void AnnotateTokenIndexesAndGroups(std::vector<Token>& tokens) {
    std::vector<size_t> stack;
    for (size_t index = 0; index < tokens.size(); ++index) {
        tokens[index].modelIndex = index;
        tokens[index].matchingIndex = kNoTokenIndex;
        const std::string& text = tokens[index].text;
        if (text == "(" || text == "[" || text == "{") {
            stack.push_back(index);
            continue;
        }
        if (text != ")" && text != "]" && text != "}") {
            continue;
        }
        const std::string open = text == ")" ? "(" : text == "]" ? "[" : "{";
        while (!stack.empty() && tokens[stack.back()].text != open) {
            stack.pop_back();
        }
        if (stack.empty()) {
            continue;
        }
        const size_t openIndex = stack.back();
        stack.pop_back();
        tokens[openIndex].matchingIndex = index;
        tokens[index].matchingIndex = openIndex;
    }
}
