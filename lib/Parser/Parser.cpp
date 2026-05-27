#include "topo/Parser/Parser.h"

#include "topo/Stdlib/Types.h"

#include <charconv>
#include <set>
#include <system_error>

namespace topo {

// Stdlib type keyword tokens. These tokenize as keywords
// but stand in for identifiers in type positions, contributing their lowercase
// text to TypeNode::nameParts and an explicit stdlib::TypeId.
static stdlib::TypeId stdlibTokenTypeId(TokenKind k) {
    switch (k) {
    case TokenKind::KW_bool: return stdlib::TypeId::Bool;
    case TokenKind::KW_i64: return stdlib::TypeId::I64;
    case TokenKind::KW_f64: return stdlib::TypeId::F64;
    case TokenKind::KW_string: return stdlib::TypeId::String;
    case TokenKind::KW_optional: return stdlib::TypeId::Optional;
    case TokenKind::KW_slice: return stdlib::TypeId::Slice;
    case TokenKind::KW_bytes: return stdlib::TypeId::Bytes;
    case TokenKind::KW_u8: return stdlib::TypeId::U8;
    case TokenKind::KW_i32: return stdlib::TypeId::I32;
    case TokenKind::KW_u32: return stdlib::TypeId::U32;
    case TokenKind::KW_u64: return stdlib::TypeId::U64;
    case TokenKind::KW_f32: return stdlib::TypeId::F32;
    case TokenKind::KW_i8: return stdlib::TypeId::I8;
    case TokenKind::KW_i16: return stdlib::TypeId::I16;
    case TokenKind::KW_u16: return stdlib::TypeId::U16;
    case TokenKind::KW_time_ns: return stdlib::TypeId::TimeNs;
    case TokenKind::KW_uuid: return stdlib::TypeId::Uuid;
    case TokenKind::KW_decimal128: return stdlib::TypeId::Decimal128;
    case TokenKind::KW_record: return stdlib::TypeId::Record;
    case TokenKind::KW_array: return stdlib::TypeId::Array;
    case TokenKind::KW_union: return stdlib::TypeId::Union;
    default: return stdlib::TypeId::None;
    }
}

static bool isStdlibTypeToken(TokenKind k) {
    return stdlibTokenTypeId(k) != stdlib::TypeId::None;
}

// Non-throwing parser for non-negative decimal integer literals.
// Returns true on success; false if the text is empty, contains non-digit
// characters, represents a negative value, or overflows int.
static bool tryParseNonNegativeInt(const std::string& text, int& out) {
    if (text.empty()) return false;
    int value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc()) return false;
    if (result.ptr != last) return false;
    if (value < 0) return false;
    out = value;
    return true;
}

// Non-throwing parser for a signed int64 decimal literal. Same shape as
// tryParseNonNegativeInt but accepts negative values and the full int64
// range. Used in place of `std::stoll(tok.text)` so a fuzzer-mutated or
// otherwise non-numeric Integer-kind token surfaces as a parse diagnostic
// instead of an uncaught std::invalid_argument / std::out_of_range crash.
static bool tryParseInt64(const std::string& text, int64_t& out) {
    if (text.empty()) return false;
    int64_t value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc()) return false;
    if (result.ptr != last) return false;
    out = value;
    return true;
}

Parser::Parser(Lexer& lexer, DiagnosticEngine& diag) : lexer_(lexer), diag_(diag) {
    current_ = lexer_.nextToken();
}

// --- Token management ---

const Token& Parser::peek() const {
    return current_;
}

const Token& Parser::peekNext() {
    if (!hasLookahead_) {
        if (!pendingTokens_.empty()) {
            lookahead_ = pendingTokens_.back();
            pendingTokens_.pop_back();
        } else {
            lookahead_ = lexer_.nextToken();
        }
        hasLookahead_ = true;
    }
    return lookahead_;
}

Token Parser::advance() {
    Token prev = current_;
    if (hasLookahead_) {
        current_ = lookahead_;
        hasLookahead_ = false;
    } else if (!pendingTokens_.empty()) {
        current_ = pendingTokens_.back();
        pendingTokens_.pop_back();
    } else {
        current_ = lexer_.nextToken();
    }
    return prev;
}

bool Parser::check(TokenKind kind) const {
    return current_.kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

Token Parser::expect(TokenKind kind, const std::string& message) {
    if (check(kind)) {
        return advance();
    }
    diag_.error(current_.location, message);
    return current_;
}

// --- Error recovery ---

void Parser::synchronize() {
    auto startLoc = current_.location;
    while (!check(TokenKind::Eof)) {
        if (check(TokenKind::Semicolon)) {
            advance();
            return;
        }
        if (check(TokenKind::RCurly)) {
            return;
        }
        if (isVisibilityKeyword(current_.kind) || check(TokenKind::KW_fn) || check(TokenKind::KW_handler) ||
            check(TokenKind::KW_flow) || check(TokenKind::KW_namespace) ||
            check(TokenKind::KW_using) || check(TokenKind::KW_import) || check(TokenKind::KW_class) ||
            check(TokenKind::KW_type) || check(TokenKind::KW_constraint) || check(TokenKind::KW_adapt) ||
            check(TokenKind::KW_instantiate) || check(TokenKind::KW_priority)) {
            return;
        }
        advance();
    }
    // If we didn't advance at all, force progress
    if (current_.location == startLoc && !check(TokenKind::Eof)) {
        advance();
    }
}

// --- Modifier collection ---

std::vector<ModifierData> Parser::collectModifiers() {
    std::vector<ModifierData> mods;

    while (!diag_.reachedLimit()) {
        if (check(TokenKind::KW_static)) {
            mods.push_back({ModifierData::Static, current_.location, {}});
            advance();
        } else if (check(TokenKind::KW_explicit)) {
            mods.push_back({ModifierData::Explicit, current_.location, {}});
            advance();
        } else if (check(TokenKind::KW_comptime)) {
            mods.push_back({ModifierData::Comptime, current_.location, {}});
            advance();
        } else if (check(TokenKind::KW_external)) {
            mods.push_back({ModifierData::External, current_.location, {}});
            advance();
        } else {
            break;
        }
    }

    return mods;
}

// --- Utilities ---

bool Parser::isNamespaceIdentifier() const {
    return check(TokenKind::Identifier) || check(TokenKind::KW_internal);
}

Token Parser::expectNamespaceIdentifier(const std::string& message) {
    if (isNamespaceIdentifier()) {
        return advance();
    }
    diag_.error(current_.location, message);
    return current_;
}

bool Parser::isVisibilityKeyword(TokenKind kind) const {
    return kind == TokenKind::KW_public || kind == TokenKind::KW_protected || kind == TokenKind::KW_private ||
           kind == TokenKind::KW_internal || kind == TokenKind::KW_ignore;
}

PriorityLevel Parser::parsePriorityLevel() {
    // Consume 'priority'
    advance();
    expect(TokenKind::LParen, "expected '(' after 'priority'");

    Token levelTok = expect(TokenKind::Identifier, "expected priority level name");
    PriorityLevel level = PriorityLevel::Normal;

    if (levelTok.text == "critical")
        level = PriorityLevel::Critical;
    else if (levelTok.text == "high")
        level = PriorityLevel::High;
    else if (levelTok.text == "normal")
        level = PriorityLevel::Normal;
    else if (levelTok.text == "low")
        level = PriorityLevel::Low;
    else if (levelTok.text == "background")
        level = PriorityLevel::Background;
    else {
        diag_.error(
            levelTok.location,
            "unknown priority level '" + levelTok.text + "'; expected critical, high, normal, low, or background");
    }

    expect(TokenKind::RParen, "expected ')' after priority level");
    expect(TokenKind::Colon, "expected ':' after priority section header");

    return level;
}

// --- Data-aware optimization hints ---

int64_t Parser::parseCardinalityValue() {
    Token tok = expect(TokenKind::IntegerLiteral, "expected cardinality value");
    // Guarded conversion: the lexer's Integer-kind token text is not
    // strictly guaranteed to fit in int64 (or to be purely digits — fuzzer
    // mutations have produced both shapes). std::stoll would throw an
    // uncaught std::invalid_argument / std::out_of_range and terminate; a
    // parse diagnostic plus a 0 fallback keeps the parser alive so the
    // rest of the file still surfaces useful errors.
    int64_t value = 0;
    if (!tryParseInt64(tok.text, value)) {
        diag_.error(tok.location, "invalid cardinality integer literal '" + tok.text + "'");
        value = 0;
    }

    // Check for suffix: k, M
    if (check(TokenKind::Identifier)) {
        const auto& suffix = peek().text;
        if (suffix == "k" || suffix == "K") {
            advance();
            value *= 1000;
        } else if (suffix == "M" || suffix == "m") {
            advance();
            value *= 1000000;
        }
    }

    return value;
}

AccessPattern Parser::parseAccessPattern(int& tiledSize) {
    Token modeTok = expect(TokenKind::Identifier, "expected access pattern name");
    AccessPattern pattern = AccessPattern::None;

    if (modeTok.text == "streaming")
        pattern = AccessPattern::Streaming;
    else if (modeTok.text == "random")
        pattern = AccessPattern::Random;
    else if (modeTok.text == "tiled")
        pattern = AccessPattern::Tiled;
    else if (modeTok.text == "gather_scatter")
        pattern = AccessPattern::GatherScatter;
    else {
        diag_.error(
            modeTok.location,
            "unknown access pattern '" + modeTok.text + "'; expected streaming, random, tiled, or gather_scatter");
    }

    // Optional tile size for tiled mode: access(tiled, 64)
    if (pattern == AccessPattern::Tiled && match(TokenKind::Comma)) {
        Token sizeTok = expect(TokenKind::IntegerLiteral, "expected tile size after 'tiled,'");
        // Same guard rationale as parseCardinalityValue: a non-numeric or
        // overflowing Integer-kind token must not crash the process.
        if (!tryParseNonNegativeInt(sizeTok.text, tiledSize) || tiledSize <= 0) {
            diag_.error(sizeTok.location, "tile size must be a positive integer (got '" + sizeTok.text + "')");
            tiledSize = 0;
        }
    }

    return pattern;
}

void Parser::parseHints(FnDecl& decl) {
    // Parse comma-separated hints: cardinality(...), access(...)
    while (check(TokenKind::Identifier) && (peek().text == "cardinality" || peek().text == "access") &&
           !diag_.reachedLimit()) {
        Token hintTok = advance();

        if (hintTok.text == "cardinality") {
            expect(TokenKind::LParen, "expected '(' after 'cardinality'");
            CardinalityHint hint;
            hint.min = parseCardinalityValue();

            if (match(TokenKind::DotDot)) {
                // Range: min..max or min..
                if (check(TokenKind::IntegerLiteral)) {
                    hint.max = parseCardinalityValue();
                }
                // else: open-ended (min..), max stays -1
            } else {
                // Single value: cardinality(N) means exact
                hint.max = hint.min;
            }

            expect(TokenKind::RParen, "expected ')' after cardinality");
            decl.cardinality = hint;
        } else if (hintTok.text == "access") {
            expect(TokenKind::LParen, "expected '(' after 'access'");
            int tiledSize = 0;
            decl.accessPattern = parseAccessPattern(tiledSize);
            decl.tiledSize = tiledSize;
            expect(TokenKind::RParen, "expected ')' after access pattern");
        }

        // Consume separating comma if more hints follow
        if (check(TokenKind::Comma) && (peekNext().kind == TokenKind::Identifier) &&
            (peekNext().text == "cardinality" || peekNext().text == "access")) {
            advance(); // consume comma
        } else {
            break;
        }
    }
}

bool Parser::isTypeStart() const {
    return check(TokenKind::KW_void) || check(TokenKind::KW_const) || check(TokenKind::KW_owned) ||
           check(TokenKind::KW_shared) || check(TokenKind::KW_weak) || check(TokenKind::Identifier) ||
           check(TokenKind::KW_typename) ||
           // Stdlib type keywords are valid type starts.
           check(TokenKind::KW_bool) || check(TokenKind::KW_i64) || check(TokenKind::KW_f64) ||
           check(TokenKind::KW_string) || check(TokenKind::KW_optional) || check(TokenKind::KW_slice) ||
           check(TokenKind::KW_bytes) ||
           // Width-extension scalar keywords.
           check(TokenKind::KW_u8) || check(TokenKind::KW_i32) || check(TokenKind::KW_u32) ||
           check(TokenKind::KW_u64) || check(TokenKind::KW_f32) ||
           check(TokenKind::KW_i8) || check(TokenKind::KW_i16) || check(TokenKind::KW_u16) ||
           // Semantic scalar (i64-isomorphic, domain-tagged).
           check(TokenKind::KW_time_ns) ||
           // 16-byte value scalars.
           check(TokenKind::KW_uuid) || check(TokenKind::KW_decimal128) ||
           // Composite stdlib types.
           check(TokenKind::KW_record) || check(TokenKind::KW_array) ||
           check(TokenKind::KW_union);
}

// --- Top level ---

std::unique_ptr<TopoFile> Parser::parseTopoFile() {
    auto file = std::make_unique<TopoFile>(current_.location);

    while (!check(TokenKind::Eof) && !diag_.reachedLimit()) {
        auto decl = parseTopLevelDecl();
        if (decl) {
            file->declarations.push_back(std::move(decl));
        }
    }

    return file;
}

ASTNodePtr Parser::parseTopLevelDecl() {
    if (check(TokenKind::KW_import)) {
        return parseImport();
    }
    if (check(TokenKind::Identifier) && current_.text == "std" && peekNext().is(TokenKind::ColonColon)) {
        return parseStdImportAsDataDecl();
    }
    if (check(TokenKind::KW_using)) {
        return parseTypeAliasAsDataDecl();
    }
    if (check(TokenKind::KW_internal)) {
        // internal namespace ...
        if (peekNext().is(TokenKind::KW_namespace)) {
            advance(); // consume 'internal'
            auto ns = parseNamespaceDecl();
            if (ns) {
                static_cast<NamespaceDecl&>(*ns).isInternal = true;
            }
            return ns;
        }
    }
    if (check(TokenKind::KW_namespace)) {
        return parseNamespaceDecl();
    }
    // `debug TargetName { ... }` (contextual; `debug` is not a
    // lexer keyword). Recognized only when followed by an identifier; this
    // way a user namespace member named `debug()` keeps working — function
    // names appear in member position with a return type prefix, never as
    // the leading top-level token.
    if (isDebugDeclStart()) {
        return parseDebugDecl();
    }

    diag_.error(current_.location, "expected top-level declaration (import, using, namespace, or debug)");
    synchronize();
    return nullptr;
}

// --- Import: import path; / import path { Sym1, Sym2 }; ---

ASTNodePtr Parser::parseImport() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_import, "expected 'import'");

    // Parse import path: Identifier { "/" Identifier }
    Token first = expect(TokenKind::Identifier, "expected module name after 'import'");
    std::string path = first.text;
    while (match(TokenKind::Slash)) {
        Token segment = expect(TokenKind::Identifier, "expected identifier after '/' in import path");
        path += "/" + segment.text;
    }

    // Parse optional symbol selection: "{" Identifier { "," Identifier } "}"
    std::vector<std::string> selectedSymbols;
    if (match(TokenKind::LCurly)) {
        do {
            Token sym = expect(TokenKind::Identifier, "expected symbol name in import selection");
            selectedSymbols.push_back(sym.text);
        } while (match(TokenKind::Comma));
        expect(TokenKind::RCurly, "expected '}' after import symbol list");
    }

    expect(TokenKind::Semicolon, "expected ';' after import statement");
    return std::make_unique<Import>(loc, std::move(path), std::move(selectedSymbols));
}

