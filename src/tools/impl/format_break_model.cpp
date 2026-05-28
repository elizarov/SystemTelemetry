#include "tools/impl/format_break_model.h"

#include <algorithm>

namespace {

bool IsLeadingNameToken(const FormatBreakToken& token) {
    return token.token.kind == PrintTokenKind::Free ||
        (token.token.kind == PrintTokenKind::Known && token.token.syntaxKind == SyntaxNodeKind::ColonColon);
}

bool ConsumeLeadingName(const FormatBreakNode& node, std::string_view candidate, size_t& position) {
    switch (node.kind) {
        case FormatBreakNodeKind::Token: {
            if (!IsLeadingNameToken(node.token)) {
                return true;
            }
            const std::string_view text = FormatTokenText(node.token.token);
            if (position + text.size() > candidate.size() || candidate.substr(position, text.size()) != text) {
                return false;
            }
            position += text.size();
            return true;
        }
        case FormatBreakNodeKind::Sequence:
            for (const std::unique_ptr<FormatBreakNode>& child : node.children) {
                if (child->kind == FormatBreakNodeKind::Delimited) {
                    return true;
                }
                if (!ConsumeLeadingName(*child, candidate, position)) {
                    return false;
                }
            }
            return true;
        default:
            return true;
    }
}

}  // namespace

bool FormatBreakLeadingNameMatches(const FormatBreakNode& node, std::string_view candidate) {
    size_t position = 0;
    return ConsumeLeadingName(node, candidate, position) && position == candidate.size();
}

bool IsFormatBreakStreamConfigurationOperand(
    const FormatBreakNode& node,
    const std::vector<std::string>& configurationMethods
) {
    return std::any_of(
        configurationMethods.begin(),
        configurationMethods.end(),
        [&node](const std::string& method) { return FormatBreakLeadingNameMatches(node, method); }
    );
}
