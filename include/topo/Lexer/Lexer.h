#ifndef TOPO_LEXER_LEXER_H
#define TOPO_LEXER_LEXER_H

#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Token.h"
#include "topo/Transform/TransformRecord.h"
#include <string>
#include <vector>

namespace topo {

class Lexer {
public:
    Lexer(const std::string& source, const std::string& filename, DiagnosticEngine& diag);

    Token nextToken();

    void setPreserveComments(bool preserve) { preserveComments_ = preserve; }
    const std::vector<CommentToken>& comments() const { return comments_; }

    void setLenientMode(bool lenient) { lenientMode_ = lenient; }
    bool isLenientMode() const { return lenientMode_; }
    const std::vector<transform::TransformRecord>& transformRecords() const { return transformRecords_; }

    void setTokenBuffer(std::vector<Token> tokens);

private:
    char peek() const;
    char peekNext() const;
    char advance();
    bool isAtEnd() const;

    void skipWhitespace();
    void skipLineComment();
    bool skipBlockComment();

    Token makeToken(TokenKind kind, const std::string& text) const;
    Token scanIdentifierOrKeyword();
    Token scanIntegerLiteral();
    Token scanStringLiteral();

    SourceLocation currentLocation() const;

    const std::string& source_;
    std::string filename_;
    DiagnosticEngine& diag_;

    size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;

    // Saved at the start of each token
    int tokenLine_ = 1;
    int tokenColumn_ = 1;

    // Comment preservation (off by default)
    bool preserveComments_ = false;
    std::vector<CommentToken> comments_;

    // Lenient mode: maps variant keywords to canonical TokenKinds
    bool lenientMode_ = false;
    std::vector<transform::TransformRecord> transformRecords_;

    // Token buffer for TokenStreamRewriter integration
    std::vector<Token> tokenBuffer_;
    size_t bufferCursor_ = 0;
};

} // namespace topo

#endif // TOPO_LEXER_LEXER_H