// --- TypeAliasDecl → DataDecl: using Name = Type; ---

ASTNodePtr Parser::parseTypeAliasAsDataDecl() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_using, "expected 'using'");

    Token name;
    // Accept stdlib keyword tokens as alias names (legacy fixtures alias
    // `bool` / `string` / etc to `std::<backend>::<name>`). The alias is
    // shadowed by the stdlib keyword in type position, so this stays
    // syntactically permissive without changing semantics.
    if (check(TokenKind::Identifier) || isStdlibTypeToken(current_.kind)) {
        name = advance();
    } else {
        name = expect(TokenKind::Identifier, "expected name after 'using'");
    }
    expect(TokenKind::Assign, "expected '=' after name");

    TypeNode type = parseType();
    expect(TokenKind::Semicolon, "expected ';' after type alias declaration");

    auto decl = std::make_unique<DataDecl>(loc);
    decl->name = name.text;
    decl->type = std::move(type);
    return decl;
}

// --- NamespaceDecl ---

ASTNodePtr Parser::parseNamespaceDecl() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_namespace, "expected 'namespace'");

    std::vector<std::string> path;
    Token first = expectNamespaceIdentifier("expected namespace name");
    path.push_back(first.text);
    while (match(TokenKind::ColonColon)) {
        Token part = expectNamespaceIdentifier("expected identifier after '::'");
        path.push_back(part.text);
    }

    expect(TokenKind::LCurly, "expected '{' after namespace path");

    auto ns = std::make_unique<NamespaceDecl>(loc, std::move(path));

    while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
        auto loopLoc = current_.location;
        // lifetime declarations at namespace level (before visibility sections)
        if (check(TokenKind::Identifier) && current_.text == "lifetime" && peekNext().is(TokenKind::Identifier)) {
            auto ltd = parseLifetimeAsDataDecl();
            if (ltd) {
                auto section = std::make_unique<VisibilitySection>(ltd->location, Visibility::Protected);
                section->members.push_back(std::move(ltd));
                ns->sections.push_back(std::move(section));
            }
        } else if (check(TokenKind::KW_fn) || check(TokenKind::KW_handler) || check(TokenKind::KW_flow)) {
            // fn / handler / flow at namespace level (before/between
            // visibility sections). handler and flow desugar onto the same
            // FnDecl / pipeline FnLogicBlock the `fn` form produces, so they
            // sit in a protected section identically.
            ASTNodePtr member;
            if (check(TokenKind::KW_handler)) {
                member = parseHandlerDecl();
            } else if (check(TokenKind::KW_flow)) {
                member = parseFlowDecl();
            } else {
                member = parseFnLogicBlock();
            }
            if (member) {
                auto section = std::make_unique<VisibilitySection>(member->location, Visibility::Protected);
                section->members.push_back(std::move(member));
                ns->sections.push_back(std::move(section));
            }
        } else if (isVisibilityKeyword(current_.kind)) {
            auto section = parseVisibilitySection();
            if (section) {
                ns->sections.push_back(std::move(section));
            }
        } else {
            diag_.error(current_.location,
                        "expected visibility specifier (public, protected, "
                        "private, or internal)");
            advance(); // consume unrecognized token to prevent infinite loop
            synchronize();
        }
        // Stall detection: if token position hasn't advanced, force progress
        if (current_.location == loopLoc) {
            advance();
        }
    }

    expect(TokenKind::RCurly, "expected '}' to close namespace");
    return ns;
}

// --- VisibilitySection ---

ASTNodePtr Parser::parseVisibilitySection() {
    SourceLocation loc = current_.location;

    Visibility vis;
    if (check(TokenKind::KW_public))
        vis = Visibility::Public;
    else if (check(TokenKind::KW_protected))
        vis = Visibility::Protected;
    else if (check(TokenKind::KW_internal))
        vis = Visibility::Internal;
    else if (check(TokenKind::KW_ignore))
        vis = Visibility::Ignore;
    else
        vis = Visibility::Private;
    advance(); // consume visibility keyword

    expect(TokenKind::Colon, "expected ':' after visibility specifier");

    auto section = std::make_unique<VisibilitySection>(loc, vis);

    PriorityLevel currentPriority = PriorityLevel::Normal;

    while (!check(TokenKind::Eof) && !check(TokenKind::RCurly) && !isVisibilityKeyword(current_.kind) &&
           !diag_.reachedLimit()) {
        auto loopLoc = current_.location;
        if (check(TokenKind::KW_priority)) {
            currentPriority = parsePriorityLevel();
            continue;
        }
        auto member = parseMemberDecl();
        if (member) {
            // Apply current priority to function declarations
            if (member->kind == ASTKind::FnDecl) {
                static_cast<FnDecl&>(*member).priority = currentPriority;
            }
            section->members.push_back(std::move(member));
        }
        if (current_.location == loopLoc) {
            advance();
        }
    }

    return section;
}

// --- Member declarations inside a visibility section ---

