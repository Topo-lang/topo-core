#ifndef TOPO_TRANSFORM_TOKENSTREAMREWRITER_H
#define TOPO_TRANSFORM_TOKENSTREAMREWRITER_H

#include "topo/Lexer/Lexer.h"
#include "topo/Transform/TransformRecord.h"
#include <vector>

namespace topo::transform {

class TokenStreamRewriter {
public:
    explicit TokenStreamRewriter(Lexer& lexer);

    /// Drain lexer, apply rewrites if lenient, return rewritten token stream.
    std::vector<Token> rewrite();

    const std::vector<TransformRecord>& transformRecords() const { return records_; }

private:
    void rewriteStageParens();
    void rewriteAtVisibility();
    void rewriteAtModifier();
    void rewriteKeywordAliasText();

    bool kindAt(size_t i, TokenKind kind) const;
    void replaceRange(size_t from, size_t to, const std::vector<Token>& replacement);
    SourceLocation spanLoc(const Token& first, const Token& last) const;

    std::vector<Token> tokens_;
    std::vector<TransformRecord> records_;
    bool lenient_;
};

} // namespace topo::transform

#endif // TOPO_TRANSFORM_TOKENSTREAMREWRITER_H
