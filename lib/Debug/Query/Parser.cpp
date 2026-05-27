#include "topo/Debug/Query/Parser.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace topo::debug_query {

namespace {

class QueryLexer {
public:
    explicit QueryLexer(const std::string& s) : src_(s) {}

    void skipSpaces() {
        while (pos_ < src_.size() &&
               (src_[pos_] == ' ' || src_[pos_] == '\t' || src_[pos_] == '\n' ||
                src_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool peek(char c) {
        skipSpaces();
        return pos_ < src_.size() && src_[pos_] == c;
    }

    bool peek2(const char* s) {
        skipSpaces();
        size_t n = 0;
        while (s[n]) ++n;
        if (pos_ + n > src_.size()) return false;
        for (size_t i = 0; i < n; ++i) {
            if (src_[pos_ + i] != s[i]) return false;
        }
        return true;
    }

    bool consume(char c) {
        skipSpaces();
        if (pos_ < src_.size() && src_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool consumeRange() {
        skipSpaces();
        if (pos_ + 1 < src_.size() && src_[pos_] == '.' && src_[pos_ + 1] == '.') {
            pos_ += 2;
            return true;
        }
        return false;
    }

    size_t pos() const { return pos_; }
    bool atEnd() const { return pos_ >= src_.size(); }
    char current() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
    size_t srcSize() const { return src_.size(); }
    char charAt(size_t p) const { return p < src_.size() ? src_[p] : '\0'; }
    void advance(size_t n) { pos_ += n; }

    // Try to consume an identifier. Returns empty string if not at one.
    std::string tryIdent(size_t* startPos = nullptr) {
        skipSpaces();
        if (pos_ >= src_.size()) return {};
        char c = src_[pos_];
        if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) return {};
        size_t start = pos_;
        while (pos_ < src_.size()) {
            char cc = src_[pos_];
            if (std::isalnum(static_cast<unsigned char>(cc)) || cc == '_') {
                ++pos_;
            } else {
                break;
            }
        }
        if (startPos) *startPos = start;
        return src_.substr(start, pos_ - start);
    }

    // Try to consume a number literal. Returns false if not at one.
    // Sets isFloat = true when the literal has '.' or 'e'/'E'.
    bool tryNumber(double& dval, int64_t& ival, bool& isFloat, size_t& startPos) {
        skipSpaces();
        if (pos_ >= src_.size()) return false;
        char c = src_[pos_];
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+')) return false;
        // Disambiguate range '..' from a number — only treat as number if first
        // char is digit or sign followed by digit.
        if ((c == '+' || c == '-') &&
            !(pos_ + 1 < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_ + 1])))) {
            return false;
        }
        startPos = pos_;
        size_t start = pos_;
        if (c == '+' || c == '-') ++pos_;
        isFloat = false;
        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        // Look ahead for '.' but reject if followed by another '.' (the range op).
        if (pos_ < src_.size() && src_[pos_] == '.' &&
            !(pos_ + 1 < src_.size() && src_[pos_ + 1] == '.')) {
            isFloat = true;
            ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            isFloat = true;
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        }
        std::string lit = src_.substr(start, pos_ - start);
        if (isFloat) {
            dval = std::strtod(lit.c_str(), nullptr);
        } else {
            ival = std::strtoll(lit.c_str(), nullptr, 10);
        }
        return true;
    }

    bool tryString(std::string& out, size_t& startPos) {
        skipSpaces();
        if (pos_ >= src_.size() || src_[pos_] != '"') return false;
        startPos = pos_;
        ++pos_;
        out.clear();
        while (pos_ < src_.size() && src_[pos_] != '"') {
            if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
                ++pos_;
            }
            out.push_back(src_[pos_]);
            ++pos_;
        }
        if (pos_ < src_.size() && src_[pos_] == '"') ++pos_;
        return true;
    }

private:
    const std::string& src_;
    size_t pos_ = 0;
};

class Parser {
public:
    Parser(QueryLexer& lex, std::string& err) : lex_(lex), error_(err) {}

    // Top-level expression: precedence climbing
    //   parseExpr  → parseAddSub
    //   parseAddSub → parseMulDiv ( ('+'|'-') parseMulDiv )*
    //   parseMulDiv → parseUnary  ( ('*'|'/') parseUnary  )*
    //   parseUnary  → '-' parseUnary | parsePostfix(parsePrimary)
    ExprPtr parseExpr() { return parseAddSub(); }

private:
    ExprPtr parseAddSub() {
        ExprPtr left = parseMulDiv();
        if (!left) return nullptr;
        while (true) {
            BinaryOpKind op;
            size_t opPos = 0;
            if (peekBinaryOp('+', BinaryOpKind::Add, op, opPos)) {}
            else if (peekBinaryOp('-', BinaryOpKind::Sub, op, opPos)) {}
            else break;
            ExprPtr right = parseMulDiv();
            if (!right) return nullptr;
            left = makeBinary(op, std::move(left), std::move(right), opPos);
        }
        return left;
    }

    ExprPtr parseMulDiv() {
        ExprPtr left = parseUnary();
        if (!left) return nullptr;
        while (true) {
            BinaryOpKind op;
            size_t opPos = 0;
            if (peekBinaryOp('*', BinaryOpKind::Mul, op, opPos)) {}
            else if (peekBinaryOp('/', BinaryOpKind::Div, op, opPos)) {}
            else break;
            ExprPtr right = parseUnary();
            if (!right) return nullptr;
            left = makeBinary(op, std::move(left), std::move(right), opPos);
        }
        return left;
    }