ASTNodePtr Parser::parseMemberDecl() {
    // Collect simple modifiers (static, explicit, comptime)
    auto mods = collectModifiers();

    // Check for comptime+fn or comptime+if before generic fn dispatch
    bool hasComptime = false;
    for (const auto& m : mods) {
        if (m.kind == ModifierData::Comptime) {
            hasComptime = true;
            break;
        }
    }
    if (hasComptime) {
        if (check(TokenKind::KW_if)) {
            advance(); // consume 'if'
            SourceLocation loc = mods[0].location;
            auto node = std::make_unique<IfDecl>(loc);
            node->modifiers = std::move(mods);

            expect(TokenKind::LParen, "expected '(' after 'comptime if'");
            node->condition = parseExpression();
            expect(TokenKind::RParen, "expected ')' after condition");

            expect(TokenKind::LCurly, "expected '{' after comptime if condition");
            while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
                auto innerLoc = current_.location;
                auto member = parseMemberDecl();
                if (member) node->thenBody.push_back(std::move(member));
                if (current_.location == innerLoc) advance();
            }
            expect(TokenKind::RCurly, "expected '}' after comptime if body");

            if (match(TokenKind::KW_else)) {
                expect(TokenKind::LCurly, "expected '{' after 'else'");
                while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
                    auto innerLoc = current_.location;
                    auto member = parseMemberDecl();
                    if (member) node->elseBody.push_back(std::move(member));
                    if (current_.location == innerLoc) advance();
                }
                expect(TokenKind::RCurly, "expected '}' after else body");
            }

            return node;
        }
        if (check(TokenKind::KW_fn)) {
            // comptime fn name(params) -> Type { expr }
            advance(); // consume 'fn'
            SourceLocation loc = mods[0].location;

            Token nameTok = expect(TokenKind::Identifier, "expected function name after 'comptime fn'");
            auto node = std::make_unique<FnDecl>(loc);
            node->modifiers = std::move(mods);
            node->name = nameTok.text;
            node->isRustStyle = true;

            expect(TokenKind::LParen, "expected '(' after comptime fn name");
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
                auto innerLoc = current_.location;
                Parameter param;
                param.type = parseType();
                Token pname = expect(TokenKind::Identifier, "expected parameter name");
                param.name = pname.text;
                node->params.push_back(std::move(param));
                if (!check(TokenKind::RParen)) {
                    expect(TokenKind::Comma, "expected ',' or ')' in parameter list");
                }
                if (current_.location == innerLoc) advance();
            }
            expect(TokenKind::RParen, "expected ')' after parameter list");

            expect(TokenKind::Arrow, "expected '->' after comptime fn parameters");
            node->returnType = parseType();

            expect(TokenKind::LCurly, "expected '{' after return type");
            node->comptimeBody = parseExpression();
            expect(TokenKind::RCurly, "expected '}' after comptime fn body");

            return node;
        }
    }

    // fn -> FnLogicBlock or operator decl
    if (check(TokenKind::KW_fn)) {
        // Check if it's fn operator+ ...
        if (peekNext().is(TokenKind::KW_operator)) {
            auto result = parseOperatorDecl();
            if (result) result->modifiers = std::move(mods);
            return result;
        }
        auto result = parseFnLogicBlock();
        if (result) result->modifiers = std::move(mods);
        return result;
    }

    // handler -> FnDecl carrying the Handler marker
    if (check(TokenKind::KW_handler)) {
        auto result = parseHandlerDecl();
        if (result) {
            for (auto& m : mods) result->modifiers.push_back(m);
        }
        return result;
    }

    // flow -> pipeline FnLogicBlock carrying the Flow marker
    if (check(TokenKind::KW_flow)) {
        auto result = parseFlowDecl();
        if (result) {
            for (auto& m : mods) result->modifiers.push_back(m);
        }
        return result;
    }

    // std::import() -> DataDecl
    if (check(TokenKind::Identifier) && current_.text == "std" && peekNext().is(TokenKind::ColonColon)) {
        auto result = parseStdImportAsDataDecl();
        if (result) result->modifiers = std::move(mods);
        return result;
    }

    // using -> DataDecl (type alias)
    if (check(TokenKind::KW_using)) {
        auto result = parseTypeAliasAsDataDecl();
        if (result) result->modifiers = std::move(mods);
        return result;
    }

    // internal namespace -> nested internal NamespaceDecl
    if (check(TokenKind::KW_internal) && peekNext().is(TokenKind::KW_namespace)) {
        advance(); // consume 'internal'
        auto ns = parseNamespaceDecl();
        if (ns) {
            static_cast<NamespaceDecl&>(*ns).isInternal = true;
        }
        return ns;
    }

    // namespace -> nested NamespaceDecl
    if (check(TokenKind::KW_namespace)) {
        return parseNamespaceDecl();
    }

    // template<...> -> TemplateDecl wrapping function or type
    if (check(TokenKind::KW_template)) {
        return parseTemplateDecl();
    }

    // type -> TypeDecl
    if (check(TokenKind::KW_type)) {
        auto result = parseTypeDecl();
        if (result) result->modifiers = std::move(mods);
        return result;
    }

    // class -> TypeDecl (deprecated, with warning)
    if (check(TokenKind::KW_class)) {
        diag_.warning(current_.location, "'class' is deprecated, use 'type' instead");
        auto result = parseTypeDecl();
        if (result) result->modifiers = std::move(mods);
        return result;
    }

    // constraint -> TypeDecl with Constraint modifier
    if (check(TokenKind::KW_constraint)) {
        auto result = parseConstraintAsTypeDecl();
        if (result) result->modifiers.insert(result->modifiers.begin(), mods.begin(), mods.end());
        return result;
    }

    // adapt -> TypeDecl with Adapt modifier
    if (check(TokenKind::KW_adapt)) {
        auto result = parseAdaptAsTypeDecl();
        if (result) result->modifiers.insert(result->modifiers.begin(), mods.begin(), mods.end());
        return result;
    }

    // instantiate -> DataDecl with Instantiate modifier
    if (check(TokenKind::KW_instantiate)) {
        auto result = parseInstantiateAsDataDecl();
        if (result) result->modifiers.insert(result->modifiers.begin(), mods.begin(), mods.end());
        return result;
    }

    // typefn -> TypeDecl with TypeFn modifier
    if (check(TokenKind::KW_typefn)) {
        auto result = parseTypeFnAsTypeDecl();
        if (result) result->modifiers.insert(result->modifiers.begin(), mods.begin(), mods.end());
        return result;
    }

    // virtual -> prohibited keyword detection
    if (check(TokenKind::KW_virtual)) {
        diag_.error(current_.location, "'virtual' is prohibited in Topo (C002)");
        synchronize();
        return nullptr;
    }

    // lifetime name = range;
    if (check(TokenKind::Identifier) && current_.text == "lifetime" && peekNext().is(TokenKind::Identifier)) {
        auto result = parseLifetimeAsDataDecl();
        if (result) result->modifiers.insert(result->modifiers.begin(), mods.begin(), mods.end());
        return result;
    }

    // Determine C++ style vs Rust style function declaration
    if (check(TokenKind::KW_void) || check(TokenKind::KW_const) || check(TokenKind::KW_owned) ||
        check(TokenKind::KW_shared) || check(TokenKind::KW_weak) ||
        // Stdlib type keywords (bool/i64/f64/string/optional/slice)
        // open a C++-style member declaration just like KW_void.
        isStdlibTypeToken(current_.kind)) {
        auto result = parseFnDeclCppStyle();
        if (result) {
            // Transfer static modifier
            for (const auto& m : mods) {
                if (m.kind == ModifierData::Static) static_cast<FnDecl&>(*result).isStatic = true;
            }
            result->modifiers = std::move(mods);
        }
        return result;
    }

    if (check(TokenKind::Identifier)) {
        const Token& next = peekNext();
        if (next.is(TokenKind::LParen) || next.is(TokenKind::Dot)) {
            auto result = parseFnDeclRustStyle();
            if (result) {
                for (const auto& m : mods) {
                    if (m.kind == ModifierData::Static) static_cast<FnDecl&>(*result).isStatic = true;
                }
                result->modifiers = std::move(mods);
            }
            return result;
        }
        // Otherwise: could be a user type like "Config getName(...);"
        auto result = parseFnDeclCppStyle();
        if (result) {
            for (const auto& m : mods) {
                if (m.kind == ModifierData::Static) static_cast<FnDecl&>(*result).isStatic = true;
            }
            result->modifiers = std::move(mods);
        }
        return result;
    }

    diag_.error(current_.location, "expected member declaration");
    synchronize();
    return nullptr;
}

// --- FnLogicBlock: fn funcName { ... } (was parseFunctionLogicBlock) ---

ASTNodePtr Parser::parseFnLogicBlock() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_fn, "expected 'fn'");

    std::string name = parseFuncName();

    SourceLocation lbraceLoc = current_.location;
    expect(TokenKind::LCurly, "expected '{' after fn name");

    auto block = std::make_unique<FnLogicBlock>(loc, name);

    // Detect pipeline mode
    bool isPipelineMode = false;
    if (check(TokenKind::Identifier)) {
        if (peekNext().is(TokenKind::Arrow)) {
            isPipelineMode = true;
        } else if (peekNext().is(TokenKind::ColonColon)) {
            std::vector<Token> buffered;
            buffered.push_back(advance());
            while (check(TokenKind::ColonColon)) {
                buffered.push_back(advance());
                if (check(TokenKind::Identifier)) {
                    buffered.push_back(advance());
                } else {
                    break;
                }
            }
            if (check(TokenKind::Arrow)) {
                isPipelineMode = true;
            }
            for (auto it = buffered.rbegin(); it != buffered.rend(); ++it) {
                pendingTokens_.push_back(current_);
                current_ = *it;
            }
            hasLookahead_ = false;
        }
    }

    if (isPipelineMode) {
        parsePipelineEdges(block->pipelineEdges);
    } else {
        while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
            auto loopLoc = current_.location;
            auto op = parseOperationDecl();
            if (op) {
                block->operations.push_back(std::move(op));
            }
            if (current_.location == loopLoc) {
                advance();
            }
        }
    }

    if (!match(TokenKind::RCurly)) {
        diag_.error(current_.location,
                    "expected '}' to close fn block (opened at " + std::to_string(lbraceLoc.line) + ":" +
                        std::to_string(lbraceLoc.column) + ")");
    }

    return block;
}

// --- handler Name(In inName) -> Out;  ⇒  FnDecl + Handler marker ---
//
// A handler is a pure logic Functor: its In/Out is the entire data it
// touches (signature is the contract). It desugars to an ordinary
// Rust-style single-In/single-Out FnDecl so Sema, the SymbolTable, and
// every checker treat it exactly like any declared function — purity is
// then enforced downstream by the existing checkers once the handler
// participates in a flow, not self-asserted here. The Handler marker is
// the only thing distinguishing the surface form, kept so the
// declaration round-trips back to `handler`.
ASTNodePtr Parser::parseHandlerDecl() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_handler, "expected 'handler'");

    auto decl = std::make_unique<FnDecl>(loc);
    decl->isRustStyle = true;
    decl->modifiers.push_back({ModifierData::Handler, loc, {}});
    decl->name = parseFuncName();

    expect(TokenKind::LParen, "expected '(' after handler name");
    // A handler's input is the single value it consumes (the Functor's
    // In). One parameter keeps the f(In)->Out contract literal; an empty
    // input list is allowed for source handlers (nothing consumed).
    if (!check(TokenKind::RParen)) {
        do {
            Parameter param;
            param.type = parseType();
            Token paramName = expect(TokenKind::Identifier, "expected input name in handler");
            param.name = paramName.text;
            decl->params.push_back(std::move(param));
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "expected ')' after handler input");

    if (decl->params.size() > 1) {
        diag_.error(loc,
                    "a handler takes a single input value (the Functor's In); "
                    "aggregate multiple inputs into one record<...>");
    }

    expect(TokenKind::Arrow, "expected '->' before handler output type");
    decl->returnType = parseType();

    expect(TokenKind::Semicolon, "expected ';' after handler declaration");
    return decl;
}

// --- flow Name { a -> b; ... }  ⇒  pipeline FnLogicBlock + Flow marker ---
//
// A flow is a DAG of handlers. It desugars to a pipeline-mode
// FnLogicBlock — the identical AST the pipeline `fn` form produces — so
// StageIsolationCheck / VisibilityCheck / PurityCheck consume it with no
// checker change (parallel branches in a flow yield a shared-stage logic
// block, exactly the shape PurityCheck guards). The Flow marker only
// preserves the surface keyword for round-trip.
ASTNodePtr Parser::parseFlowDecl() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_flow, "expected 'flow'");

    std::string name = parseFuncName();
    SourceLocation lbraceLoc = current_.location;
    expect(TokenKind::LCurly, "expected '{' after flow name");

    auto block = std::make_unique<FnLogicBlock>(loc, name);
    block->modifiers.push_back({ModifierData::Flow, loc, {}});

    // A flow is always a DAG of edges — never the operation/stage form.
    parsePipelineEdges(block->pipelineEdges);

    if (!match(TokenKind::RCurly)) {
        diag_.error(current_.location,
                    "expected '}' to close flow (opened at " + std::to_string(lbraceLoc.line) + ":" +
                        std::to_string(lbraceLoc.column) + ")");
    }
    return block;
}

// --- LifetimeDecl → DataDecl: lifetime name = funcRef ['..' [funcRef]] ; ---

ASTNodePtr Parser::parseLifetimeAsDataDecl() {
    SourceLocation loc = current_.location;
    advance(); // consume 'lifetime'

    std::string name = current_.text;
    expect(TokenKind::Identifier, "expected lifetime name");

    expect(TokenKind::Assign, "expected '=' after lifetime name");

    std::string startFunc = parseFuncName();

    auto node = std::make_unique<DataDecl>(loc);
    node->modifiers.push_back({ModifierData::Lifetime, loc, {}});
    node->name = name;
    node->startFunc = startFunc;

    if (check(TokenKind::DotDot)) {
        advance(); // consume '..'
        if (check(TokenKind::Identifier)) {
            node->endFunc = parseFuncName();
        } else {
            node->isOpenEnded = true;
        }
    } else {
        node->isSingleFunc = true;
    }

    expect(TokenKind::Semicolon, "expected ';' after lifetime declaration");
    return node;
}

// --- OperationDecl: [stage<N>] (funcCall() | varName = expr); ---

