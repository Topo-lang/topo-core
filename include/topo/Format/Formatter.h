#ifndef TOPO_FORMAT_FORMATTER_H
#define TOPO_FORMAT_FORMATTER_H

#include "topo/AST/ASTNode.h"
#include "topo/Lexer/Token.h"
#include <string>
#include <vector>

namespace topo::format {

class Formatter {
public:
    std::string format(const TopoFile& ast, const std::string& source, const std::vector<CommentToken>& comments);

    std::string formatRange(const TopoFile& ast,
                            const std::string& source,
                            const std::vector<CommentToken>& comments,
                            int startLine,
                            int endLine);

private:
    // Output helpers
    void emit(const std::string& text);
    void emitLine(const std::string& text = "");
    void emitIndent(int level);
    void emitBlankLine();

    // Comment attachment
    void emitCommentsBefore(int line, int indentLevel);
    void emitEndOfLineComment(int line);

    // AST formatting
    void formatFile(const TopoFile& ast);
    void formatImport(const Import& imp, int indent);
    void formatStdImport(const DataDecl& imp, int indent);
    void formatTypeAlias(const DataDecl& ta, int indent);
    void formatNamespace(const NamespaceDecl& ns, int indent);
    void formatVisibilitySection(const VisibilitySection& vis, int nsIndent);
    void formatFnDecl(const FnDecl& fn, int indent);
    void formatFnLogicBlock(const FnLogicBlock& fb, int indent);
    void formatOperation(const OperationDecl& op, int indent);
    void formatPipelineEdge(const PipelineEdge& edge, int indent);
    void formatTypeDecl(const TypeDecl& cls, int indent);
    void formatTypeMember(const ASTNode& member, int indent);
    void formatComptimeIf(const IfDecl& node, int indent);
    void formatComptimeIfMember(const ASTNode& member, int indent);

    // Re-emit an unhandled top-level node verbatim from its source span so
    // formatting never deletes content it does not know how to pretty-print
    // (e.g. a top-level `debug T { ... }` declaration). Returns false when
    // the span could not be recovered (no source / out-of-range line), in
    // which case the caller must avoid claiming the node was emitted.
    bool emitVerbatimNode(const ASTNode& node, int indent);

    // Parameter formatting
    std::string renderParams(const std::vector<Parameter>& params);
    std::string renderReturnParams(const std::vector<ReturnParam>& params);

    // Source text extraction for expressions
    std::string extractSourceLine(int line) const;
    std::string extractParenContent(int fromLine) const;
    // Extract a brace-balanced block span starting at `fromLine` (the line of
    // the node's leading token). Returns the verbatim source lines from the
    // opening line through the line containing the matching `}`. When no `{`
    // is seen on/after `fromLine`, returns just that single line (covers a
    // hypothetical brace-less top-level form). Empty when source is absent or
    // the line is out of range.
    std::string extractBlockSpan(int fromLine) const;

    // Import sorting
    std::vector<const ASTNode*> sortedImports(const std::vector<ASTNodePtr>& decls) const;

    // State
    std::string output_;
    const std::string* source_ = nullptr;
    const std::vector<CommentToken>* comments_ = nullptr;
    size_t nextCommentIdx_ = 0;
    bool lastWasBlank_ = false;
    int rangeStart_ = -1;
    int rangeEnd_ = -1;
};

} // namespace topo::format

#endif // TOPO_FORMAT_FORMATTER_H
