#include "tools/impl/format_pretty_printer.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

enum class PrintTokenKind {
    Known,
    Free,
    Comment,
    BlankLine,
    Preprocessor,
};

enum class BraceRole {
    Compact,
    Block,
    Enum,
};

struct PrintToken {
    PrintTokenKind kind = PrintTokenKind::Free;
    KnownToken known = KnownToken::Unknown;
    std::string_view text;
    SyntaxTreeKind treeKind = SyntaxTreeKind::Unknown;
    SyntaxTreeKind parentKind = SyntaxTreeKind::Unknown;
    const SyntaxNode* node = nullptr;
};

bool IsPreprocessorNode(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::PreprocCall ||
        kind == SyntaxTreeKind::PreprocDef ||
        kind == SyntaxTreeKind::PreprocFunctionDef ||
        kind == SyntaxTreeKind::PreprocInclude ||
        kind == SyntaxTreeKind::PreprocUsing;
}

bool IsControlKeyword(KnownToken token) {
    return token == KnownToken::KeywordIf ||
        token == KnownToken::KeywordFor ||
        token == KnownToken::KeywordWhile ||
        token == KnownToken::KeywordSwitch ||
        token == KnownToken::KeywordCatch;
}

bool IsAttachAfterBlockKeyword(KnownToken token) {
    return token == KnownToken::KeywordElse ||
        token == KnownToken::KeywordCatch ||
        token == KnownToken::KeywordFinally ||
        token == KnownToken::KeywordWhile;
}

bool IsAccessKeyword(KnownToken token) {
    return token == KnownToken::KeywordPublic ||
        token == KnownToken::KeywordProtected ||
        token == KnownToken::KeywordPrivate;
}

bool IsKeyword(KnownToken token) {
    return token >= KnownToken::KeywordAlignas && token <= KnownToken::KeywordCoYield;
}

bool IsWordLike(const PrintToken& token) {
    if (token.kind == PrintTokenKind::Free) {
        return true;
    }
    return token.kind == PrintTokenKind::Known && IsKeyword(token.known);
}

bool IsStringLike(const PrintToken& token) {
    return token.treeKind == SyntaxTreeKind::StringLiteral ||
        token.treeKind == SyntaxTreeKind::RawStringLiteral ||
        token.treeKind == SyntaxTreeKind::CharacterLiteral;
}

bool IsMemberOperator(KnownToken token) {
    return token == KnownToken::Dot ||
        token == KnownToken::Arrow ||
        token == KnownToken::DotStar ||
        token == KnownToken::ArrowStar ||
        token == KnownToken::ColonColon;
}

bool IsAssignmentOperator(KnownToken token) {
    return token == KnownToken::Equal ||
        token == KnownToken::PlusEqual ||
        token == KnownToken::MinusEqual ||
        token == KnownToken::StarEqual ||
        token == KnownToken::SlashEqual ||
        token == KnownToken::PercentEqual ||
        token == KnownToken::CaretEqual ||
        token == KnownToken::AmpersandEqual ||
        token == KnownToken::PipeEqual ||
        token == KnownToken::LessLessEqual ||
        token == KnownToken::GreaterGreaterEqual;
}

bool IsBinaryOperator(KnownToken token) {
    switch (token) {
    case KnownToken::Plus:
    case KnownToken::Minus:
    case KnownToken::Star:
    case KnownToken::Slash:
    case KnownToken::Percent:
    case KnownToken::Caret:
    case KnownToken::Ampersand:
    case KnownToken::Pipe:
    case KnownToken::Less:
    case KnownToken::Greater:
    case KnownToken::LessEqual:
    case KnownToken::GreaterEqual:
    case KnownToken::EqualEqual:
    case KnownToken::BangEqual:
    case KnownToken::Spaceship:
    case KnownToken::LessLess:
    case KnownToken::GreaterGreater:
    case KnownToken::AmpersandAmpersand:
    case KnownToken::PipePipe:
        return true;
    default:
        return false;
    }
}

bool IsUnaryOperator(KnownToken token) {
    return token == KnownToken::Bang ||
        token == KnownToken::Tilde ||
        token == KnownToken::Plus ||
        token == KnownToken::Minus ||
        token == KnownToken::Star ||
        token == KnownToken::Ampersand ||
        token == KnownToken::PlusPlus ||
        token == KnownToken::MinusMinus;
}

bool IsPointerDeclaratorToken(const PrintToken& token) {
    return token.parentKind == SyntaxTreeKind::PointerDeclarator &&
        (token.known == KnownToken::Star || token.known == KnownToken::Ampersand);
}

bool IsReferenceDeclaratorToken(const PrintToken& token) {
    return token.parentKind == SyntaxTreeKind::ReferenceDeclarator &&
        (token.known == KnownToken::Ampersand || token.known == KnownToken::AmpersandAmpersand ||
         token.known == KnownToken::Percent);
}