ASTNodePtr Parser::parseOperationDecl() {
    SourceLocation loc = current_.location;
    auto op = std::make_unique<OperationDecl>(loc);

    // Optional stage specifier
    if (check(TokenKind::KW_stage)) {
        advance(); // consume 'stage'
        expect(TokenKind::LAngle, "expected '<' after 'stage'");
        // Reject negative stage numbers explicitly (spec: stage numbers are
        // non-negative). Consume the '-' so recovery can continue.
        if (check(TokenKind::Minus)) {
            diag_.error(current_.location,
                        "stage number must be non-negative (S008)");
            advance(); // consume '-'
            if (check(TokenKind::IntegerLiteral)) {
                advance(); // consume the digits so recovery continues
            }
        } else {
            Token num = expect(TokenKind::IntegerLiteral, "expected integer after 'stage<'");
            int parsed = 0;
            if (tryParseNonNegativeInt(num.text, parsed)) {
                op->stage = parsed;
            } else if (num.kind == TokenKind::IntegerLiteral) {
                // Lexer only produces digit-only IntegerLiteral, so the most
                // likely failure here is integer overflow.
                diag_.error(num.location,
                            "stage number '" + num.text + "' is not a valid non-negative integer");
            }
            // else: expect() already reported a diagnostic for the missing literal.
        }
        expect(TokenKind::RAngle, "expected '>' after stage number");
    }

    // Lookahead: Identifier '=' → assignment; otherwise → function call
    if (check(TokenKind::Identifier) && peekNext().is(TokenKind::Assign)) {
        Token varTok = advance();
        op->varName = varTok.text;
        advance(); // consume '='
        op->expr = parseExpression();
        expect(TokenKind::Semicolon, "expected ';' after assignment");
    } else {
        op->funcName = parseQualifiedFuncRef();
        expect(TokenKind::LParen, "expected '(' in operation");
        if (!check(TokenKind::RParen)) {
            while (!check(TokenKind::RParen) && !check(TokenKind::Semicolon) && !check(TokenKind::Eof)) {
                advance();
            }
        }
        expect(TokenKind::RParen, "expected ')' in operation");

        if (check(TokenKind::Arrow)) {
            advance();
            op->returnBinding = parseReturnBinding();
        }

        expect(TokenKind::Semicolon, "expected ';' after operation");
    }

    return op;
}

// --- StdImportDecl → DataDecl: std::import("path.h", Type); ---

ASTNodePtr Parser::parseStdImportAsDataDecl() {
    SourceLocation loc = current_.location;

    expect(TokenKind::Identifier, "expected 'std'");
    expect(TokenKind::ColonColon, "expected '::'");
    expect(TokenKind::KW_import, "expected 'import'");
    expect(TokenKind::LParen, "expected '(' after 'std::import'");

    Token pathTok = expect(TokenKind::StringLiteral, "expected string literal for import path");
    std::string path = pathTok.text;

    std::string typeName;
    if (match(TokenKind::Comma)) {
        auto parts = parseQualifiedName();
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) typeName += "::";
            typeName += parts[i];
        }
    }

    expect(TokenKind::RParen, "expected ')'");
    expect(TokenKind::Semicolon, "expected ';' after std::import");

    auto decl = std::make_unique<DataDecl>(loc);
    decl->importPath = std::move(path);
    decl->name = std::move(typeName);
    return decl;
}

// --- Expression parsing (precedence climbing) ---

int Parser::getOperatorPrecedence(TokenKind kind) {
    switch (kind) {
    case TokenKind::PipePipe: return 1;
    case TokenKind::AmpAmp: return 2;
    case TokenKind::EqEq:
    case TokenKind::NotEq: return 3;
    case TokenKind::LAngle:
    case TokenKind::RAngle:
    case TokenKind::LessEq:
    case TokenKind::GreaterEq: return 4;
    case TokenKind::Plus:
    case TokenKind::Minus: return 5;
    case TokenKind::Star:
    case TokenKind::Slash: return 6;
    default: return -1;
    }
}

bool Parser::isBinaryOperator(TokenKind kind) {
    return getOperatorPrecedence(kind) > 0;
}

ExprPtr Parser::parseExpression() {
    return parseBinaryExpr(1);
}

ExprPtr Parser::parseBinaryExpr(int minPrec) {
    RecursionGuard guard(recursionDepth_, kMaxRecursionDepth, diag_, current_.location);
    if (guard.exceeded) {
        return std::make_unique<LiteralExpr>(current_.location, "0", TokenKind::IntegerLiteral);
    }

    auto lhs = parseUnaryExpr();

    while (isBinaryOperator(current_.kind) && getOperatorPrecedence(current_.kind) >= minPrec) {
        TokenKind op = current_.kind;
        int prec = getOperatorPrecedence(op);
        SourceLocation opLoc = current_.location;
        advance();

        auto rhs = parseBinaryExpr(prec + 1);
        lhs = std::make_unique<BinaryExpr>(opLoc, op, std::move(lhs), std::move(rhs));
    }

    return lhs;
}

ExprPtr Parser::parseUnaryExpr() {
    if (check(TokenKind::Bang) || check(TokenKind::Minus)) {
        TokenKind op = current_.kind;
        SourceLocation loc = current_.location;
        advance();
        auto operand = parseUnaryExpr();
        return std::make_unique<UnaryExpr>(loc, op, std::move(operand));
    }
    return parsePrimaryExpr();
}

ExprPtr Parser::parsePrimaryExpr() {
    SourceLocation loc = current_.location;

    if (check(TokenKind::LParen)) {
        advance();
        auto expr = parseExpression();
        expect(TokenKind::RParen, "expected ')'");
        return expr;
    }

    if (check(TokenKind::IntegerLiteral)) {
        Token tok = advance();
        return std::make_unique<LiteralExpr>(loc, tok.text, TokenKind::IntegerLiteral);
    }

    if (check(TokenKind::KW_true)) {
        advance();
        return std::make_unique<LiteralExpr>(loc, "true", TokenKind::KW_true);
    }
    if (check(TokenKind::KW_false)) {
        advance();
        return std::make_unique<LiteralExpr>(loc, "false", TokenKind::KW_false);
    }

    if (check(TokenKind::StringLiteral)) {
        Token tok = advance();
        return std::make_unique<LiteralExpr>(loc, tok.text, TokenKind::StringLiteral);
    }

    if (check(TokenKind::Identifier)) {
        std::string name = parseFuncName();

        if (check(TokenKind::LParen)) {
            auto call = std::make_unique<CallExpr>(loc, name);
            advance();
            if (!check(TokenKind::RParen)) {
                call->args.push_back(parseExpression());
                while (match(TokenKind::Comma)) {
                    call->args.push_back(parseExpression());
                }
            }
            expect(TokenKind::RParen, "expected ')' after arguments");
            return call;
        }

        return std::make_unique<IdentifierExpr>(loc, name);
    }

    diag_.error(loc, "expected expression");
    return std::make_unique<LiteralExpr>(loc, "0", TokenKind::IntegerLiteral);
}

// --- C++ style: Type Name(...) [const]; → FnDecl ---

ASTNodePtr Parser::parseFnDeclCppStyle() {
    SourceLocation loc = current_.location;
    auto decl = std::make_unique<FnDecl>(loc);
    decl->isRustStyle = false;

    decl->returnType = parseType();
    decl->name = parseFuncName();

    expect(TokenKind::LParen, "expected '(' in function declaration");

    if (!check(TokenKind::RParen)) {
        do {
            Parameter param;
            param.type = parseType();
            Token paramName = expect(TokenKind::Identifier, "expected parameter name");
            param.name = paramName.text;
            decl->params.push_back(std::move(param));
        } while (match(TokenKind::Comma));
    }

    expect(TokenKind::RParen, "expected ')' in function declaration");

    if (match(TokenKind::KW_const)) {
        decl->isConst = true;
    }

    if (match(TokenKind::Assign)) {
        decl->bindingTarget = parseQualifiedNameString();
    }

    // C++ style hints: ReturnType name(...) -> cardinality(...), access(...);
    if (check(TokenKind::Arrow) && peekNext().kind == TokenKind::Identifier &&
        (peekNext().text == "cardinality" || peekNext().text == "access")) {
        advance(); // consume '->'
        parseHints(*decl);
    }

    expect(TokenKind::Semicolon, "expected ';' after function declaration");
    return decl;
}

// --- Rust style: Name(...) [const] -> Type; → FnDecl ---

ASTNodePtr Parser::parseFnDeclRustStyle() {
    SourceLocation loc = current_.location;
    auto decl = std::make_unique<FnDecl>(loc);
    decl->isRustStyle = true;

    decl->name = parseFuncName();

    expect(TokenKind::LParen, "expected '(' in function declaration");

    if (!check(TokenKind::RParen)) {
        do {
            Parameter param;
            param.type = parseType();
            Token paramName = expect(TokenKind::Identifier, "expected parameter name");
            param.name = paramName.text;
            decl->params.push_back(std::move(param));
        } while (match(TokenKind::Comma));
    }

    expect(TokenKind::RParen, "expected ')' in function declaration");

    if (match(TokenKind::KW_const)) {
        decl->isConst = true;
    }

    expect(TokenKind::Arrow, "expected '->' for return type");

    // Void + hints: name() -> cardinality(...);
    if (check(TokenKind::Identifier) && (peek().text == "cardinality" || peek().text == "access")) {
        // No return type — void with hints
        parseHints(*decl);
    } else if (check(TokenKind::LParen)) {
        decl->isMultiReturn = true;
        decl->returnParams = parseMultiReturnParams();
    } else {
        decl->returnType = parseType();
    }

    // Rust-style hints after return type: name() -> Type, cardinality(...);
    if (check(TokenKind::Comma) && peekNext().kind == TokenKind::Identifier &&
        (peekNext().text == "cardinality" || peekNext().text == "access")) {
        advance(); // consume ','
        parseHints(*decl);
    }

    // Optional selective-return clause: `with returns(a, _)`.  Only valid
    // after a multi-return signature.  Parse unconditionally so callers
    // see a useful diagnostic when the clause appears on a single-return fn.
    if (check(TokenKind::KW_with)) {
        if (!decl->isMultiReturn) {
            diag_.error(current_.location,
                        "'with returns(...)' clause is only valid on multi-return functions");
        }
        SourceLocation clauseLoc = current_.location;
        size_t itemCount = 0;
        parseUsedReturnsClause(decl->declaredUsedReturns, decl->hasUsedReturnsClause, itemCount);
        if (decl->isMultiReturn && itemCount > decl->returnParams.size()) {
            diag_.error(clauseLoc,
                        "'with returns(...)' lists " + std::to_string(itemCount) +
                            " items but function declares only " +
                            std::to_string(decl->returnParams.size()) + " return parameters");
        }
    }

    if (match(TokenKind::Assign)) {
        decl->bindingTarget = parseQualifiedNameString();
    }

    expect(TokenKind::Semicolon, "expected ';' after function declaration");

    return decl;
}

bool Parser::consumeCloseAngle(const std::string& msg) {
    if (check(TokenKind::RAngle)) {
        advance();
        return true;
    }
    if (check(TokenKind::ShiftRight)) {
        // Split `>>` into two `>`. The first closes this level; the second
        // becomes the current token so the enclosing close consumes it.
        Token synthetic = current_;
        synthetic.kind = TokenKind::RAngle;
        synthetic.text = ">";
        synthetic.location.column += 1;
        current_ = synthetic;
        return true;
    }
    diag_.error(current_.location, msg);
    return false;
}

// --- Record field-list parsing ---