    ExprPtr parseUnary() {
        // Unary `-` binds tighter than `* /` so `-x * 3` parses as `(-x) * 3`.
        // `tryNumber` already accepts `-3` as IntLit(-3) when the `-` is at
        // the start of a numeric token. We only fire unary minus here when
        // the lookahead is `-` *not* followed by a digit — keeping single-
        // token negative literals untouched (so e.g. `-3` stays IntLit(-3)
        // for stable golden output).
        lex_.skipSpaces();
        size_t start = lex_.pos();
        if (lex_.current() == '-') {
            char next = (start + 1 < lex_.srcSize()) ? lex_.charAt(start + 1) : '\0';
            if (!std::isdigit(static_cast<unsigned char>(next))) {
                lex_.advance(1); // consume the '-'
                ExprPtr operand = parseUnary();
                if (!operand) return nullptr;
                return makeUnary(UnaryOpKind::Neg, std::move(operand), start + 1);
            }
        }
        ExprPtr p = parsePrimary();
        if (!p) return nullptr;
        return parsePostfix(std::move(p));
    }

    // Peek-and-consume helper for `parseAddSub` / `parseMulDiv`. Returns true
    // when the next non-space token is the requested op AND it is not the
    // start of a numeric literal (e.g. `-3` must not be eaten as a binary
    // operator at the start of an expression). For mid-expression ops the
    // preceding parseMulDiv/parseUnary already consumed the LHS, so seeing
    // `-` here is always a binary minus.
    bool peekBinaryOp(char c, BinaryOpKind candidate,
                      BinaryOpKind& outOp, size_t& outPos) {
        lex_.skipSpaces();
        if (lex_.current() != c) return false;
        outPos = lex_.pos() + 1; // 1-based column position for error msgs
        lex_.advance(1);
        outOp = candidate;
        return true;
    }

    ExprPtr parsePrimary() {
        // Parenthesised sub-expression — overrides precedence so the user
        // can write `(sum(matrix) + 8) * 2`.
        if (lex_.consume('(')) {
            ExprPtr inner = parseExpr();
            if (!inner) return nullptr;
            if (!lex_.consume(')')) {
                error_ = "expected ')' at column " + std::to_string(lex_.pos() + 1);
                return nullptr;
            }
            return inner;
        }
        // number?
        double dv = 0.0;
        int64_t iv = 0;
        bool isFloat = false;
        size_t pos = 0;
        if (lex_.tryNumber(dv, iv, isFloat, pos)) {
            return isFloat ? makeFloat(dv, pos + 1) : makeInt(iv, pos + 1);
        }
        // string?
        std::string s;
        if (lex_.tryString(s, pos)) {
            return makeString(std::move(s), pos + 1);
        }
        // ident?
        std::string name = lex_.tryIdent(&pos);
        if (!name.empty()) return makeIdent(std::move(name), pos + 1);
        error_ = "expected expression at column " + std::to_string(lex_.pos() + 1);
        return nullptr;
    }

    ExprPtr parsePostfix(ExprPtr base) {
        while (true) {
            if (lex_.consume('.')) {
                size_t pos = 0;
                std::string name = lex_.tryIdent(&pos);
                if (name.empty()) {
                    error_ = "expected field name after '.' at column " +
                             std::to_string(lex_.pos() + 1);
                    return nullptr;
                }
                auto e = std::make_unique<Expr>();
                e->kind = ExprKind::FieldAccess;
                e->name = std::move(name);
                e->base = std::move(base);
                e->pos = pos + 1;
                base = std::move(e);
                continue;
            }
            if (lex_.consume('[')) {
                ExprPtr start = parseExpr();
                if (!start) return nullptr;
                if (!lex_.consumeRange()) {
                    error_ = "expected '..' inside slice at column " +
                             std::to_string(lex_.pos() + 1);
                    return nullptr;
                }
                ExprPtr endExpr = parseExpr();
                if (!endExpr) return nullptr;
                if (!lex_.consume(']')) {
                    error_ = "expected ']' at column " + std::to_string(lex_.pos() + 1);
                    return nullptr;
                }
                auto e = std::make_unique<Expr>();
                e->kind = ExprKind::Slice;
                e->base = std::move(base);
                e->sliceStart = std::move(start);
                e->sliceEnd = std::move(endExpr);
                e->pos = e->base->pos;
                base = std::move(e);
                continue;
            }
            if (lex_.consume('(')) {
                auto e = std::make_unique<Expr>();
                e->kind = ExprKind::Call;
                e->base = std::move(base);
                e->pos = e->base->pos;
                if (!lex_.consume(')')) {
                    while (true) {
                        ExprPtr arg = parseExpr();
                        if (!arg) return nullptr;
                        e->args.push_back(std::move(arg));
                        if (lex_.consume(',')) continue;
                        if (!lex_.consume(')')) {
                            error_ = "expected ',' or ')' in call at column " +
                                     std::to_string(lex_.pos() + 1);
                            return nullptr;
                        }
                        break;
                    }
                }
                base = std::move(e);
                continue;
            }
            break;
        }
        return base;
    }

    QueryLexer& lex_;
    std::string& error_;
};

} // namespace

ExprPtr parseQuery(const std::string& source, std::string& error) {
    error.clear();
    QueryLexer lex(source);
    Parser parser(lex, error);
    auto expr = parser.parseExpr();
    if (!expr) return nullptr;
    lex.skipSpaces();
    if (!lex.atEnd()) {
        error = "unexpected trailing input at column " + std::to_string(lex.pos() + 1);
        return nullptr;
    }
    return expr;
}

} // namespace topo::debug_query
