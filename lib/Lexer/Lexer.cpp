#include "topo/Lexer/Lexer.h"
#include <unordered_map>

namespace topo {

static const std::unordered_map<std::string, TokenKind> keywordAliases = {
    {"struct", TokenKind::KW_type},
    // `record` is NOT a `type`-declaration alias: it is now a first-class
    // stdlib bridging type keyword (record<...>), so it resolves via the
    // strict keyword table above. Keeping it here would be dead (strict
    // keywords win) and semantically wrong.
    {"trait", TokenKind::KW_constraint},
    {"interface", TokenKind::KW_constraint},
    {"protocol", TokenKind::KW_constraint},
    {"impl", TokenKind::KW_adapt},
    {"implement", TokenKind::KW_adapt},
    {"module", TokenKind::KW_namespace},
    {"package", TokenKind::KW_namespace},
    {"mod", TokenKind::KW_namespace},
    {"pub", TokenKind::KW_public},
    {"export", TokenKind::KW_public},
    {"unique", TokenKind::KW_owned},
    {"own", TokenKind::KW_owned},
    {"extern", TokenKind::KW_external},
    {"def", TokenKind::KW_fn},
    {"func", TokenKind::KW_fn},
    {"function", TokenKind::KW_fn},
};

// Reverse lookup: TokenKind -> canonical keyword string
static const char* canonicalKeywordText(TokenKind kind) {
    switch (kind) {
    case TokenKind::KW_type: return "type";
    case TokenKind::KW_constraint: return "constraint";
    case TokenKind::KW_adapt: return "adapt";
    case TokenKind::KW_namespace: return "namespace";
    case TokenKind::KW_public: return "public";
    case TokenKind::KW_owned: return "owned";
    case TokenKind::KW_fn: return "fn";
    case TokenKind::KW_external: return "external";
    default: return "";
    }
}

static const std::unordered_map<std::string, TokenKind> keywords = {
    {"namespace", TokenKind::KW_namespace},
    {"using", TokenKind::KW_using},
    {"import", TokenKind::KW_import},
    {"public", TokenKind::KW_public},
    {"protected", TokenKind::KW_protected},
    {"private", TokenKind::KW_private},
    {"internal", TokenKind::KW_internal},
    {"ignore", TokenKind::KW_ignore},
    {"fn", TokenKind::KW_fn},
    {"stage", TokenKind::KW_stage},
    {"const", TokenKind::KW_const},
    {"void", TokenKind::KW_void},
    {"true", TokenKind::KW_true},
    {"false", TokenKind::KW_false},
    {"return", TokenKind::KW_return},
    {"class", TokenKind::KW_class},
    {"static", TokenKind::KW_static},
    {"explicit", TokenKind::KW_explicit},
    {"virtual", TokenKind::KW_virtual},
    {"template", TokenKind::KW_template},
    {"typename", TokenKind::KW_typename},
    {"constraint", TokenKind::KW_constraint},
    {"requires", TokenKind::KW_requires},
    {"adapt", TokenKind::KW_adapt},
    {"instantiate", TokenKind::KW_instantiate},
    {"for", TokenKind::KW_for},
    {"comptime", TokenKind::KW_comptime},
    {"typefn", TokenKind::KW_typefn},
    {"match", TokenKind::KW_match},
    {"if", TokenKind::KW_if},
    {"else", TokenKind::KW_else},
    {"owned", TokenKind::KW_owned},
    {"shared", TokenKind::KW_shared},
    {"weak", TokenKind::KW_weak},
    {"operator", TokenKind::KW_operator},
    {"priority", TokenKind::KW_priority},
    {"type", TokenKind::KW_type},
    {"external", TokenKind::KW_external},
    {"with", TokenKind::KW_with},
    {"handler", TokenKind::KW_handler},
    {"flow", TokenKind::KW_flow},
    // Stdlib bridging type keywords (lowercase by convention).
    {"bool", TokenKind::KW_bool},
    {"i64", TokenKind::KW_i64},
    {"f64", TokenKind::KW_f64},
    {"string", TokenKind::KW_string},
    {"optional", TokenKind::KW_optional},
    {"slice", TokenKind::KW_slice},
    {"bytes", TokenKind::KW_bytes},
    // Width-extension scalar keywords.
    {"u8", TokenKind::KW_u8},
    {"i32", TokenKind::KW_i32},
    {"u32", TokenKind::KW_u32},
    {"u64", TokenKind::KW_u64},
    {"f32", TokenKind::KW_f32},
    {"i8", TokenKind::KW_i8},
    {"i16", TokenKind::KW_i16},
    {"u16", TokenKind::KW_u16},
    // Semantic scalar (i64-isomorphic ABI, domain-tagged).
    {"time_ns", TokenKind::KW_time_ns},
    // 16-byte value scalar (raw RFC 4122 bytes).
    {"uuid", TokenKind::KW_uuid},
    // 16-byte value scalar (raw IEEE 754-2008 decimal128 bytes).
    {"decimal128", TokenKind::KW_decimal128},
    // Composite stdlib type keywords.
    {"record", TokenKind::KW_record},
    {"array", TokenKind::KW_array},
    {"union", TokenKind::KW_union},
};

Lexer::Lexer(const std::string& source, const std::string& filename, DiagnosticEngine& diag)
    : source_(source), filename_(filename), diag_(diag) {}

char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source_[pos_];
}

