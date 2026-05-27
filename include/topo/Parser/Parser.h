#ifndef TOPO_PARSER_PARSER_H
#define TOPO_PARSER_PARSER_H

#include "topo/AST/ASTNode.h"
#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include <cstdint>
#include <memory>

namespace topo {

class Parser {
public:
    Parser(Lexer& lexer, DiagnosticEngine& diag);

    std::unique_ptr<TopoFile> parseTopoFile();

private:
    // Token management
    const Token& peek() const;
    const Token& peekNext();
    Token advance();
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    Token expect(TokenKind kind, const std::string& message);

    // Error recovery
    void synchronize();

    // Modifier collection
    std::vector<ModifierData> collectModifiers();

    // Parsing methods
    ASTNodePtr parseTopLevelDecl();
    ASTNodePtr parseImport();              // was parseFileImport
    ASTNodePtr parseStdImportAsDataDecl(); // was parseStdImportDecl
    ASTNodePtr parseTypeAliasAsDataDecl(); // was parseTypeAliasDecl
    ASTNodePtr parseNamespaceDecl();
    ASTNodePtr parseVisibilitySection();
    ASTNodePtr parseMemberDecl();
    ASTNodePtr parseFnLogicBlock();                          // was parseFunctionLogicBlock
    ASTNodePtr parseHandlerDecl();                           // handler Name(In) -> Out;
    ASTNodePtr parseFlowDecl();                              // flow Name { a -> b; ... }
    ASTNodePtr parseFnDeclCppStyle();                        // was parseFunctionDeclCppStyle
    ASTNodePtr parseFnDeclRustStyle();                       // was parseFunctionDeclRustStyle
    ASTNodePtr parseTypeDecl();                              // was parseClassDecl, handles type/class/constraint
    ASTNodePtr parseTypeMember(const std::string& typeName); // was parseClassMember
    ASTNodePtr parseOperatorDecl();
    ASTNodePtr parseTemplateDecl();
    std::vector<TemplateParamDecl> parseTemplateParamList();
    ASTNodePtr parseConstraintAsTypeDecl();  // was parseConstraintDecl
    ASTNodePtr parseAdaptAsTypeDecl();       // was parseAdaptDecl
    ASTNodePtr parseInstantiateAsDataDecl(); // was parseInstantiateDecl
    ASTNodePtr parseComptimeDecl();
    ASTNodePtr parseTypeFnAsTypeDecl();   // was parseTypeFnDecl
    ASTNodePtr parseLifetimeAsDataDecl(); // was parseLifetimeDecl
    ASTNodePtr parseOperationDecl();
    // Debug metadata block. Contextual: triggered when the
    // current token is `Identifier("debug")` followed by `Identifier` followed
    // by `{`. Body keywords (`view` / `summary` / `inactive_region` / `render`
    // / `grayed` / `hidden`) are matched textually, not lexed.
    ASTNodePtr parseDebugDecl();
    bool isDebugDeclStart();   // 3-token lookahead heuristic
    DebugSliceExpr parseDebugSliceExpr(const char* contextLabel);
    std::vector<ReturnParam> parseMultiReturnParams();
    // Parse the optional `with returns(a, _, ...)` clause that may follow a
    // `-> (...)` multi-return signature. `outNames` receives the listed
    // return-param names ("_" wildcards are not included). Sets
    // `outHasClause` to true whenever the `with` keyword was consumed.
    // `outItemCount` receives the total number of items parsed (including
    // wildcards) — used by the caller to enforce count-vs-arity rules.
    void parseUsedReturnsClause(std::vector<std::string>& outNames, bool& outHasClause, size_t& outItemCount);
    ReturnBinding parseReturnBinding();
    void parsePipelineEdges(std::vector<PipelineEdge>& edges);

    // Expression parsing (precedence climbing)
    ExprPtr parseExpression();
    ExprPtr parseBinaryExpr(int minPrec);
    ExprPtr parseUnaryExpr();
    ExprPtr parsePrimaryExpr();
    static int getOperatorPrecedence(TokenKind kind);
    static bool isBinaryOperator(TokenKind kind);

    // Type parsing
    TypeNode parseType();
    // Consume a closing `>` of a type/template angle bracket. When the
    // lexer produced `>>` (ShiftRight) at the join of two nested generic
    // closes (`optional<slice<i64>>`), split it: treat the first `>` as
    // this close and leave a synthetic `>` as the current token for the
    // enclosing close. Returns false (after emitting `msg`) when neither a
    // `>` nor a `>>` is present.
    bool consumeCloseAngle(const std::string& msg);
    // Parse the `<name: Type, ...>` payload of a record<...> composite into
    // outType.recordFields. Called by parseType() after the `record`
    // keyword is consumed.
    void parseRecordFields(TypeNode& outType);
    std::vector<std::string> parseQualifiedName();
    std::string parseFuncName();            // Identifier or DottedName
    std::string parseQualifiedFuncRef();    // Identifier or Identifier::Identifier...
    std::string parseQualifiedNameString(); // Qualified name as single string

    // Priority section parsing
    PriorityLevel parsePriorityLevel();

    // Data-aware optimization hints
    void parseHints(FnDecl& decl);
    int64_t parseCardinalityValue();
    AccessPattern parseAccessPattern(int& tiledSize);

    // Utilities
    bool isNamespaceIdentifier() const;
    Token expectNamespaceIdentifier(const std::string& message);
    bool isVisibilityKeyword(TokenKind kind) const;
    bool isTypeStart() const;

    Lexer& lexer_;
    DiagnosticEngine& diag_;
    Token current_;
    Token lookahead_;
    bool hasLookahead_ = false;
    int recursionDepth_ = 0;
    static constexpr int kMaxRecursionDepth = 256;

    /// RAII guard for tracking recursion depth.
    struct RecursionGuard {
        int& depth;
        bool exceeded = false;
        RecursionGuard(int& d, int max, DiagnosticEngine& diag, const SourceLocation& loc) : depth(d) {
            if (++depth > max) {
                exceeded = true;
                diag.error(loc, "maximum recursion depth exceeded");
                --depth;
            }
        }
        ~RecursionGuard() {
            if (!exceeded) --depth;
        }
        RecursionGuard(const RecursionGuard&) = delete;
        RecursionGuard& operator=(const RecursionGuard&) = delete;
    };

    std::vector<Token> pendingTokens_; // replay buffer (back = next to consume)
};

} // namespace topo

#endif // TOPO_PARSER_PARSER_H
