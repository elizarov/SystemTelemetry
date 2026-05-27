#include "tools/impl/format_pretty_printer.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

enum class PrintTokenKind {
    Known,
    Free,
    Comment,
    TrailingComment,
    BlankLine,
    Preprocessor,
};

enum class BraceRole {
    Compact,
    Block,
    Enum,
    Namespace,
    CaseBlock,
};

struct PrintToken {
    PrintTokenKind kind = PrintTokenKind::Free;
    KnownToken known = KnownToken::Unknown;
    std::string_view text;
    SyntaxTreeKind treeKind = SyntaxTreeKind::Unknown;
    SyntaxTreeKind parentKind = SyntaxTreeKind::Unknown;
    SyntaxTreeKind grandParentKind = SyntaxTreeKind::Unknown;
    bool inTemplateDeclaration = false;
    bool inRequiresClause = false;
    const SyntaxNode* node = nullptr;
};

bool IsPreprocessorNode(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::PreprocCall ||
        kind == SyntaxTreeKind::PreprocDef ||
        kind == SyntaxTreeKind::PreprocFunctionDef ||
        kind == SyntaxTreeKind::PreprocInclude ||
        kind == SyntaxTreeKind::PreprocUsing;
}

bool IsCommentToken(PrintTokenKind kind) {
    return kind == PrintTokenKind::Comment || kind == PrintTokenKind::TrailingComment;
}

bool IsWordLike(const PrintToken& token) {
    if (token.kind == PrintTokenKind::Free) {
        return true;
    }
    return token.kind == PrintTokenKind::Known && KnownTokenHasClass(token.known, TokenClass::Keyword);
}

bool IsStringLike(const PrintToken& token) {
    return token.treeKind == SyntaxTreeKind::StringLiteral ||
        token.treeKind == SyntaxTreeKind::RawStringLiteral ||
        token.treeKind == SyntaxTreeKind::CharacterLiteral;
}

bool IsDeclaratorReferenceParent(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::PointerDeclarator ||
        kind == SyntaxTreeKind::AbstractPointerDeclarator ||
        kind == SyntaxTreeKind::ReferenceDeclarator ||
        kind == SyntaxTreeKind::AbstractReferenceDeclarator ||
        kind == SyntaxTreeKind::HandleDeclarator ||
        kind == SyntaxTreeKind::AbstractHandleDeclarator ||
        kind == SyntaxTreeKind::MemberPointerDeclarator;
}

bool IsReferenceToken(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known &&
        KnownTokenHasClass(token.known, TokenClass::DeclaratorReferenceToken);
}

bool IsDeclaratorBindingToken(const PrintToken& token) {
    return IsDeclaratorReferenceParent(token.parentKind) && IsReferenceToken(token);
}

bool IsUnaryContext(const PrintToken& token) {
    return token.parentKind == SyntaxTreeKind::UnaryExpression;
}

bool IsBinaryContext(const PrintToken& token) {
    return token.parentKind == SyntaxTreeKind::BinaryExpression ||
        token.parentKind == SyntaxTreeKind::AssignmentExpression ||
        token.parentKind == SyntaxTreeKind::ConditionalExpression;
}

BraceRole RoleForBraceParent(SyntaxTreeKind parentKind) {
    switch (parentKind) {
    case SyntaxTreeKind::CompoundStatement:
    case SyntaxTreeKind::FieldDeclarationList:
    case SyntaxTreeKind::DeclarationList:
        return BraceRole::Block;
    case SyntaxTreeKind::EnumeratorList:
        return BraceRole::Enum;
    default:
        return BraceRole::Compact;
    }
}

BraceRole RoleForBrace(const PrintToken& token) {
    if (token.parentKind == SyntaxTreeKind::DeclarationList &&
        token.grandParentKind == SyntaxTreeKind::NamespaceDefinition) {
        return BraceRole::Namespace;
    }
    return RoleForBraceParent(token.parentKind);
}