char Lexer::peekNext() const {
    if (pos_ + 1 >= source_.size()) return '\0';
    return source_[pos_ + 1];
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::isAtEnd() const {
    return pos_ >= source_.size();
}

SourceLocation Lexer::currentLocation() const {
    return {filename_, tokenLine_, tokenColumn_};
}

Token Lexer::makeToken(TokenKind kind, const std::string& text) const {
    return {kind, text, {filename_, tokenLine_, tokenColumn_, line_, column_}};
}

void Lexer::skipWhitespace() {
    while (!isAtEnd()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peekNext() == '/') {
            skipLineComment();
        } else if (c == '/' && peekNext() == '*') {
            if (!skipBlockComment()) return;
        } else {
            break;
        }
    }
}

void Lexer::skipLineComment() {
    int startLine = line_;
    int startCol = column_;
    size_t startPos = pos_;
    // Skip "//"
    advance();
    advance();
    while (!isAtEnd() && peek() != '\n') {
        advance();
    }
    if (preserveComments_) {
        comments_.push_back({source_.substr(startPos, pos_ - startPos), {filename_, startLine, startCol}, false});
    }
}

bool Lexer::skipBlockComment() {
    int startLine = line_;
    int startCol = column_;
    size_t startPos = pos_;
    // Skip "/*"
    advance();
    advance();
    while (!isAtEnd()) {
        if (peek() == '*' && peekNext() == '/') {
            advance(); // *
            advance(); // /
            if (preserveComments_) {
                comments_.push_back(
                    {source_.substr(startPos, pos_ - startPos), {filename_, startLine, startCol}, true});
            }
            return true;
        }
        advance();
    }
    diag_.error({filename_, startLine, startCol}, "unterminated block comment");
    return false;
}

Token Lexer::scanIdentifierOrKeyword() {
    size_t start = pos_;
    while (!isAtEnd() && (std::isalnum(peek()) || peek() == '_')) {
        advance();
    }
    std::string text = source_.substr(start, pos_ - start);
    auto it = keywords.find(text);
    if (it != keywords.end()) {
        return makeToken(it->second, text);
    }
    if (lenientMode_) {
        auto ait = keywordAliases.find(text);
        if (ait != keywordAliases.end()) {
            transformRecords_.push_back({{filename_, tokenLine_, tokenColumn_},
                                         text,
                                         canonicalKeywordText(ait->second),
                                         transform::TransformRecord::KeywordAlias});
            return makeToken(ait->second, text);
        }
    }
    return makeToken(TokenKind::Identifier, text);
}

Token Lexer::scanIntegerLiteral() {
    size_t start = pos_;
    while (!isAtEnd() && std::isdigit(peek())) {
        advance();
    }
    return makeToken(TokenKind::IntegerLiteral, source_.substr(start, pos_ - start));
}

Token Lexer::scanStringLiteral() {
    advance(); // opening "
    size_t start = pos_;
    while (!isAtEnd() && peek() != '"') {
        if (peek() == '\n') {
            diag_.error(currentLocation(), "unterminated string literal");
            return makeToken(TokenKind::Error, "");
        }
        advance();
    }
    if (isAtEnd()) {
        diag_.error(currentLocation(), "unterminated string literal");
        return makeToken(TokenKind::Error, "");
    }
    std::string text = source_.substr(start, pos_ - start);
    advance(); // closing "
    return makeToken(TokenKind::StringLiteral, text);
}

void Lexer::setTokenBuffer(std::vector<Token> tokens) {
    tokenBuffer_ = std::move(tokens);
    bufferCursor_ = 0;
}