// Grammar (entered with the `record` keyword already consumed):
//   "<" [ field { "," field } ] ">"
//   field := Identifier ":" Type
// An empty list (`record<>`) is syntactically accepted here; Sema decides
// whether an empty record is meaningful. Field names accept Identifier; a
// field may legitimately reuse a stdlib keyword spelling as its name only
// where unambiguous, but the contract is "Identifier", matching how user
// struct fields are named everywhere else.
// Shared by record<...> and union<...>: identical surface grammar
//   "<" [ field { "," field } ] ">"   where field := Identifier ":" Type
// An empty list is syntactically accepted here; Sema decides whether an
// empty record / a variant-less union is meaningful. Diagnostics stay
// form-neutral so they read correctly for both.
void Parser::parseRecordFields(TypeNode& outType) {
    if (!expect(TokenKind::LAngle, "expected '<' after composite stdlib type")
             .is(TokenKind::LAngle)) {
        return;
    }
    if (check(TokenKind::RAngle)) {
        advance(); // empty <>
        return;
    }
    do {
        TypeNode::RecordField field;
        field.location = current_.location;
        Token nameTok = expect(TokenKind::Identifier, "expected field name in named-field list");
        field.name = nameTok.text;
        expect(TokenKind::Colon, "expected ':' after field name");
        field.typeBox.push_back(parseType());
        outType.recordFields.push_back(std::move(field));
    } while (match(TokenKind::Comma));
    consumeCloseAngle("expected '>' to close named-field list");
}

// --- Type parsing ---

TypeNode Parser::parseType() {
    RecursionGuard guard(recursionDepth_, kMaxRecursionDepth, diag_, current_.location);
    if (guard.exceeded) {
        TypeNode dummy;
        dummy.location = current_.location;
        dummy.nameParts.push_back("error");
        return dummy;
    }

    TypeNode type;
    type.location = current_.location;

    if (match(TokenKind::KW_const)) {
        type.isConst = true;
    }

    if (check(TokenKind::KW_owned)) {
        type.ownership = OwnershipKind::Owned;
        advance();
    } else if (check(TokenKind::KW_shared)) {
        type.ownership = OwnershipKind::Shared;
        advance();
    } else if (check(TokenKind::KW_weak)) {
        type.ownership = OwnershipKind::Weak;
        advance();
    }

    if (check(TokenKind::KW_void)) {
        type.nameParts.push_back(advance().text);
    } else if (check(TokenKind::KW_record) || check(TokenKind::KW_union)) {
        // Composite stdlib types: `record<name: Type, ...>` and
        // `union<tag: TagT, v1: T1, ...>`. Unlike the generic stdlib types
        // (optional/slice) the angle-bracket payload is a named-field list,
        // not positional template arguments, so both share a dedicated parse
        // path that fills `recordFields`. The generic `<...>` handler below
        // is intentionally skipped for them. The record/union distinction
        // (first field is a discriminant tag for union) is a Sema concern;
        // the surface grammar is identical.
        type.stdlibId = stdlibTokenTypeId(current_.kind);
        type.nameParts.push_back(advance().text); // consume 'record' / 'union'
        parseRecordFields(type);
        // Trailing ref/ptr/variadic markers still apply to the aggregate.
        if (match(TokenKind::Amp)) {
            type.modifier = TypeNode::Ref;
        } else if (match(TokenKind::Star)) {
            type.modifier = TypeNode::Ptr;
        }
        if (match(TokenKind::Ellipsis)) {
            type.isVariadic = true;
        }
        return type;
    } else if (isStdlibTypeToken(current_.kind)) {
        // Stdlib keywords are recognized as type names in type
        // position. Always single-part; never followed by `::`.
        type.stdlibId = stdlibTokenTypeId(current_.kind);
        type.nameParts.push_back(advance().text);
    } else if (check(TokenKind::Identifier) || check(TokenKind::KW_typename)) {
        type.nameParts = parseQualifiedName();
    } else {
        diag_.error(current_.location, "expected type name");
        return type;
    }

    if (check(TokenKind::LAngle) && !type.nameParts.empty()) {
        const auto& firstName = type.nameParts[0];
        // Built-in scalar names that never take template arguments. Stdlib
        // generic types (optional, slice) DO consume their `<...>` here;
        // arity is enforced later by Sema.
        bool isBuiltinKeyword = (firstName == "void" || firstName == "int" || firstName == "bool" ||
                                 firstName == "i64" || firstName == "f64" || firstName == "string");
        if (!isBuiltinKeyword) {
            advance(); // consume '<'
            if (!check(TokenKind::RAngle)) {
                auto parseTemplateArg = [this]() -> TypeNode {
                    if (check(TokenKind::IntegerLiteral)) {
                        TypeNode arg;
                        arg.location = current_.location;
                        Token lit = advance();
                        arg.nameParts.push_back(lit.text);
                        // Same guard: an unparseable Integer-kind token text
                        // must not throw out of this lambda. Fall back to 0
                        // and emit a diagnostic so the caller (sema) still
                        // sees the structural template-arg shape.
                        int parsed = 0;
                        if (!tryParseNonNegativeInt(lit.text, parsed)) {
                            diag_.error(lit.location,
                                        "invalid non-type template argument '" + lit.text + "'");
                        }
                        arg.nonTypeValue = parsed;
                        return arg;
                    }
                    TypeNode arg = parseType();
                    if (check(TokenKind::DotDot) && !arg.nameParts.empty()) {
                        advance();
                        std::string rangeName = arg.nameParts.back();
                        if (check(TokenKind::Identifier)) {
                            rangeName += ".." + advance().text;
                        } else {
                            rangeName += "..";
                        }
                        arg.nameParts.back() = rangeName;
                    }
                    return arg;
                };
                type.templateArgs.push_back(parseTemplateArg());
                while (match(TokenKind::Comma)) {
                    type.templateArgs.push_back(parseTemplateArg());
                }
            }
            consumeCloseAngle("expected '>' after template arguments");
        }
    }

    if (match(TokenKind::Amp)) {
        type.modifier = TypeNode::Ref;
    } else if (match(TokenKind::Star)) {
        type.modifier = TypeNode::Ptr;
    }

    if (match(TokenKind::Ellipsis)) {
        type.isVariadic = true;
    }

    return type;
}

std::vector<std::string> Parser::parseQualifiedName() {
    std::vector<std::string> parts;
    Token first = advance();
    parts.push_back(first.text);

    while (check(TokenKind::ColonColon)) {
        advance();
        // Accept identifiers and a small set of keywords whose source text
        // can legitimately appear inside a qualified name like
        // `std::cpp17::bool`, `std::python::int`, etc. Stdlib keywords are
        // included so existing backend-namespace fixtures keep working.
        if (check(TokenKind::Identifier) || check(TokenKind::KW_void) || check(TokenKind::KW_import) ||
            isStdlibTypeToken(current_.kind)) {
            parts.push_back(advance().text);
        } else {
            Token part = expect(TokenKind::Identifier, "expected identifier after '::'");
            parts.push_back(part.text);
        }
    }
    return parts;
}

std::string Parser::parseFuncName() {
    Token first = expect(TokenKind::Identifier, "expected function name");
    std::string name = first.text;

    while (check(TokenKind::Dot) && peekNext().is(TokenKind::Identifier)) {
        advance();
        name += ".";
        name += advance().text;
    }

    return name;
}

std::string Parser::parseQualifiedNameString() {
    auto parts = parseQualifiedName();
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += "::";
        result += parts[i];
    }
    return result;
}

std::string Parser::parseQualifiedFuncRef() {
    Token first = expect(TokenKind::Identifier, "expected function name");
    std::string name = first.text;
    while (check(TokenKind::ColonColon) && peekNext().is(TokenKind::Identifier)) {
        advance();
        name += "::";
        name += advance().text;
    }
    return name;
}

// --- Multi-return params: (Type name, Type name, ...) ---

std::vector<ReturnParam> Parser::parseMultiReturnParams() {
    std::vector<ReturnParam> params;
    expect(TokenKind::LParen, "expected '(' for multi-return parameters");

    if (!check(TokenKind::RParen)) {
        do {
            ReturnParam rp;
            rp.loc = current_.location;
            rp.type = parseType();
            Token nameTok = expect(TokenKind::Identifier, "expected return parameter name");
            rp.name = nameTok.text;
            params.push_back(std::move(rp));
        } while (match(TokenKind::Comma));
    }

    expect(TokenKind::RParen, "expected ')' after multi-return parameters");
    return params;
}

// --- `with returns(a, _, ...)` selective-return clause ---
// Parsed after the `-> (...)` multi-return signature.  Names are
// validated against the callee's return-param list by SemanticAnalyzer.
// An empty list (`with returns()`) is rejected — callers who want to
// keep every field should simply omit the clause.  `_` wildcards are
// skipped (treated as "this position may be elided"); the remaining
// names are returned to the caller.  `hasUsedReturnsClause` is set
// whenever the clause was syntactically present.
void Parser::parseUsedReturnsClause(std::vector<std::string>& outNames, bool& outHasClause, size_t& outItemCount) {
    // Consume `with`.
    SourceLocation withLoc = current_.location;
    advance();
    outHasClause = true;
    outItemCount = 0;

    // Next must be the contextual keyword `returns` (lexed as Identifier).
    if (!(check(TokenKind::Identifier) && current_.text == "returns")) {
        diag_.error(current_.location, "expected 'returns' after 'with'");
        return;
    }
    advance(); // consume `returns`

    expect(TokenKind::LParen, "expected '(' after 'with returns'");

    std::set<std::string> seenInClause;
    if (!check(TokenKind::RParen)) {
        do {
            if (!check(TokenKind::Identifier)) {
                diag_.error(current_.location, "expected return parameter name or '_' in 'with returns(...)'");
                break;
            }
            Token tok = advance();
            ++outItemCount;
            if (tok.text == "_") {
                // Wildcard position — do not record a name.
                continue;
            }
            if (!seenInClause.insert(tok.text).second) {
                diag_.error(tok.location,
                            "'with returns(...)' lists '" + tok.text + "' more than once");
            }
            outNames.push_back(tok.text);
        } while (match(TokenKind::Comma));
    }

    expect(TokenKind::RParen, "expected ')' after 'with returns(...)'");

    if (outItemCount == 0) {
        diag_.error(withLoc,
                    "'with returns()' must name at least one return parameter "
                    "or '_'; omit the clause entirely if all fields are observable");
    }
}

// --- Return binding: -> name or -> (a, b, _) ---

ReturnBinding Parser::parseReturnBinding() {
    ReturnBinding binding;
    binding.loc = current_.location;

    if (check(TokenKind::LParen)) {
        advance();
        binding.isSingleValue = false;

        if (!check(TokenKind::RParen)) {
            do {
                ReturnBinding::Target target;
                Token tok = expect(TokenKind::Identifier, "expected identifier in return binding");
                target.name = tok.text;
                target.isDiscard = (tok.text == "_");
                binding.targets.push_back(std::move(target));
            } while (match(TokenKind::Comma));
        }

        expect(TokenKind::RParen, "expected ')' after return binding");
    } else {
        binding.isSingleValue = true;
        Token tok = expect(TokenKind::Identifier, "expected identifier after '->' in return binding");
        binding.singleName = tok.text;
    }

    return binding;
}

// --- Pipeline edges ---

void Parser::parsePipelineEdges(std::vector<PipelineEdge>& edges) {
    while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
        auto edgeLoc = current_.location;
        if (check(TokenKind::KW_stage)) {
            diag_.error(current_.location,
                        "explicit stage<N> is incompatible with pipeline mode; "
                        "stages are inferred from the DAG topology");
            synchronize();
            continue;
        }

        PipelineEdge edge;
        edge.loc = current_.location;

        {
            Token sourceTok = expect(TokenKind::Identifier, "expected function name in pipeline edge");
            edge.source = sourceTok.text;
            while (check(TokenKind::ColonColon) && peekNext().is(TokenKind::Identifier)) {
                advance();
                edge.source += "::";
                edge.source += advance().text;
            }
        }

        expect(TokenKind::Arrow, "expected '->' in pipeline edge");

        if (check(TokenKind::KW_void)) {
            edge.isTerminal = true;
            edge.terminalType = current_.text;
            advance();
        } else if (check(TokenKind::Identifier)) {
            std::string targetName = advance().text;
            while (check(TokenKind::ColonColon) && peekNext().is(TokenKind::Identifier)) {
                advance();
                targetName += "::";
                targetName += advance().text;
            }
            edge.target = targetName;
        } else {
            diag_.error(current_.location, "expected function name or type after '->' in pipeline edge");
            synchronize();
            continue;
        }

        expect(TokenKind::Semicolon, "expected ';' after pipeline edge");
        edges.push_back(std::move(edge));
        if (current_.location == edgeLoc) {
            advance();
        }
    }
}