void AppendTokens(
    const SyntaxNode& node,
    SyntaxTreeKind parentKind,
    SyntaxTreeKind grandParentKind,
    bool inTemplateDeclaration,
    bool inRequiresClause,
    std::vector<PrintToken>& tokens
) {
    const bool childInTemplateDeclaration = inTemplateDeclaration ||
        node.treeKind == SyntaxTreeKind::TemplateDeclaration;
    const bool childInRequiresClause = inRequiresClause ||
        node.treeKind == SyntaxTreeKind::RequiresClause;
    switch (node.kind) {
    case SyntaxNodeKind::BlankLine:
        tokens.push_back({.kind = PrintTokenKind::BlankLine, .node = &node});
        return;
    case SyntaxNodeKind::Comment:
    case SyntaxNodeKind::TrailingComment:
        tokens.push_back({
            .kind = node.kind == SyntaxNodeKind::TrailingComment ? PrintTokenKind::TrailingComment :
                                                                    PrintTokenKind::Comment,
            .text = node.text,
            .treeKind = node.treeKind,
            .parentKind = parentKind,
            .grandParentKind = grandParentKind,
            .inTemplateDeclaration = childInTemplateDeclaration,
            .inRequiresClause = childInRequiresClause,
            .node = &node,
        });
        return;
    case SyntaxNodeKind::KnownToken:
        tokens.push_back({
            .kind = PrintTokenKind::Known,
            .known = node.known,
            .text = KnownTokenText(node.known),
            .treeKind = node.treeKind,
            .parentKind = parentKind,
            .grandParentKind = grandParentKind,
            .inTemplateDeclaration = childInTemplateDeclaration,
            .inRequiresClause = childInRequiresClause,
            .node = &node,
        });
        return;
    case SyntaxNodeKind::FreeToken:
        tokens.push_back({
            .kind = PrintTokenKind::Free,
            .text = node.text,
            .treeKind = node.treeKind,
            .parentKind = parentKind,
            .grandParentKind = grandParentKind,
            .inTemplateDeclaration = childInTemplateDeclaration,
            .inRequiresClause = childInRequiresClause,
            .node = &node,
        });
        return;
    case SyntaxNodeKind::Tree:
        if (IsPreprocessorNode(node.treeKind)) {
            tokens.push_back({
                .kind = PrintTokenKind::Preprocessor,
                .text = node.text,
                .treeKind = node.treeKind,
                .parentKind = parentKind,
                .grandParentKind = grandParentKind,
                .inTemplateDeclaration = childInTemplateDeclaration,
                .inRequiresClause = childInRequiresClause,
                .node = &node,
            });
            return;
        }
        for (const std::unique_ptr<SyntaxNode>& child : node.children) {
            AppendTokens(
                *child,
                node.treeKind,
                parentKind,
                childInTemplateDeclaration,
                childInRequiresClause,
                tokens
            );
        }
        return;
    }
}

bool IsNewline(char ch) {
    return ch == '\r' || ch == '\n';
}