bool IsDeclaratorBindingToken(const PrintToken& token) {
    return IsPointerDeclaratorToken(token) || IsReferenceDeclaratorToken(token);
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

void AppendTokens(
    const SyntaxNode& node,
    SyntaxTreeKind parentKind,
    std::vector<PrintToken>& tokens
) {
    switch (node.kind) {
    case SyntaxNodeKind::BlankLine:
        tokens.push_back({.kind = PrintTokenKind::BlankLine, .node = &node});
        return;
    case SyntaxNodeKind::Comment:
        tokens.push_back({
            .kind = PrintTokenKind::Comment,
            .text = node.text,
            .treeKind = node.treeKind,
            .parentKind = parentKind,
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
            .node = &node,
        });
        return;
    case SyntaxNodeKind::FreeToken:
        tokens.push_back({
            .kind = PrintTokenKind::Free,
            .text = node.text,
            .treeKind = node.treeKind,
            .parentKind = parentKind,
            .node = &node,
        });
        return;
    case SyntaxNodeKind::Tree:
        if (IsPreprocessorNode(node.treeKind)) {
            tokens.push_back({
                .kind = PrintTokenKind::Preprocessor,
                .text = {},
                .treeKind = node.treeKind,
                .parentKind = parentKind,
                .node = &node,
            });
            return;
        }
        for (const std::unique_ptr<SyntaxNode>& child : node.children) {
            AppendTokens(*child, node.treeKind, tokens);
        }
        return;
    }
}

std::string_view NodeSourceText(const FormatModel& model, const SyntaxNode& node) {
    if (!model.sourceText || node.startByte > node.endByte || node.endByte > model.sourceText->size()) {
        return {};
    }
    return std::string_view(*model.sourceText).substr(node.startByte, node.endByte - node.startByte);
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

class Printer {
public:
    Printer(const FormatterConfig& config, const FormatModel& model) :
        indentWidth_(std::max(1, config.indentWidth)),
        model_(model) {}

    std::string Print(const std::vector<PrintToken>& tokens) {
        for (size_t index = 0; index < tokens.size(); ++index) {
            const PrintToken* previous = PreviousToken(tokens, index);
            const PrintToken* next = NextToken(tokens, index);
            PrintOne(tokens[index], previous, next);
        }
        FinishLine();
        if (!output_.empty() && output_.back() != '\n') {
            output_.push_back('\n');
        }
        return output_;
    }

private:
    int indentWidth_ = 4;
    const FormatModel& model_;
    std::string output_;
    int indentLevel_ = 0;
    bool atLineStart_ = true;
    bool lineHasText_ = false;
    int parenDepth_ = 0;
    int bracketDepth_ = 0;
    std::vector<BraceRole> braceStack_;

    static const PrintToken* PreviousToken(const std::vector<PrintToken>& tokens, size_t index) {
        while (index > 0) {
            --index;
            if (tokens[index].kind != PrintTokenKind::BlankLine && tokens[index].kind != PrintTokenKind::Comment) {
                return &tokens[index];
            }
        }
        return nullptr;
    }

    static const PrintToken* NextToken(const std::vector<PrintToken>& tokens, size_t index) {
        for (++index; index < tokens.size(); ++index) {
            if (tokens[index].kind != PrintTokenKind::BlankLine && tokens[index].kind != PrintTokenKind::Comment) {
                return &tokens[index];
            }
        }
        return nullptr;
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

    void Write(std::string_view text) {
        WriteIndentIfNeeded();
        output_.append(text);
        lineHasText_ = lineHasText_ || !text.empty();
    }

    void Space() {
        if (!atLineStart_ && !output_.empty() && output_.back() != ' ' && output_.back() != '\n') {
            output_.push_back(' ');
        }
    }

    bool InEnumBody() const {
        return !braceStack_.empty() && braceStack_.back() == BraceRole::Enum;
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
        if (cur == KnownToken::RightParen || cur == KnownToken::RightBracket || cur == KnownToken::Comma ||
            cur == KnownToken::Semicolon || cur == KnownToken::ColonColon || cur == KnownToken::Dot ||
            cur == KnownToken::Arrow || cur == KnownToken::DotStar || cur == KnownToken::ArrowStar) {
            if (cur == KnownToken::ColonColon &&
                (IsKeyword(prev) || IsAssignmentOperator(prev) || prev == KnownToken::KeywordReturn)) {
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
            return previous->kind == PrintTokenKind::Known && IsControlKeyword(prev);
        }
        if (cur == KnownToken::LeftBracket) {
            return false;
        }
        if (cur == KnownToken::LeftBrace) {
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
        if (prev == KnownToken::Colon) {
            return current.parentKind != SyntaxTreeKind::CaseStatement &&
                !IsAccessKeyword(previous->known);
        }
        if (cur == KnownToken::Less && prev == KnownToken::KeywordTemplate) {
            return true;
        }
        if (cur == KnownToken::Colon) {
            if (previous->known == KnownToken::KeywordDefault || IsAccessKeyword(previous->known)) {
                return false;
            }
            return current.parentKind != SyntaxTreeKind::CaseStatement;
        }
        if (IsDeclaratorBindingToken(current)) {
            return false;
        }
        if (IsDeclaratorBindingToken(*previous)) {
            return !IsDeclaratorBindingToken(current);
        }
        if (prev == KnownToken::KeywordReturn && current.kind == PrintTokenKind::Known && IsUnaryOperator(cur)) {
            return true;
        }
        if (current.kind == PrintTokenKind::Known &&
            (IsAssignmentOperator(cur) || (IsBinaryOperator(cur) && IsBinaryContext(current)))) {
            return true;
        }
        if (previous->kind == PrintTokenKind::Known &&
            (IsAssignmentOperator(prev) || (IsBinaryOperator(prev) && IsBinaryContext(*previous)))) {
            return true;
        }
        if (current.kind == PrintTokenKind::Known && IsUnaryOperator(cur) && IsUnaryContext(current)) {
            return false;
        }
        if (previous->kind == PrintTokenKind::Known && IsUnaryOperator(prev) && IsUnaryContext(*previous)) {
            return false;
        }
        if (IsMemberOperator(prev) || IsMemberOperator(cur)) {
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

    void PrintOne(const PrintToken& token, const PrintToken* previous, const PrintToken* next) {
        if (token.kind == PrintTokenKind::BlankLine) {
            BlankLine();
            return;
        }
        if (token.kind == PrintTokenKind::Comment) {
            PrintComment(token);
            return;
        }
        if (token.kind == PrintTokenKind::Preprocessor) {
            PrintPreprocessor(token);
            return;
        }
        if (token.kind == PrintTokenKind::Known) {
            PrintKnown(token, previous, next);
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

    void PrintPreprocessor(const PrintToken& token) {
        if (lineHasText_) {
            NewLine();
        }
        const std::string line = CollapseSourceWhitespace(NodeSourceText(model_, *token.node));
        output_.append(line);
        lineHasText_ = true;
        atLineStart_ = false;
        NewLine();
        if (StartsWith(line, "#pragma once")) {
            BlankLine();
        }
    }

    void PrintKnown(const PrintToken& token, const PrintToken* previous, const PrintToken* next) {
        switch (token.known) {
        case KnownToken::LeftParen:
            if (ShouldSpaceBetween(previous, token)) {
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
        case KnownToken::LeftBrace:
            PrintLeftBrace(token, previous);
            return;
        case KnownToken::RightBrace:
            PrintRightBrace(token, next);
            return;
        case KnownToken::Semicolon:
            Write(";");
            if (parenDepth_ == 0) {
                NewLine();
            }
            return;
        case KnownToken::Comma:
            Write(",");
            if (InEnumBody() && parenDepth_ == 0 && bracketDepth_ == 0) {
                NewLine();
            }
            return;
        case KnownToken::Colon:
            if (ShouldSpaceBetween(previous, token)) {
                Space();
            }
            Write(":");
            if (token.parentKind == SyntaxTreeKind::CaseStatement ||
                (previous != nullptr && previous->kind == PrintTokenKind::Known &&
                 (previous->known == KnownToken::KeywordDefault || IsAccessKeyword(previous->known)))) {
                NewLine();
            }
            return;
        default:
            if (ShouldSpaceBetween(previous, token)) {
                Space();
            }
            Write(KnownTokenText(token.known));
            return;
        }
    }

    void PrintLeftBrace(const PrintToken& token, const PrintToken* previous) {
        const BraceRole role = RoleForBraceParent(token.parentKind);
        if (ShouldSpaceBetween(previous, token)) {
            Space();
        }
        Write("{");
        braceStack_.push_back(role);
        if (role != BraceRole::Compact) {
            ++indentLevel_;
            NewLine();
        }
    }

    void PrintRightBrace(const PrintToken& token, const PrintToken* next) {
        const BraceRole role = braceStack_.empty() ? RoleForBraceParent(token.parentKind) : braceStack_.back();
        if (!braceStack_.empty()) {
            braceStack_.pop_back();
        }
        if (role != BraceRole::Compact) {
            if (lineHasText_) {
                NewLine();
            }
            indentLevel_ = std::max(0, indentLevel_ - 1);
            Write("}");
            if (next != nullptr && next->kind == PrintTokenKind::Known &&
                (next->known == KnownToken::Semicolon || next->known == KnownToken::Comma ||
                 IsAttachAfterBlockKeyword(next->known))) {
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
    AppendTokens(*model.root, SyntaxTreeKind::Unknown, tokens);
    return Printer(config, model).Print(tokens);
}