// --- Template declaration ---

std::vector<TemplateParamDecl> Parser::parseTemplateParamList() {
    std::vector<TemplateParamDecl> params;

    expect(TokenKind::LAngle, "expected '<' after 'template'");

    if (check(TokenKind::RAngle)) {
        advance();
        return params;
    }

    do {
        TemplateParamDecl param;
        param.location = current_.location;

        if (check(TokenKind::KW_typename)) {
            advance();
            param.kind = TemplateParamDecl::TypeParam;

            if (check(TokenKind::Ellipsis)) {
                advance();
                param.isVariadic = true;
            }

            Token name = expect(TokenKind::Identifier, "expected template parameter name");
            param.name = name.text;

            if (match(TokenKind::Colon)) {
                param.constraintType = parseType();
            }

            if (match(TokenKind::Assign)) {
                param.defaultType = parseType();
            }

        } else if (isTypeStart()) {
            param.kind = TemplateParamDecl::NonTypeParam;
            param.constraintType = parseType();

            Token name = expect(TokenKind::Identifier, "expected non-type template parameter name");
            param.name = name.text;

            if (match(TokenKind::Assign)) {
                Token defVal = expect(TokenKind::IntegerLiteral,
                                      "expected default value for non-type "
                                      "template parameter");
                TypeNode defType;
                defType.nameParts.push_back(defVal.text);
                // Same guard as the other numeric-token sites — a malformed
                // Integer-kind token text must not throw an uncaught
                // std::invalid_argument. 0 fallback + diagnostic.
                int parsed = 0;
                if (!tryParseNonNegativeInt(defVal.text, parsed)) {
                    diag_.error(defVal.location,
                                "invalid non-type template parameter default '" + defVal.text + "'");
                }
                defType.nonTypeValue = parsed;
                param.defaultType = std::move(defType);
            }
        } else if (check(TokenKind::KW_template)) {
            param.kind = TemplateParamDecl::TemplateTemplateParam;
            advance();

            param.innerParams = [&]() {
                std::vector<TemplateParamDecl> inner;
                expect(TokenKind::LAngle, "expected '<' in template template parameter");
                do {
                    TemplateParamDecl ip;
                    ip.location = current_.location;
                    if (check(TokenKind::KW_typename)) {
                        advance();
                        ip.kind = TemplateParamDecl::TypeParam;
                        if (check(TokenKind::Identifier)) {
                            ip.name = advance().text;
                        }
                    }
                    inner.push_back(std::move(ip));
                } while (match(TokenKind::Comma));
                expect(TokenKind::RAngle, "expected '>' in template template parameter");
                return inner;
            }();

            expect(TokenKind::KW_class, "expected 'class' after template template parameter list");

            Token name = expect(TokenKind::Identifier, "expected template template parameter name");
            param.name = name.text;
        } else {
            diag_.error(current_.location, "expected 'typename', type, or 'template' in template parameter list");
            synchronize();
            break;
        }

        params.push_back(std::move(param));
    } while (match(TokenKind::Comma));

    expect(TokenKind::RAngle, "expected '>' to close template parameter list");
    return params;
}

ASTNodePtr Parser::parseTemplateDecl() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_template, "expected 'template'");

    auto templateParams = parseTemplateParamList();

    // type or class keyword → TypeDecl
    if (check(TokenKind::KW_type) || check(TokenKind::KW_class)) {
        if (check(TokenKind::KW_class)) {
            diag_.warning(current_.location, "'class' is deprecated, use 'type' instead");
        }
        auto typeDecl = parseTypeDecl();
        if (typeDecl && typeDecl->kind == ASTKind::TypeDecl) {
            auto& td = static_cast<TypeDecl&>(*typeDecl);
            td.templateParams = std::move(templateParams);

            if (!td.specializationArgs.empty()) {
                if (td.templateParams.empty()) {
                    td.isSpecialization = true;
                } else {
                    td.isPartialSpecialization = true;
                }
            }
        }
        return typeDecl;
    }

    if (check(TokenKind::KW_fn)) {
        // Check if it's fn operator or fn logic block vs fn decl after template
        // For templates, fn always starts a logic block
        auto fnBlock = parseFnLogicBlock();
        if (fnBlock && fnBlock->kind == ASTKind::FnLogicBlock) {
            static_cast<FnLogicBlock&>(*fnBlock).templateParams = std::move(templateParams);
        }
        return fnBlock;
    }

    // template<...> ReturnType name(...); — C++ style function decl
    // template<...> name(...) -> Type;    — Rust style function decl

    if (check(TokenKind::KW_void) || check(TokenKind::KW_const)) {
        auto funcDecl = parseFnDeclCppStyle();
        if (funcDecl && funcDecl->kind == ASTKind::FnDecl) {
            static_cast<FnDecl&>(*funcDecl).templateParams = std::move(templateParams);
        }
        return funcDecl;
    }

    if (check(TokenKind::Identifier)) {
        const Token& next = peekNext();
        if (next.is(TokenKind::LParen) || next.is(TokenKind::Dot)) {
            auto funcDecl = parseFnDeclRustStyle();
            if (funcDecl && funcDecl->kind == ASTKind::FnDecl) {
                static_cast<FnDecl&>(*funcDecl).templateParams = std::move(templateParams);
            }
            return funcDecl;
        }
        auto funcDecl = parseFnDeclCppStyle();
        if (funcDecl && funcDecl->kind == ASTKind::FnDecl) {
            static_cast<FnDecl&>(*funcDecl).templateParams = std::move(templateParams);
        }
        return funcDecl;
    }

    diag_.error(current_.location, "expected function or type declaration after template<...>");
    synchronize();
    return nullptr;
}

// --- TypeDecl: type/class Name [: public Base] { sections } ---

ASTNodePtr Parser::parseTypeDecl() {
    SourceLocation loc = current_.location;
    // Accept both 'type' and 'class' (class is deprecated)
    if (check(TokenKind::KW_type)) {
        advance();
    } else if (check(TokenKind::KW_class)) {
        advance();
    } else {
        diag_.error(current_.location, "expected 'type' or 'class'");
        synchronize();
        return nullptr;
    }

    Token nameTok = expect(TokenKind::Identifier, "expected type name");
    auto typeDecl = std::make_unique<TypeDecl>(loc, nameTok.text);

    // Optional specialization arguments
    if (check(TokenKind::LAngle)) {
        advance();
        while (!check(TokenKind::RAngle) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
            auto innerLoc = current_.location;
            typeDecl->specializationArgs.push_back(parseType());
            if (!check(TokenKind::RAngle)) {
                expect(TokenKind::Comma, "expected ',' or '>' in specialization argument list");
            }
            if (current_.location == innerLoc) advance();
        }
        expect(TokenKind::RAngle, "expected '>' after specialization arguments");
    }

    // Optional inheritance: : public Base
    if (match(TokenKind::Colon)) {
        if (!check(TokenKind::KW_public)) {
            diag_.error(current_.location, "expected 'public' in inheritance declaration");
            synchronize();
            return typeDecl;
        }
        advance();
        typeDecl->baseClass = parseType();
    }

    expect(TokenKind::LCurly, "expected '{' after type declaration");

    while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
        auto outerLoc = current_.location;
        if (isVisibilityKeyword(current_.kind)) {
            SourceLocation secLoc = current_.location;

            Visibility vis;
            if (check(TokenKind::KW_public))
                vis = Visibility::Public;
            else if (check(TokenKind::KW_protected))
                vis = Visibility::Protected;
            else if (check(TokenKind::KW_internal))
                vis = Visibility::Internal;
            else
                vis = Visibility::Private;
            advance();

            expect(TokenKind::Colon, "expected ':' after visibility specifier");

            auto section = std::make_unique<VisibilitySection>(secLoc, vis);

            PriorityLevel classPriority = PriorityLevel::Normal;

            while (!check(TokenKind::Eof) && !check(TokenKind::RCurly) && !isVisibilityKeyword(current_.kind) &&
                   !diag_.reachedLimit()) {
                auto innerLoc = current_.location;
                if (check(TokenKind::KW_priority)) {
                    classPriority = parsePriorityLevel();
                    continue;
                }
                auto member = parseTypeMember(nameTok.text);
                if (member) {
                    if (member->kind == ASTKind::FnDecl) {
                        static_cast<FnDecl&>(*member).priority = classPriority;
                    }
                    section->members.push_back(std::move(member));
                }
                if (current_.location == innerLoc) {
                    advance();
                }
            }

            typeDecl->sections.push_back(std::move(section));
        } else {
            diag_.error(current_.location,
                        "expected visibility specifier (public, protected, "
                        "private) in type body");
            synchronize();
        }
        if (current_.location == outerLoc) {
            advance();
        }
    }

    expect(TokenKind::RCurly, "expected '}' to close type");
    return typeDecl;
}

// --- Type member parsing (was parseClassMember) ---