std::string CollapseSourceWhitespace(std::string_view text) {
    std::string result;
    bool pendingSpace = false;
    bool inString = false;
    bool inChar = false;
    for (size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        const char next = index + 1 < text.size() ? text[index + 1] : '\0';
        if (!inString && !inChar && ch == '\\' && IsNewline(next)) {
            pendingSpace = true;
            ++index;
            if (next == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
            continue;
        }
        if (!inString && !inChar && (ch == ' ' || ch == '\t' || IsNewline(ch))) {
            pendingSpace = true;
            if (ch == '\r' && next == '\n') {
                ++index;
            }
            continue;
        }
        if (pendingSpace && !result.empty()) {
            result.push_back(' ');
        }
        pendingSpace = false;
        result.push_back(ch);
        if (ch == '\\' && (inString || inChar) && index + 1 < text.size()) {
            result.push_back(text[index + 1]);
            ++index;
            continue;
        }
        if (ch == '"' && !inChar) {
            inString = !inString;
        } else if (ch == '\'' && !inString) {
            inChar = !inChar;
        }
    }
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool IsAccessKeyword(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known && KnownTokenHasClass(token.known, TokenClass::AccessKeyword);
}

bool IsCaseLabelKeyword(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known &&
        token.parentKind == SyntaxTreeKind::CaseStatement &&
        (token.known == KnownToken::KeywordCase || token.known == KnownToken::KeywordDefault);
}

bool IsParenthesizedDeclarator(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::ParenthesizedDeclarator ||
        kind == SyntaxTreeKind::AbstractParenthesizedDeclarator;
}

bool IsCompilerCallModifierStart(const PrintToken* token) {
    return token != nullptr &&
        token->kind == PrintTokenKind::Known &&
        (token->known == KnownToken::KeywordCdecl || token->known == KnownToken::KeywordDeclspec);
}

class Printer {
public:
    explicit Printer(const FormatterConfig& config) :
        indentWidth_(std::max(1, config.indentWidth)) {}

    std::string Print(const std::vector<PrintToken>& tokens) {
        for (size_t index = 0; index < tokens.size(); ++index) {
            const PrintToken* previous = PreviousToken(tokens, index);
            const PrintToken* next = NextToken(tokens, index);
            const PrintToken* rawNext = RawNextToken(tokens, index);
            PrintOne(tokens[index], previous, next, rawNext);
        }
        FinishLine();
        if (!output_.empty() && output_.back() != '\n') {
            output_.push_back('\n');
        }
        return output_;
    }

private:
    int indentWidth_ = 4;
    std::string output_;
    int indentLevel_ = 0;
    bool atLineStart_ = true;
    bool lineHasText_ = false;
    int compactRightBraceSkips_ = 0;
    int switchDepth_ = 0;
    int parenDepth_ = 0;
    int bracketDepth_ = 0;
    std::vector<BraceRole> braceStack_;
    std::vector<int> braceParenDepthStack_;
    std::vector<int> activeCaseBodySwitchDepths_;

    static const PrintToken* PreviousToken(const std::vector<PrintToken>& tokens, size_t index) {
        while (index > 0) {
            --index;
            if (tokens[index].kind != PrintTokenKind::BlankLine && !IsCommentToken(tokens[index].kind)) {
                return &tokens[index];
            }
        }
        return nullptr;
    }

    static const PrintToken* NextToken(const std::vector<PrintToken>& tokens, size_t index) {
        for (++index; index < tokens.size(); ++index) {
            if (tokens[index].kind != PrintTokenKind::BlankLine && !IsCommentToken(tokens[index].kind)) {
                return &tokens[index];
            }
        }
        return nullptr;
    }

    static const PrintToken* RawNextToken(const std::vector<PrintToken>& tokens, size_t index) {
        return index + 1 < tokens.size() ? &tokens[index + 1] : nullptr;
    }

    void TrimTrailingSpaces() {
        while (!output_.empty() && output_.back() == ' ') {
            output_.pop_back();
        }
    }

    void FinishLine() {
        TrimTrailingSpaces();
    }

    void NewLine() {
        FinishLine();
        if (output_.empty() || output_.back() != '\n') {
            output_.push_back('\n');
        }
        atLineStart_ = true;
        lineHasText_ = false;
    }

    void BlankLine() {
        NewLine();
        if (output_.size() < 2 || output_[output_.size() - 2] != '\n') {
            output_.push_back('\n');
        }
        atLineStart_ = true;
        lineHasText_ = false;
    }

    void WriteIndentIfNeeded() {
        if (!atLineStart_) {
            return;
        }
        output_.append(static_cast<size_t>(indentLevel_ * indentWidth_), ' ');
        atLineStart_ = false;
    }

    void WriteWithIndentOffset(std::string_view text, int indentOffset) {
        if (!atLineStart_) {
            output_.append(text);
            lineHasText_ = lineHasText_ || !text.empty();
            return;
        }
        const int adjustedIndent = std::max(0, indentLevel_ + indentOffset);
        output_.append(static_cast<size_t>(adjustedIndent * indentWidth_), ' ');
        atLineStart_ = false;
        output_.append(text);
        lineHasText_ = lineHasText_ || !text.empty();
    }

    void Write(std::string_view text) {
        WriteIndentIfNeeded();
        output_.append(text);
        lineHasText_ = lineHasText_ || !text.empty();
    }

    void CloseCaseBodyIndentIfNeeded() {
        if (!activeCaseBodySwitchDepths_.empty() && activeCaseBodySwitchDepths_.back() == switchDepth_) {
            indentLevel_ = std::max(0, indentLevel_ - 1);
            activeCaseBodySwitchDepths_.pop_back();
        }
    }

    void Space() {
        if (!atLineStart_ && !output_.empty() && output_.back() != ' ' && output_.back() != '\n') {
            output_.push_back(' ');
        }
    }

    bool InEnumBody() const {
        return !braceStack_.empty() && braceStack_.back() == BraceRole::Enum;
    }

    bool ShouldBreakAfterSemicolon() const {
        if (braceParenDepthStack_.empty()) {
            return parenDepth_ == 0;
        }
        return parenDepth_ <= braceParenDepthStack_.back();
    }

    bool ShouldSpaceBetween(const PrintToken* previous, const PrintToken& current) const {
        if (previous == nullptr || previous->kind == PrintTokenKind::Preprocessor ||
            current.kind == PrintTokenKind::Preprocessor) {
            return false;
        }
        if (previous->kind != PrintTokenKind::Known && current.kind != PrintTokenKind::Known) {
            return IsWordLike(*previous) && IsWordLike(current);
        }
        const KnownToken prev = previous->kind == PrintTokenKind::Known ? previous->known : KnownToken::Unknown;
        const KnownToken cur = current.kind == PrintTokenKind::Known ? current.known : KnownToken::Unknown;

        if (IsStringLike(*previous) && IsStringLike(current)) {
            return true;
        }
        if ((cur == KnownToken::Arrow && current.parentKind == SyntaxTreeKind::TrailingReturnType) ||
            (prev == KnownToken::Arrow && previous->parentKind == SyntaxTreeKind::TrailingReturnType)) {
            return true;
        }
        if (cur == KnownToken::Dot && current.parentKind == SyntaxTreeKind::FieldDesignator &&
            prev == KnownToken::Comma) {
            return true;
        }
        if (cur == KnownToken::RightParen || cur == KnownToken::RightBracket || cur == KnownToken::Comma ||
            cur == KnownToken::Semicolon || cur == KnownToken::ColonColon || cur == KnownToken::Dot ||
            cur == KnownToken::Arrow || cur == KnownToken::DotStar || cur == KnownToken::ArrowStar) {
            if (cur == KnownToken::ColonColon &&
                (KnownTokenHasClass(prev, TokenClass::Keyword) ||
                 KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
                 prev == KnownToken::KeywordReturn)) {
                return true;
            }
            return false;
        }
        if (prev == KnownToken::LeftParen || prev == KnownToken::LeftBracket || prev == KnownToken::ColonColon ||
            prev == KnownToken::Dot || prev == KnownToken::Arrow || prev == KnownToken::DotStar ||
            prev == KnownToken::ArrowStar || prev == KnownToken::Tilde) {
            return false;
        }
        if (prev == KnownToken::KeywordOperator && cur != KnownToken::LeftParen) {
            return false;
        }
        if (prev == KnownToken::KeywordVirtual && cur == KnownToken::Tilde) {
            return true;
        }
        if (cur == KnownToken::LeftParen) {
            if (IsParenthesizedDeclarator(current.parentKind)) {
                return true;
            }
            if (previous->parentKind == SyntaxTreeKind::OperatorName ||
                previous->parentKind == SyntaxTreeKind::OperatorCast) {
                return false;
            }
            if (previous->kind == PrintTokenKind::Known &&
                (KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
                 (KnownTokenHasClass(prev, TokenClass::BinaryOperator) && IsBinaryContext(*previous)) ||
                 prev == KnownToken::Comma ||
                 prev == KnownToken::KeywordReturn ||
                 (prev == KnownToken::Colon && previous->parentKind == SyntaxTreeKind::ConditionalExpression) ||
                 prev == KnownToken::Question)) {
                return true;
            }
            return previous->kind == PrintTokenKind::Known &&
                KnownTokenHasClass(prev, TokenClass::ControlKeyword);
        }
        if (cur == KnownToken::LeftBracket) {
            if (current.parentKind == SyntaxTreeKind::StructuredBindingDeclarator) {
                return true;
            }
            if (current.parentKind == SyntaxTreeKind::LambdaCaptureSpecifier) {
                return previous->kind == PrintTokenKind::Known &&
                    (KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
                     prev == KnownToken::Comma ||
                     prev == KnownToken::KeywordReturn ||
                     prev == KnownToken::Question ||
                     (prev == KnownToken::Colon && previous->parentKind == SyntaxTreeKind::ConditionalExpression));
            }
            return false;
        }
        if (cur == KnownToken::LeftBrace) {
            if (current.parentKind == SyntaxTreeKind::InitializerList) {
                return previous->kind == PrintTokenKind::Known &&
                    (KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
                     prev == KnownToken::Comma ||
                     prev == KnownToken::KeywordReturn ||
                     prev == KnownToken::Question ||
                     prev == KnownToken::Colon);
            }
            if (current.parentKind == SyntaxTreeKind::CompoundStatement ||
                current.parentKind == SyntaxTreeKind::FieldDeclarationList ||
                current.parentKind == SyntaxTreeKind::DeclarationList ||
                current.parentKind == SyntaxTreeKind::EnumeratorList) {
                return true;
            }
            return prev == KnownToken::KeywordReturn;
        }
        if (prev == KnownToken::Comma || prev == KnownToken::Semicolon || prev == KnownToken::Question) {
            return true;
        }
        if (prev == KnownToken::Ellipsis && IsWordLike(current)) {
            return true;
        }
        if (cur == KnownToken::Question) {
            return true;
        }
        if (prev == KnownToken::Colon) {
            return current.parentKind != SyntaxTreeKind::CaseStatement &&
                !KnownTokenHasClass(previous->known, TokenClass::AccessKeyword);
        }
        if (cur == KnownToken::Less && prev == KnownToken::KeywordTemplate) {
            return true;
        }
        if (cur == KnownToken::Colon) {
            if (previous->known == KnownToken::KeywordDefault ||
                KnownTokenHasClass(previous->known, TokenClass::AccessKeyword)) {
                return false;
            }
            return current.parentKind != SyntaxTreeKind::CaseStatement;
        }
        if (IsDeclaratorBindingToken(current)) {
            return false;
        }
        if (IsDeclaratorBindingToken(*previous)) {
            if (cur == KnownToken::Greater) {
                return false;
            }
            if (IsParenthesizedDeclarator(previous->grandParentKind)) {
                return false;
            }
            return !IsDeclaratorBindingToken(current);
        }
        if (prev == KnownToken::KeywordReturn && current.kind == PrintTokenKind::Known &&
            KnownTokenHasClass(cur, TokenClass::UnaryOperator)) {
            return true;
        }
        if (current.kind == PrintTokenKind::Known &&
            (KnownTokenHasClass(cur, TokenClass::AssignmentOperator) ||
             (KnownTokenHasClass(cur, TokenClass::BinaryOperator) && IsBinaryContext(current)))) {
            return true;
        }
        if (previous->kind == PrintTokenKind::Known &&
            (KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
             (KnownTokenHasClass(prev, TokenClass::BinaryOperator) && IsBinaryContext(*previous)))) {
            return true;
        }
        if (current.kind == PrintTokenKind::Known && KnownTokenHasClass(cur, TokenClass::UnaryOperator) &&
            IsUnaryContext(current)) {
            return false;
        }
        if (previous->kind == PrintTokenKind::Known && KnownTokenHasClass(prev, TokenClass::UnaryOperator) &&
            IsUnaryContext(*previous)) {
            return false;
        }
        if (KnownTokenHasClass(prev, TokenClass::MemberOperator) ||
            KnownTokenHasClass(cur, TokenClass::MemberOperator)) {
            return false;
        }
        if (IsWordLike(*previous) && IsWordLike(current)) {
            return true;
        }
        if (previous->kind == PrintTokenKind::Known &&
            (prev == KnownToken::RightParen || prev == KnownToken::RightBracket || prev == KnownToken::RightBrace ||
             prev == KnownToken::Greater) &&
            IsWordLike(current)) {
            return true;
        }
        return false;
    }

    void PrintOne(
        const PrintToken& token,
        const PrintToken* previous,
        const PrintToken* next,
        const PrintToken* rawNext
    ) {
        if (token.kind == PrintTokenKind::BlankLine) {
            BlankLine();
            return;
        }
        if (IsCommentToken(token.kind)) {
            PrintComment(token);
            return;
        }
        if (token.kind == PrintTokenKind::Preprocessor) {
            PrintPreprocessor(token, next);
            return;
        }
        if (token.kind == PrintTokenKind::Known) {
            PrintKnown(token, previous, next, rawNext);
            return;
        }
        if (ShouldSpaceBetween(previous, token)) {
            Space();
        }
        Write(token.text);
    }

    void PrintComment(const PrintToken& token) {
        if (lineHasText_) {
            Space();
            output_.push_back(' ');
            Write(token.text);
            NewLine();
            return;
        }
        Write(token.text);
        NewLine();
    }

    void PrintPreprocessor(const PrintToken& token, const PrintToken* next) {
        const std::string line = CollapseSourceWhitespace(token.text);
        const bool isInclude = StartsWith(line, "#include");
        const bool isUndef = StartsWith(line, "#undef");
        if (isUndef) {
            BlankLine();
        }
        if (lineHasText_) {
            NewLine();
        }
        output_.append(line);
        lineHasText_ = true;
        atLineStart_ = false;
        NewLine();
        if (StartsWith(line, "#pragma once") || isUndef ||
            (!isInclude && next != nullptr && next->kind != PrintTokenKind::Preprocessor)) {
            BlankLine();
        }
    }

    void PrintKnown(
        const PrintToken& token,
        const PrintToken* previous,
        const PrintToken* next,
        const PrintToken* rawNext
    ) {
        switch (token.known) {
        case KnownToken::LeftParen:
            if (ShouldSpaceBetween(previous, token) && !IsCompilerCallModifierStart(rawNext)) {
                Space();
            }
            Write(KnownTokenText(token.known));
            ++parenDepth_;
            return;
        case KnownToken::RightParen:
            Write(KnownTokenText(token.known));
            if (parenDepth_ > 0) {
                --parenDepth_;
            }
            if (token.inRequiresClause && token.inTemplateDeclaration &&
                !(next != nullptr && next->inRequiresClause)) {
                NewLine();
            }
            return;
        case KnownToken::LeftBracket:
            if (ShouldSpaceBetween(previous, token)) {
                Space();
            }
            Write(KnownTokenText(token.known));
            ++bracketDepth_;
            return;
        case KnownToken::RightBracket:
            Write(KnownTokenText(token.known));
            if (bracketDepth_ > 0) {
                --bracketDepth_;
            }
            return;
        case KnownToken::Greater:
            if (ShouldSpaceBetween(previous, token)) {
                Space();
            }
            Write(KnownTokenText(token.known));
            if (token.parentKind == SyntaxTreeKind::TemplateParameterList &&
                token.inTemplateDeclaration &&
                !(next != nullptr && next->kind == PrintTokenKind::Known &&
                  next->known == KnownToken::KeywordRequires)) {
                NewLine();
            }
            return;
        case KnownToken::LeftBrace:
            PrintLeftBrace(token, previous, rawNext);
            return;
        case KnownToken::RightBrace:
            PrintRightBrace(token, next);
            return;
        case KnownToken::Semicolon:
            Write(";");
            if (ShouldBreakAfterSemicolon() &&
                !(rawNext != nullptr && rawNext->kind == PrintTokenKind::TrailingComment)) {
                NewLine();
            }
            return;
        case KnownToken::Comma:
            Write(",");
            if (InEnumBody() && parenDepth_ == 0 && bracketDepth_ == 0 &&
                !(rawNext != nullptr && rawNext->kind == PrintTokenKind::TrailingComment)) {
                NewLine();
            }
            return;
        case KnownToken::Colon:
            if (ShouldSpaceBetween(previous, token)) {
                Space();
            }
            Write(":");
            if (token.parentKind == SyntaxTreeKind::CaseStatement) {
                ++indentLevel_;
                activeCaseBodySwitchDepths_.push_back(switchDepth_);
                if (rawNext != nullptr && rawNext->kind == PrintTokenKind::Known &&
                    rawNext->known == KnownToken::LeftBrace) {
                    return;
                }
                NewLine();
                return;
            }
            if (previous != nullptr && previous->kind == PrintTokenKind::Known &&
                (previous->known == KnownToken::KeywordDefault ||
                 KnownTokenHasClass(previous->known, TokenClass::AccessKeyword))) {
                NewLine();
            }
            return;
        default:
            if (ShouldSpaceBetween(previous, token)) {
                Space();
            }
            if (IsCaseLabelKeyword(token) && atLineStart_) {
                CloseCaseBodyIndentIfNeeded();
            }
            if (IsAccessKeyword(token)) {
                WriteWithIndentOffset(KnownTokenText(token.known), -1);
                return;
            }
            Write(KnownTokenText(token.known));
            if (token.inRequiresClause && token.inTemplateDeclaration &&
                !(next != nullptr && next->inRequiresClause)) {
                NewLine();
            }
            return;
        }
    }

    void PrintLeftBrace(const PrintToken& token, const PrintToken* previous, const PrintToken* rawNext) {
        const bool isEmptyBracePair = rawNext != nullptr &&
            rawNext->kind == PrintTokenKind::Known &&
            rawNext->known == KnownToken::RightBrace;
        const bool isCaseBlock = previous != nullptr &&
            previous->kind == PrintTokenKind::Known &&
            previous->known == KnownToken::Colon &&
            previous->parentKind == SyntaxTreeKind::CaseStatement;
        const BraceRole role = isCaseBlock ? BraceRole::CaseBlock : RoleForBrace(token);
        if (ShouldSpaceBetween(previous, token)) {
            Space();
        }
        if (isEmptyBracePair) {
            Write("{}");
            ++compactRightBraceSkips_;
            return;
        }
        Write("{");
        braceStack_.push_back(role);
        braceParenDepthStack_.push_back(parenDepth_);
        if (token.parentKind == SyntaxTreeKind::CompoundStatement &&
            token.grandParentKind == SyntaxTreeKind::SwitchStatement) {
            ++switchDepth_;
        }
        if (role == BraceRole::Block || role == BraceRole::Enum) {
            ++indentLevel_;
            NewLine();
        } else if (role == BraceRole::Namespace || role == BraceRole::CaseBlock) {
            NewLine();
            if (role == BraceRole::Namespace) {
                BlankLine();
            }
        }
    }

    void PrintRightBrace(const PrintToken& token, const PrintToken* next) {
        if (compactRightBraceSkips_ > 0) {
            --compactRightBraceSkips_;
            return;
        }
        const BraceRole role = braceStack_.empty() ? RoleForBrace(token) : braceStack_.back();
        if (!braceStack_.empty()) {
            braceStack_.pop_back();
            braceParenDepthStack_.pop_back();
        }
        if (role == BraceRole::Namespace) {
            if (lineHasText_) {
                NewLine();
            }
            BlankLine();
            Write("}");
            NewLine();
            return;
        }
        if (role == BraceRole::CaseBlock) {
            if (lineHasText_) {
                NewLine();
            }
            WriteWithIndentOffset("}", -1);
            NewLine();
            return;
        }
        if (role != BraceRole::Compact) {
            if (lineHasText_) {
                NewLine();
            }
            const bool isSwitchBody = token.parentKind == SyntaxTreeKind::CompoundStatement &&
                token.grandParentKind == SyntaxTreeKind::SwitchStatement;
            if (isSwitchBody) {
                CloseCaseBodyIndentIfNeeded();
            }
            indentLevel_ = std::max(0, indentLevel_ - 1);
            Write("}");
            if (isSwitchBody) {
                switchDepth_ = std::max(0, switchDepth_ - 1);
            }
            if (next != nullptr && next->kind == PrintTokenKind::Known &&
                (next->known == KnownToken::Semicolon || next->known == KnownToken::Comma ||
                 (token.parentKind == SyntaxTreeKind::CompoundStatement &&
                  token.grandParentKind == SyntaxTreeKind::LambdaExpression &&
                  next->known == KnownToken::RightParen) ||
                 (KnownTokenHasClass(next->known, TokenClass::AttachAfterBlockKeyword) &&
                  next->known != KnownToken::KeywordWhile) ||
                 (next->known == KnownToken::KeywordWhile && next->parentKind == SyntaxTreeKind::DoStatement))) {
                return;
            }
            NewLine();
            return;
        }
        Write(KnownTokenText(token.known));
    }
};

}  // namespace

std::string FormatModelText(const FormatterConfig& config, const FormatModel& model, std::string_view sourcePath) {
    (void)sourcePath;
    if (!model.root) {
        return {};
    }
    std::vector<PrintToken> tokens;
    AppendTokens(
        *model.root,
        SyntaxTreeKind::Unknown,
        SyntaxTreeKind::Unknown,
        false,
        false,
        tokens
    );
    return Printer(config).Print(tokens);
}
