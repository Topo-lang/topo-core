#ifndef TOPO_LEXER_TOKEN_H
#define TOPO_LEXER_TOKEN_H

#include "topo/Basic/SourceLocation.h"
#include "topo/Basic/TokenKinds.h"
#include <ostream>
#include <string>

namespace topo {

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string text;
    SourceLocation location;

    bool is(TokenKind k) const { return kind == k; }
    bool isNot(TokenKind k) const { return kind != k; }

    bool isKeyword() const { return kind >= TokenKind::KW_namespace && kind <= TokenKind::KW_operator; }
};

inline std::ostream& operator<<(std::ostream& os, const Token& tok) {
    os << "[" << tok.location.line << ":" << tok.location.column << "] " << tokenKindName(tok.kind) << " '" << tok.text
       << "'";
    return os;
}

struct CommentToken {
    std::string text; // Full text including // or /* */ delimiters
    SourceLocation location;
    bool isBlock; // true = /* */, false = //
};

} // namespace topo

#endif // TOPO_LEXER_TOKEN_H