ASTNodePtr Parser::parseTypeMember(const std::string& typeName) {
    SourceLocation loc = current_.location;

    // virtual keyword → prohibition error
    if (check(TokenKind::KW_virtual)) {
        diag_.error(current_.location, "'virtual' is prohibited in Topo (C002)");
        synchronize();
        return nullptr;
    }

    // Destructor: ~TypeName();
    if (check(TokenKind::Tilde)) {
        advance();
        Token dtorName = expect(TokenKind::Identifier, "expected type name after '~'");
        if (dtorName.text != typeName) {
            diag_.error(dtorName.location,
                        "destructor name '~" + dtorName.text + "' does not match type name '" + typeName + "'");
        }
        expect(TokenKind::LParen, "expected '(' after destructor name");
        expect(TokenKind::RParen, "expected ')' in destructor declaration");
        expect(TokenKind::Semicolon, "expected ';' after destructor declaration");

        auto decl = std::make_unique<FnDecl>(loc);
        decl->isDestructor = true;
        decl->className = typeName;
        decl->name = "~" + typeName;
        return decl;
    }

    // explicit constructor: explicit TypeName(...);
    if (check(TokenKind::KW_explicit)) {
        advance();
        Token ctorName = expect(TokenKind::Identifier, "expected constructor name after 'explicit'");
        if (ctorName.text != typeName) {
            diag_.error(
                ctorName.location,
                "explicit constructor name '" + ctorName.text + "' does not match type name '" + typeName + "'");
        }

        auto decl = std::make_unique<FnDecl>(loc);
        decl->isConstructor = true;
        decl->isExplicit = true;
        decl->className = typeName;
        decl->name = typeName;

        expect(TokenKind::LParen, "expected '(' in constructor declaration");
        if (!check(TokenKind::RParen)) {
            do {
                Parameter param;
                param.type = parseType();
                Token paramName = expect(TokenKind::Identifier, "expected parameter name");
                param.name = paramName.text;
                decl->params.push_back(std::move(param));
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "expected ')' in constructor declaration");

        if (check(TokenKind::LCurly)) {
            diag_.error(current_.location, "member function bodies are prohibited in Topo (C001)");
            int depth = 1;
            advance();
            while (!check(TokenKind::Eof) && depth > 0) {
                if (check(TokenKind::LCurly)) depth++;
                if (check(TokenKind::RCurly)) depth--;
                advance();
            }
            return decl;
        }

        expect(TokenKind::Semicolon, "expected ';' after constructor declaration");
        return decl;
    }

    // static member: static ...
    bool isStatic = false;
    if (check(TokenKind::KW_static)) {
        isStatic = true;
        advance();
    }

    // Operator overload: fn operator+(Type, Type) -> Type;
    if (check(TokenKind::KW_fn) && peekNext().is(TokenKind::KW_operator)) {
        auto result = parseOperatorDecl();
        if (result && isStatic) {
            static_cast<FnDecl&>(*result).isStatic = true;
        }
        return result;
    }

    // Constructor (non-explicit): TypeName(...);
    if (check(TokenKind::Identifier) && current_.text == typeName && peekNext().is(TokenKind::LParen)) {
        if (isStatic) {
            diag_.error(loc, "constructors cannot be declared static");
        }
        advance();

        auto decl = std::make_unique<FnDecl>(loc);
        decl->isConstructor = true;
        decl->className = typeName;
        decl->name = typeName;

        expect(TokenKind::LParen, "expected '(' in constructor declaration");
        if (!check(TokenKind::RParen)) {
            do {
                Parameter param;
                param.type = parseType();
                Token paramName = expect(TokenKind::Identifier, "expected parameter name");
                param.name = paramName.text;
                decl->params.push_back(std::move(param));
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "expected ')' in constructor declaration");

        if (check(TokenKind::LCurly)) {
            diag_.error(current_.location, "member function bodies are prohibited in Topo (C001)");
            int depth = 1;
            advance();
            while (!check(TokenKind::Eof) && depth > 0) {
                if (check(TokenKind::LCurly)) depth++;
                if (check(TokenKind::RCurly)) depth--;
                advance();
            }
            return decl;
        }

        expect(TokenKind::Semicolon, "expected ';' after constructor declaration");
        return decl;
    }

    // using -> DataDecl (type alias inside type)
    if (check(TokenKind::KW_using)) {
        return parseTypeAliasAsDataDecl();
    }

    // Remaining: member function or member variable
    if (!isTypeStart()) {
        diag_.error(current_.location, "expected type member declaration");
        synchronize();
        return nullptr;
    }

    TypeNode type = parseType();

    if (!check(TokenKind::Identifier)) {
        diag_.error(current_.location, "expected member name after type");
        synchronize();
        return nullptr;
    }

    Token memberName = advance();

    // Member function: Type name ( ... ) [const] ;
    if (check(TokenKind::LParen)) {
        auto decl = std::make_unique<FnDecl>(loc);
        decl->returnType = std::move(type);
        decl->name = memberName.text;
        decl->isStatic = isStatic;
        decl->isRustStyle = false;

        advance(); // consume '('

        if (!check(TokenKind::RParen)) {
            do {
                Parameter param;
                param.type = parseType();
                Token paramName = expect(TokenKind::Identifier, "expected parameter name");
                param.name = paramName.text;
                decl->params.push_back(std::move(param));
            } while (match(TokenKind::Comma));
        }

        expect(TokenKind::RParen, "expected ')' in member function declaration");

        if (match(TokenKind::KW_const)) {
            decl->isConst = true;
        }

        if (check(TokenKind::LCurly)) {
            diag_.error(current_.location, "member function bodies are prohibited in Topo (C001)");
            int depth = 1;
            advance();
            while (!check(TokenKind::Eof) && depth > 0) {
                if (check(TokenKind::LCurly)) depth++;
                if (check(TokenKind::RCurly)) depth--;
                advance();
            }
            return decl;
        }

        expect(TokenKind::Semicolon, "expected ';' after member function declaration");
        return decl;
    }

    // Member variable: Type name ;
    if (check(TokenKind::Assign)) {
        diag_.error(current_.location, "member variable initializers are prohibited in Topo (C004)");
        synchronize();
        auto var = std::make_unique<DataDecl>(loc);
        var->type = std::move(type);
        var->name = memberName.text;
        if (isStatic) {
            var->modifiers.push_back({ModifierData::Static, loc, {}});
        }
        return var;
    }

    expect(TokenKind::Semicolon, "expected ';' after member variable declaration");

    auto var = std::make_unique<DataDecl>(loc);
    var->type = std::move(type);
    var->name = memberName.text;
    if (isStatic) {
        var->modifiers.push_back({ModifierData::Static, loc, {}});
    }
    return var;
}

// --- OperatorDecl: fn operator+(Type, Type) -> Type [= binding]; → FnDecl ---

ASTNodePtr Parser::parseOperatorDecl() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_fn, "expected 'fn'");
    expect(TokenKind::KW_operator, "expected 'operator'");

    OverloadableOp op;
    bool recognized = true;
    switch (current_.kind) {
    case TokenKind::Plus: op = OverloadableOp::Plus; break;
    case TokenKind::Minus: op = OverloadableOp::Minus; break;
    case TokenKind::Star: op = OverloadableOp::Star; break;
    case TokenKind::Slash: op = OverloadableOp::Slash; break;
    case TokenKind::Percent: op = OverloadableOp::Percent; break;
    case TokenKind::EqEq: op = OverloadableOp::EqEq; break;
    case TokenKind::NotEq: op = OverloadableOp::NotEq; break;
    case TokenKind::LAngle: op = OverloadableOp::Less; break;
    case TokenKind::RAngle: op = OverloadableOp::Greater; break;
    case TokenKind::LessEq: op = OverloadableOp::LessEq; break;
    case TokenKind::GreaterEq: op = OverloadableOp::GreaterEq; break;
    case TokenKind::Amp: op = OverloadableOp::Amp; break;
    case TokenKind::Pipe: op = OverloadableOp::Pipe; break;
    case TokenKind::Caret: op = OverloadableOp::Caret; break;
    case TokenKind::Tilde: op = OverloadableOp::Tilde; break;
    case TokenKind::ShiftLeft: op = OverloadableOp::ShiftLeft; break;
    case TokenKind::ShiftRight: op = OverloadableOp::ShiftRight; break;
    case TokenKind::AmpAmp: op = OverloadableOp::AmpAmp; break;
    case TokenKind::PipePipe: op = OverloadableOp::PipePipe; break;
    case TokenKind::Bang: op = OverloadableOp::Bang; break;
    case TokenKind::FatArrow: op = OverloadableOp::FatArrow; break;
    default:
        diag_.error(current_.location, "expected overloadable operator after 'operator'");
        recognized = false;
        op = OverloadableOp::Plus;
        break;
    }
    if (recognized) advance();

    auto decl = std::make_unique<FnDecl>(loc);
    decl->operatorOp = op;
    decl->name = std::string("operator") + overloadableOpName(op);
    decl->isRustStyle = true;

    expect(TokenKind::LParen, "expected '(' in operator declaration");
    if (!check(TokenKind::RParen)) {
        do {
            Parameter param;
            param.type = parseType();
            Token paramName = expect(TokenKind::Identifier, "expected parameter name");
            param.name = paramName.text;
            decl->params.push_back(std::move(param));
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "expected ')' in operator declaration");

    expect(TokenKind::Arrow, "expected '->' for return type");
    decl->returnType = parseType();

    if (match(TokenKind::Assign)) {
        decl->bindingTarget = parseQualifiedNameString();
    }

    expect(TokenKind::Semicolon, "expected ';' after operator declaration");
    return decl;
}

// --- ConstraintDecl → TypeDecl with Constraint modifier ---

ASTNodePtr Parser::parseConstraintAsTypeDecl() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_constraint, "expected 'constraint'");

    Token nameTok = expect(TokenKind::Identifier, "expected constraint name");
    auto decl = std::make_unique<TypeDecl>(loc, nameTok.text);
    decl->modifiers.push_back({ModifierData::Constraint, loc, {}});

    // Optional parent constraint
    if (match(TokenKind::Colon)) {
        Token parentTok = expect(TokenKind::Identifier, "expected parent constraint name");
        decl->parentConstraint = parentTok.text;
    }

    expect(TokenKind::LCurly, "expected '{' after constraint declaration");

    while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
        auto outerLoc = current_.location;
        ConstraintMember member;
        member.location = current_.location;
        member.type = parseType();

        Token memberName = expect(TokenKind::Identifier, "expected member name in constraint");
        member.name = memberName.text;

        if (match(TokenKind::LParen)) {
            member.isFunction = true;
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
                auto innerLoc = current_.location;
                Parameter param;
                param.type = parseType();
                Token pname = expect(TokenKind::Identifier, "expected parameter name");
                param.name = pname.text;
                member.params.push_back(std::move(param));
                if (!check(TokenKind::RParen)) {
                    expect(TokenKind::Comma, "expected ',' or ')' in parameter list");
                }
                if (current_.location == innerLoc) advance();
            }
            expect(TokenKind::RParen, "expected ')' after parameter list");
        } else {
            member.isFunction = false;
        }

        expect(TokenKind::Semicolon, "expected ';' after constraint member");
        decl->constraintMembers.push_back(std::move(member));
        if (current_.location == outerLoc) advance();
    }

    expect(TokenKind::RCurly, "expected '}' after constraint body");
    return decl;
}

// --- AdaptDecl → TypeDecl with Adapt modifier ---

ASTNodePtr Parser::parseAdaptAsTypeDecl() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_adapt, "expected 'adapt'");

    Token constraintTok = expect(TokenKind::Identifier, "expected constraint name after 'adapt'");

    expect(TokenKind::KW_for, "expected 'for' after constraint name in adapt");
    TypeNode targetType = parseType();

    // Use constraint name as the TypeDecl name
    auto decl = std::make_unique<TypeDecl>(loc, constraintTok.text);
    decl->modifiers.push_back({ModifierData::Adapt, loc, {constraintTok.text}});
    decl->adaptConstraintName = constraintTok.text;
    decl->adaptTargetType = std::move(targetType);

    expect(TokenKind::LCurly, "expected '{' after adapt declaration");

    while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
        auto innerLoc = current_.location;
        AdaptMapping mapping;
        mapping.location = current_.location;

        Token memberTok = expect(TokenKind::Identifier, "expected member name in adapt mapping");
        mapping.memberName = memberTok.text;

        expect(TokenKind::Assign, "expected '=' in adapt mapping");

        Token targetTok = expect(TokenKind::Identifier, "expected target name in adapt mapping");
        mapping.targetName = targetTok.text;

        expect(TokenKind::Semicolon, "expected ';' after adapt mapping");
        decl->adaptMappings.push_back(std::move(mapping));
        if (current_.location == innerLoc) advance();
    }

    expect(TokenKind::RCurly, "expected '}' after adapt body");
    return decl;
}

// --- InstantiateDecl → DataDecl with Instantiate modifier ---

ASTNodePtr Parser::parseInstantiateAsDataDecl() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_instantiate, "expected 'instantiate'");

    TypeNode type = parseType();

    expect(TokenKind::Semicolon, "expected ';' after instantiate declaration");

    auto decl = std::make_unique<DataDecl>(loc);
    decl->modifiers.push_back({ModifierData::Instantiate, loc, {}});
    decl->type = std::move(type);
    return decl;
}