Token Lexer::nextToken() {
    if (!tokenBuffer_.empty()) {
        if (bufferCursor_ < tokenBuffer_.size()) return tokenBuffer_[bufferCursor_++];
        return makeToken(TokenKind::Eof, "");
    }

    skipWhitespace();

    tokenLine_ = line_;
    tokenColumn_ = column_;

    if (isAtEnd()) {
        return makeToken(TokenKind::Eof, "");
    }

    char c = peek();

    // Identifiers and keywords
    if (std::isalpha(c) || c == '_') {
        return scanIdentifierOrKeyword();
    }

    // Integer literals
    if (std::isdigit(c)) {
        return scanIntegerLiteral();
    }

    // String literals
    if (c == '"') {
        return scanStringLiteral();
    }

    // Multi-character operators (longest match first)
    if (c == ':' && peekNext() == ':') {
        advance();
        advance();
        return makeToken(TokenKind::ColonColon, "::");
    }
    if (c == '-' && peekNext() == '>') {
        advance();
        advance();
        return makeToken(TokenKind::Arrow, "->");
    }
    if (c == '=' && peekNext() == '=') {
        advance();
        advance();
        return makeToken(TokenKind::EqEq, "==");
    }
    if (c == '=' && peekNext() == '>') {
        advance();
        advance();
        return makeToken(TokenKind::FatArrow, "=>");
    }
    if (c == '!' && peekNext() == '=') {
        advance();
        advance();
        return makeToken(TokenKind::NotEq, "!=");
    }
    if (c == '<' && peekNext() == '<') {
        advance();
        advance();
        return makeToken(TokenKind::ShiftLeft, "<<");
    }
    if (c == '<' && peekNext() == '=') {
        advance();
        advance();
        return makeToken(TokenKind::LessEq, "<=");
    }
    if (c == '>' && peekNext() == '>') {
        advance();
        advance();
        return makeToken(TokenKind::ShiftRight, ">>");
    }
    if (c == '>' && peekNext() == '=') {
        advance();
        advance();
        return makeToken(TokenKind::GreaterEq, ">=");
    }
    if (c == '&' && peekNext() == '&') {
        advance();
        advance();
        return makeToken(TokenKind::AmpAmp, "&&");
    }
    if (c == '|' && peekNext() == '|') {
        advance();
        advance();
        return makeToken(TokenKind::PipePipe, "||");
    }

    // DotDot (..) or Ellipsis (...)
    if (c == '.' && peekNext() == '.') {
        advance(); // first .
        advance(); // second .
        if (peek() == '.') {
            advance(); // third .
            return makeToken(TokenKind::Ellipsis, "...");
        }
        return makeToken(TokenKind::DotDot, "..");
    }

    // Single-character tokens
    advance();
    switch (c) {
    case '{': return makeToken(TokenKind::LCurly, "{");
    case '}': return makeToken(TokenKind::RCurly, "}");
    case '(': return makeToken(TokenKind::LParen, "(");
    case ')': return makeToken(TokenKind::RParen, ")");
    case '<': return makeToken(TokenKind::LAngle, "<");
    case '>': return makeToken(TokenKind::RAngle, ">");
    case ',': return makeToken(TokenKind::Comma, ",");
    case ';': return makeToken(TokenKind::Semicolon, ";");
    case ':': return makeToken(TokenKind::Colon, ":");
    case '=': return makeToken(TokenKind::Assign, "=");
    case '.': return makeToken(TokenKind::Dot, ".");
    case '&': return makeToken(TokenKind::Amp, "&");
    case '*': return makeToken(TokenKind::Star, "*");
    case '+': return makeToken(TokenKind::Plus, "+");
    case '-': return makeToken(TokenKind::Minus, "-");
    case '/': return makeToken(TokenKind::Slash, "/");
    case '!': return makeToken(TokenKind::Bang, "!");
    case '~': return makeToken(TokenKind::Tilde, "~");
    case '%': return makeToken(TokenKind::Percent, "%");
    case '|': return makeToken(TokenKind::Pipe, "|");
    case '^': return makeToken(TokenKind::Caret, "^");
    case '@':
        if (lenientMode_) return makeToken(TokenKind::At, "@");
        break;
    case '#':
        if (lenientMode_) return makeToken(TokenKind::Hash, "#");
        break;
    // `[` and `]` were originally lenient-mode-only (used by topo-fmt). They
    // are now first-class tokens so `.topo` debug slice expressions
    // (`field[0..10]`) parse in strict mode. Other constructs still reject
    // brackets via their own parsing rules.
    case '[':
        return makeToken(TokenKind::LBracket, "[");
    case ']':
        return makeToken(TokenKind::RBracket, "]");
    default: break;
    }
    diag_.error(currentLocation(), std::string("unexpected character '") + c + "'");
    return makeToken(TokenKind::Error, std::string(1, c));
}

} // namespace topo
