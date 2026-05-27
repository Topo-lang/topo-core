#include "topo/Transform/TokenStreamRewriter.h"
#include <unordered_map>

namespace topo::transform {

TokenStreamRewriter::TokenStreamRewriter(Lexer& lexer) : lenient_(lexer.isLenientMode()) {
    while (true) {
        Token tok = lexer.nextToken();
        tokens_.push_back(tok);
        if (tok.kind == TokenKind::Eof) break;
    }
}

std::vector<Token> TokenStreamRewriter::rewrite() {
    if (lenient_) {
        rewriteStageParens();
        rewriteAtVisibility();
        rewriteAtModifier();
        rewriteKeywordAliasText();
    }
    return std::move(tokens_);
}

// Normalize keyword alias text to canonical form.
// The Lexer maps aliases (e.g., "extern"→KW_external, "struct"→KW_type) to
// canonical TokenKind but preserves the original text. This pass fixes the text.
void TokenStreamRewriter::rewriteKeywordAliasText() {
    // Map: TokenKind → canonical text (only for keywords that have aliases).
    // `handler` / `flow` (and `record` / `union`) are deliberately absent: they have
    // exactly one surface spelling and no alias or `@`/`#[]` structural
    // variant, so there is nothing to normalize.
    static const std::unordered_map<TokenKind, const char*> canonicalText = {
        {TokenKind::KW_type, "type"},
        {TokenKind::KW_constraint, "constraint"},
        {TokenKind::KW_adapt, "adapt"},
        {TokenKind::KW_namespace, "namespace"},
        {TokenKind::KW_public, "public"},
        {TokenKind::KW_owned, "owned"},
        {TokenKind::KW_fn, "fn"},
        {TokenKind::KW_external, "external"},
    };
    for (auto& tok : tokens_) {
        auto it = canonicalText.find(tok.kind);
        if (it != canonicalText.end() && tok.text != it->second) {
            std::string origText = tok.text;
            tok.text = it->second;
            records_.push_back(
                {{tok.location.file, tok.location.line, tok.location.column},
                 origText, tok.text, TransformRecord::KeywordAlias});
        }
    }
}

bool TokenStreamRewriter::kindAt(size_t i, TokenKind kind) const {
    if (i >= tokens_.size()) return false;
    return tokens_[i].kind == kind;
}

void TokenStreamRewriter::replaceRange(size_t from, size_t to, const std::vector<Token>& replacement) {
    tokens_.erase(tokens_.begin() + static_cast<ptrdiff_t>(from), tokens_.begin() + static_cast<ptrdiff_t>(to));
    tokens_.insert(tokens_.begin() + static_cast<ptrdiff_t>(from), replacement.begin(), replacement.end());
}

SourceLocation TokenStreamRewriter::spanLoc(const Token& first, const Token& last) const {
    return {first.location.file,
            first.location.line,
            first.location.column,
            last.location.endLine,
            last.location.endColumn};
}

// Rewrite patterns:
// #[stage(N)] -> stage<N>
// @stage(N)   -> stage<N>
// stage(N)    -> stage<N>
void TokenStreamRewriter::rewriteStageParens() {
    for (size_t i = 0; i < tokens_.size();) {
        // Pattern: #[stage(N)]
        if (kindAt(i, TokenKind::Hash) && kindAt(i + 1, TokenKind::LBracket) && kindAt(i + 2, TokenKind::KW_stage) &&
            kindAt(i + 3, TokenKind::LParen) && kindAt(i + 4, TokenKind::IntegerLiteral) &&
            kindAt(i + 5, TokenKind::RParen) && kindAt(i + 6, TokenKind::RBracket)) {
            auto loc = spanLoc(tokens_[i], tokens_[i + 6]);
            std::string origText = "#[stage(" + tokens_[i + 4].text + ")]";
            std::string canonText = "stage<" + tokens_[i + 4].text + ">";

            Token stageT{TokenKind::KW_stage, "stage", loc};
            Token lAngle{TokenKind::LAngle, "<", loc};
            Token intT{TokenKind::IntegerLiteral, tokens_[i + 4].text, loc};
            Token rAngle{TokenKind::RAngle, ">", loc};

            replaceRange(i, i + 7, {stageT, lAngle, intT, rAngle});
            records_.push_back(
                {{loc.file, loc.line, loc.column}, origText, canonText, TransformRecord::StructuralRewrite});
            i += 4;
            continue;
        }
        // Pattern: @stage(N)
        if (kindAt(i, TokenKind::At) && kindAt(i + 1, TokenKind::KW_stage) && kindAt(i + 2, TokenKind::LParen) &&
            kindAt(i + 3, TokenKind::IntegerLiteral) && kindAt(i + 4, TokenKind::RParen)) {
            auto loc = spanLoc(tokens_[i], tokens_[i + 4]);
            std::string origText = "@stage(" + tokens_[i + 3].text + ")";
            std::string canonText = "stage<" + tokens_[i + 3].text + ">";

            Token stageT{TokenKind::KW_stage, "stage", loc};
            Token lAngle{TokenKind::LAngle, "<", loc};
            Token intT{TokenKind::IntegerLiteral, tokens_[i + 3].text, loc};
            Token rAngle{TokenKind::RAngle, ">", loc};

            replaceRange(i, i + 5, {stageT, lAngle, intT, rAngle});
            records_.push_back(
                {{loc.file, loc.line, loc.column}, origText, canonText, TransformRecord::StructuralRewrite});
            i += 4;
            continue;
        }
        // Pattern: stage(N) — parens instead of angle brackets
        if (kindAt(i, TokenKind::KW_stage) && kindAt(i + 1, TokenKind::LParen) &&
            kindAt(i + 2, TokenKind::IntegerLiteral) && kindAt(i + 3, TokenKind::RParen)) {
            auto loc = spanLoc(tokens_[i], tokens_[i + 3]);
            std::string origText = "stage(" + tokens_[i + 2].text + ")";
            std::string canonText = "stage<" + tokens_[i + 2].text + ">";

            Token stageT{TokenKind::KW_stage, "stage", tokens_[i].location};
            Token lAngle{TokenKind::LAngle, "<", loc};
            Token intT{TokenKind::IntegerLiteral, tokens_[i + 2].text, tokens_[i + 2].location};
            Token rAngle{TokenKind::RAngle, ">", loc};

            replaceRange(i, i + 4, {stageT, lAngle, intT, rAngle});
            records_.push_back(
                {{loc.file, loc.line, loc.column}, origText, canonText, TransformRecord::StructuralRewrite});
            i += 4;
            continue;
        }
        ++i;
    }
}

// Rewrite: @public -> public:   @private -> private:   @protected -> protected:
void TokenStreamRewriter::rewriteAtVisibility() {
    for (size_t i = 0; i < tokens_.size();) {
        if (kindAt(i, TokenKind::At)) {
            TokenKind nextKind = (i + 1 < tokens_.size()) ? tokens_[i + 1].kind : TokenKind::Eof;
            if (nextKind == TokenKind::KW_public || nextKind == TokenKind::KW_private ||
                nextKind == TokenKind::KW_protected) {
                auto loc = spanLoc(tokens_[i], tokens_[i + 1]);
                std::string kwText = tokens_[i + 1].text;
                std::string origText = "@" + kwText;
                std::string canonText = kwText + ":";

                Token kwT{nextKind, kwText, loc};
                Token colonT{TokenKind::Colon, ":", loc};

                replaceRange(i, i + 2, {kwT, colonT});
                records_.push_back(
                    {{loc.file, loc.line, loc.column}, origText, canonText, TransformRecord::StructuralRewrite});
                i += 2;
                continue;
            }
        }
        ++i;
    }
}

// Rewrite: @owned -> owned
void TokenStreamRewriter::rewriteAtModifier() {
    for (size_t i = 0; i < tokens_.size();) {
        if (kindAt(i, TokenKind::At) && kindAt(i + 1, TokenKind::KW_owned)) {
            auto loc = spanLoc(tokens_[i], tokens_[i + 1]);
            Token ownedT{TokenKind::KW_owned, "owned", loc};

            replaceRange(i, i + 2, {ownedT});
            records_.push_back(
                {{loc.file, loc.line, loc.column}, "@owned", "owned", TransformRecord::StructuralRewrite});
            i += 1;
            continue;
        }
        ++i;
    }
}

} // namespace topo::transform