// --- ComptimeDecl: handled by parseMemberDecl with comptime modifier ---
// Left as standalone for backward compatibility from parseMemberDecl
ASTNodePtr Parser::parseComptimeDecl() {
    // This is now handled directly in parseMemberDecl via collectModifiers
    // but kept for any remaining callers
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_comptime, "expected 'comptime'");

    if (check(TokenKind::KW_if)) {
        advance();
        auto node = std::make_unique<IfDecl>(loc);
        node->modifiers.push_back({ModifierData::Comptime, loc, {}});

        expect(TokenKind::LParen, "expected '(' after 'comptime if'");
        node->condition = parseExpression();
        expect(TokenKind::RParen, "expected ')' after condition");

        expect(TokenKind::LCurly, "expected '{' after comptime if condition");
        while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
            auto innerLoc = current_.location;
            auto member = parseMemberDecl();
            if (member) node->thenBody.push_back(std::move(member));
            if (current_.location == innerLoc) advance();
        }
        expect(TokenKind::RCurly, "expected '}' after comptime if body");

        if (match(TokenKind::KW_else)) {
            expect(TokenKind::LCurly, "expected '{' after 'else'");
            while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
                auto innerLoc = current_.location;
                auto member = parseMemberDecl();
                if (member) node->elseBody.push_back(std::move(member));
                if (current_.location == innerLoc) advance();
            }
            expect(TokenKind::RCurly, "expected '}' after else body");
        }

        return node;
    }

    if (check(TokenKind::KW_fn)) {
        advance();

        Token nameTok = expect(TokenKind::Identifier, "expected function name after 'comptime fn'");
        auto node = std::make_unique<FnDecl>(loc);
        node->modifiers.push_back({ModifierData::Comptime, loc, {}});
        node->name = nameTok.text;
        node->isRustStyle = true;

        expect(TokenKind::LParen, "expected '(' after comptime fn name");
        while (!check(TokenKind::RParen) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
            auto innerLoc = current_.location;
            Parameter param;
            param.type = parseType();
            Token pname = expect(TokenKind::Identifier, "expected parameter name");
            param.name = pname.text;
            node->params.push_back(std::move(param));
            if (!check(TokenKind::RParen)) {
                expect(TokenKind::Comma, "expected ',' or ')' in parameter list");
            }
            if (current_.location == innerLoc) advance();
        }
        expect(TokenKind::RParen, "expected ')' after parameter list");

        expect(TokenKind::Arrow, "expected '->' after comptime fn parameters");
        node->returnType = parseType();

        expect(TokenKind::LCurly, "expected '{' after return type");
        node->comptimeBody = parseExpression();
        expect(TokenKind::RCurly, "expected '}' after comptime fn body");

        return node;
    }

    diag_.error(current_.location, "expected 'if' or 'fn' after 'comptime'");
    synchronize();
    return nullptr;
}

// --- TypeFn → TypeDecl with TypeFn modifier ---

ASTNodePtr Parser::parseTypeFnAsTypeDecl() {
    SourceLocation loc = current_.location;
    expect(TokenKind::KW_typefn, "expected 'typefn'");

    Token nameTok = expect(TokenKind::Identifier, "expected typefn name");
    auto node = std::make_unique<TypeDecl>(loc, nameTok.text);
    node->modifiers.push_back({ModifierData::TypeFn, loc, {}});

    // Parse template params: typefn Wider(typename T) -> typename
    expect(TokenKind::LParen, "expected '(' after typefn name");
    while (!check(TokenKind::RParen) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
        auto innerLoc = current_.location;
        TemplateParamDecl param;
        param.location = current_.location;
        if (match(TokenKind::KW_typename)) {
            param.kind = TemplateParamDecl::TypeParam;
            Token pname = expect(TokenKind::Identifier, "expected parameter name");
            param.name = pname.text;
        }
        node->templateParams.push_back(std::move(param));
        if (!check(TokenKind::RParen)) {
            expect(TokenKind::Comma, "expected ',' or ')' in typefn parameter list");
        }
        if (current_.location == innerLoc) advance();
    }
    expect(TokenKind::RParen, "expected ')' after typefn parameters");

    expect(TokenKind::Arrow, "expected '->' after typefn parameters");
    expect(TokenKind::KW_typename, "expected 'typename' as typefn return");

    expect(TokenKind::LCurly, "expected '{' after typefn return type");

    // Parse: match T { Type => Type, ... }
    expect(TokenKind::KW_match, "expected 'match' in typefn body");
    Token matchTarget = expect(TokenKind::Identifier, "expected parameter name to match on");
    node->matchTarget = matchTarget.text;

    expect(TokenKind::LCurly, "expected '{' after match target");

    while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
        auto innerLoc = current_.location;
        TypeMatchArm arm;
        arm.location = current_.location;

        if (check(TokenKind::Identifier) && current_.text == "_") {
            advance();
        } else {
            arm.pattern = parseType();
        }

        expect(TokenKind::FatArrow, "expected '=>' in match arm");
        arm.result = parseType();

        match(TokenKind::Comma);

        node->matchArms.push_back(std::move(arm));
        if (current_.location == innerLoc) advance();
    }

    expect(TokenKind::RCurly, "expected '}' to close match block");
    expect(TokenKind::RCurly, "expected '}' to close typefn body");

    return node;
}

// --- DebugDecl ---
//
// `debug` / `view` / `summary` / `inactive_region` / `grayed` / `hidden`
// / `render` are *contextual* — recognized by text inside this parser only.
// The Lexer keeps emitting them as plain Identifier tokens, so user code can
// still name functions like `void debug()` or `void hidden()` in member
// positions (verified via grep over existing fixtures before landing).

bool Parser::isDebugDeclStart() {
    return check(TokenKind::Identifier) && current_.text == "debug" &&
           peekNext().is(TokenKind::Identifier);
}

DebugSliceExpr Parser::parseDebugSliceExpr(const char* contextLabel) {
    DebugSliceExpr slice;
    slice.location = current_.location;

    Token containerTok = expect(TokenKind::Identifier,
                                std::string("expected identifier in ") + contextLabel);
    slice.container = containerTok.text;

    if (!check(TokenKind::LBracket)) {
        return slice; // bare identifier — `field` form
    }

    // `field [ start .. end ]`
    advance(); // consume '['
    slice.isSliced = true;

    if (check(TokenKind::IntegerLiteral)) {
        Token startTok = advance();
        int parsed = 0;
        if (tryParseNonNegativeInt(startTok.text, parsed)) {
            slice.start = static_cast<int64_t>(parsed);
        } else {
            diag_.error(startTok.location, std::string(contextLabel) +
                                               ": slice start must be a non-negative integer");
        }
    } else {
        diag_.error(current_.location,
                    std::string(contextLabel) + ": expected integer literal for slice start");
    }

    expect(TokenKind::DotDot,
           std::string("expected '..' between slice bounds in ") + contextLabel);

    if (check(TokenKind::IntegerLiteral)) {
        Token endTok = advance();
        int parsed = 0;
        if (tryParseNonNegativeInt(endTok.text, parsed)) {
            slice.end = static_cast<int64_t>(parsed);
        } else {
            diag_.error(endTok.location, std::string(contextLabel) +
                                             ": slice end must be a non-negative integer");
        }
    } else {
        diag_.error(current_.location,
                    std::string(contextLabel) + ": expected integer literal for slice end");
    }

    expect(TokenKind::RBracket,
           std::string("expected ']' after slice end in ") + contextLabel);

    return slice;
}

ASTNodePtr Parser::parseDebugDecl() {
    SourceLocation loc = current_.location;
    advance(); // consume 'debug' (identifier text)

    Token targetTok = expect(TokenKind::Identifier, "expected target type name after 'debug'");
    auto node = std::make_unique<DebugDecl>(loc, targetTok.text);

    expect(TokenKind::LCurly, "expected '{' to open debug body");

    while (!check(TokenKind::RCurly) && !check(TokenKind::Eof) && !diag_.reachedLimit()) {
        auto innerLoc = current_.location;

        // Each body item starts with one of: 'view' | 'summary' | 'inactive_region' | 'render'.
        if (!check(TokenKind::Identifier)) {
            diag_.error(current_.location, "expected 'view', 'summary', 'inactive_region', or 'render' in debug body");
            advance();
            if (current_.location == innerLoc) advance();
            continue;
        }

        const std::string& kw = current_.text;
        SourceLocation kwLoc = current_.location;

        if (kw == "view") {
            advance(); // 'view'
            Token nameTok = expect(TokenKind::Identifier, "expected view name after 'view'");
            // optional 'from' (text identifier)
            if (check(TokenKind::Identifier) && current_.text == "from") {
                advance(); // 'from'
            } else {
                diag_.error(current_.location, "expected 'from' between view name and expression");
            }
            DebugSliceExpr slice = parseDebugSliceExpr("view expression");
            expect(TokenKind::Semicolon, "expected ';' after view declaration");
            DebugViewEntry entry;
            entry.name = nameTok.text;
            entry.slice = std::move(slice);
            entry.location = kwLoc;
            node->views.push_back(std::move(entry));
        } else if (kw == "summary") {
            advance(); // 'summary'
            Token strTok = expect(TokenKind::StringLiteral, "expected string literal after 'summary'");
            expect(TokenKind::Semicolon, "expected ';' after summary declaration");
            // Template is stored verbatim; `{...}` interpolation validation
            // is deferred to the query-language layer (not yet wired).
            node->summaryTemplate = strTok.text;
        } else if (kw == "inactive_region") {
            advance(); // 'inactive_region'
            DebugSliceExpr region = parseDebugSliceExpr("inactive_region expression");
            DebugInactiveMode mode = DebugInactiveMode::Default;
            if (check(TokenKind::Identifier) && (current_.text == "grayed" || current_.text == "hidden")) {
                mode = (current_.text == "grayed") ? DebugInactiveMode::Grayed : DebugInactiveMode::Hidden;
                advance();
            }
            expect(TokenKind::Semicolon, "expected ';' after inactive_region declaration");
            DebugInactiveEntry entry;
            entry.region = std::move(region);
            entry.mode = mode;
            entry.location = kwLoc;
            node->inactiveRegions.push_back(std::move(entry));
        } else if (kw == "render") {
            // Capture method = IDENT (optional) and the brace-balanced body so
            // Sema can aim a precise diagnostic at it. Web-rendering semantics
            // are NOT implemented; SemanticAnalyzer explicitly rejects every
            // `render` block (no-silent-degradation) — this is not a live
            // passthrough.
            advance(); // 'render'
            DebugRenderRaw raw;
            raw.location = kwLoc;
            if (check(TokenKind::Identifier) && current_.text == "method") {
                advance();
                expect(TokenKind::Assign, "expected '=' after 'method'");
                Token mTok = expect(TokenKind::Identifier, "expected render method name after '='");
                raw.method = mTok.text;
            }
            // Now expect a `{ ... }` body; capture it raw via brace depth counting.
            if (!check(TokenKind::LCurly)) {
                diag_.error(current_.location, "expected '{' to open render body");
                // recover
                if (current_.location == innerLoc) advance();
                continue;
            }
            std::string rawBody;
            int depth = 0;
            do {
                if (check(TokenKind::LCurly)) {
                    rawBody += "{";
                    ++depth;
                } else if (check(TokenKind::RCurly)) {
                    rawBody += "}";
                    --depth;
                } else if (check(TokenKind::Eof)) {
                    diag_.error(current_.location, "unterminated render body");
                    break;
                } else {
                    // capture token text with a separator (contents are
                    // preserved only as opaque payload for downstream consumers;
                    // a single space separator round-trips well enough).
                    if (!rawBody.empty() && rawBody.back() != '{' && rawBody.back() != ' ') rawBody += " ";
                    rawBody += current_.text;
                }
                advance();
            } while (depth > 0 && !check(TokenKind::Eof));
            raw.rawBody = std::move(rawBody);
            node->renderDecls.push_back(std::move(raw));
        } else {
            diag_.error(current_.location,
                        "unexpected '" + kw + "' in debug body; expected 'view', 'summary', 'inactive_region', or 'render'");
            advance();
        }

        if (current_.location == innerLoc) advance();
    }

    expect(TokenKind::RCurly, "expected '}' to close debug body");
    return node;
}

} // namespace topo
