#include "tools/format_model_dump.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "tools/impl/format_model.h"
#include "tools/impl/format_model_parse.h"
#include "tools/impl/tools_common.h"

namespace {

constexpr size_t kQuotedTextLimit = 60;
constexpr int kYamlIndentSpaces = 2;

void WriteIndent(int indent) {
    for (int index = 0; index < indent * kYamlIndentSpaces; ++index) {
        std::fputc(' ', stdout);
    }
}

void WriteString(std::string_view text) {
    if (!text.empty()) {
        std::fwrite(text.data(), 1, text.size(), stdout);
    }
}

void WriteQuotedText(std::string_view text) {
    std::fputc('"', stdout);
    for (const unsigned char ch : text) {
        switch (ch) {
            case'\\':
                std::fputs("\\\\", stdout);
                break;
            case'"':
                std::fputs("\\\"", stdout);
                break;
            case'\n':
                std::fputs("\\n", stdout);
                break;
            case'\r':
                std::fputs("\\r", stdout);
                break;
            case'\t':
                std::fputs("\\t", stdout);
                break;
            default:
                if (ch < 0x20) {
                    std::fprintf(stdout, "\\x%02X", static_cast<unsigned int>(ch));
                } else {
                    std::fputc(static_cast<char>(ch), stdout);
                }
                break;
        }
    }
    std::fputc('"', stdout);
}

void WriteTextValue(std::string_view text) {
    if (text.size() <= kQuotedTextLimit) {
        WriteQuotedText(text);
        return;
    }
    std::fprintf(stdout, "%zu", text.size());
}

void WriteNode(const SyntaxNode& node, int indent, bool listItem) {
    const int fieldIndent = listItem ? indent + 1 : indent;
    WriteIndent(indent);
    if (listItem) {
        std::fputs("- kind: ", stdout);
    } else {
        std::fputs("kind: ", stdout);
    }
    WriteString(SyntaxNodeKindName(node.kind));
    std::fputc('\n', stdout);

    if (!node.text.empty()) {
        WriteIndent(fieldIndent);
        std::fputs("text: ", stdout);
        WriteTextValue(node.text);
        std::fputc('\n', stdout);
    }

    if (node.children.empty()) {
        return;
    }
    WriteIndent(fieldIndent);
    std::fputs("children:\n", stdout);
    for (const SyntaxNode* child : node.children) {
        if (child != nullptr) {
            WriteNode(*child, fieldIndent + 1, true);
        }
    }
}

void PrintUsage() {
    std::fprintf(stderr, "Usage: CaseDashTools.exe format_model_dump <source-file>\n");
}

}  // namespace

int RunFormatModelDump(int argc, char** argv) {
    if (argc != 1) {
        PrintUsage();
        return 2;
    }

    const std::string path = tools::AbsolutePath(argv[0]);
    const std::optional<std::string> text = tools::ReadFileText(path);
    if (!text.has_value()) {
        std::fprintf(stderr, "format_model_dump: cannot read file: %s\n", path.c_str());
        return 1;
    }

    FormatModel model = ParseFormatModel(*text);
    if (!model.parse.ok) {
        const std::string error =
            model.parse.error.empty() ? std::string("tree-sitter parser setup failed") : model.parse.error;
        std::fprintf(stderr, "format_model_dump: parse failed: %s\n", error.c_str());
        return 1;
    }
    if (model.root == nullptr) {
        std::fprintf(stderr, "format_model_dump: parse produced no root node\n");
        return 1;
    }

    WriteNode(*model.root, 0, false);
    return 0;
}
