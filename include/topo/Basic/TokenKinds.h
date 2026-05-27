#ifndef TOPO_BASIC_TOKENKINDS_H
#define TOPO_BASIC_TOKENKINDS_H

namespace topo {

enum class TokenKind {
    // Special
    Eof,
    Error,

    // Literals
    Identifier,
    IntegerLiteral,
    StringLiteral,

    // Keywords
    KW_namespace,
    KW_using,
    KW_import,
    KW_public,
    KW_protected,
    KW_private,
    KW_internal,
    KW_ignore,
    KW_fn,
    KW_stage,
    KW_const,
    KW_void,
    KW_true,
    KW_false,
    KW_return,
    KW_class,
    KW_static,
    KW_explicit,
    KW_virtual, // detected only to produce a prohibition error
    KW_template,
    KW_typename,
    KW_constraint,
    KW_requires,
    KW_adapt,
    KW_instantiate,
    KW_for,
    KW_comptime,
    KW_typefn,
    KW_match,
    KW_if,
    KW_else,
    KW_owned,
    KW_shared,
    KW_weak,
    KW_operator,
    KW_priority,
    KW_type,
    KW_external,
    KW_with, // selective return-fields clause: `with returns(a, _)`
    // Logic-unit / logic-flow declaration vocabulary. `handler` declares a
    // pure In->Out Functor; `flow` declares a DAG of those Functors. Both
    // desugar at parse time onto the existing FnDecl / FnLogicBlock path so
    // every downstream consumer (Sema, SymbolTable, the checkers) sees the
    // same shapes as `fn` / pipeline — only the surface keyword differs.
    KW_handler,
    KW_flow,

    // --- Stdlib bridging types ---
    // Lowercase keywords; recognized as type-name tokens in type position.
    // The Parser accepts these alongside Identifier in `parseType()` and
    // sets TypeNode::nameParts to the canonical lowercase string. Sema
    // (via Stdlib::Types) validates template-arg arity (0 for scalar
    // types, 1 for optional/slice).
    KW_bool,
    KW_i64,
    KW_f64,
    KW_string,
    KW_optional,
    KW_slice,
    // slice<u8>-isomorphic byte view; scalar-shaped (takes no `<...>`).
    KW_bytes,
    // Width-extension scalars (added as new entries, not new code paths)
    KW_u8,
    KW_i32,
    KW_u32,
    KW_u64,
    KW_f32,
    KW_i8,
    KW_i16,
    KW_u16,
    // Semantic scalar: i64-isomorphic ABI, domain-tagged (no `<...>`).
    KW_time_ns,
    // 16-byte value scalar (raw RFC 4122 bytes; no `<...>`).
    KW_uuid,
    // 16-byte value scalar (raw IEEE 754-2008 decimal128 bytes; no `<...>`).
    KW_decimal128,
    // Composite stdlib types. Unlike the scalar/generic keywords above,
    // record and union are followed by a named-field list
    // (`record<id: i64, ...>` / `union<tag: u8, ...>`) and array by a
    // `<T, N>` type+integer pair, parsed by dedicated paths / arg handling
    // in parseType().
    KW_record,
    KW_array,
    KW_union,

    FatArrow, // =>

    // Operators
    Assign,     // =
    Arrow,      // ->
    ColonColon, // ::
    Dot,        // .
    Plus,       // +
    Minus,      // -
    Slash,      // /
    Bang,       // !
    EqEq,       // ==
    NotEq,      // !=
    LessEq,     // <=
    GreaterEq,  // >=
    Percent,    // %
    Pipe,       // |
    Caret,      // ^
    ShiftLeft,  // <<
    ShiftRight, // >>
    AmpAmp,     // &&
    PipePipe,   // ||

    // Delimiters
    LCurly,    // {
    RCurly,    // }
    LParen,    // (
    RParen,    // )
    LAngle,    // <
    RAngle,    // >
    Comma,     // ,
    Semicolon, // ;
    Colon,     // :
    Amp,       // &
    Star,      // *
    Tilde,     // ~
    DotDot,    // ..
    Ellipsis,  // ...

    // Lenient-mode-only delimiters
    At,       // @
    Hash,     // #
    LBracket, // [
    RBracket, // ]
};

inline const char* tokenKindName(TokenKind kind) {
    switch (kind) {
    case TokenKind::Eof: return "Eof";
    case TokenKind::Error: return "Error";
    case TokenKind::Identifier: return "Identifier";
    case TokenKind::IntegerLiteral: return "IntegerLiteral";
    case TokenKind::StringLiteral: return "StringLiteral";
    case TokenKind::KW_namespace: return "KW_namespace";
    case TokenKind::KW_using: return "KW_using";
    case TokenKind::KW_import: return "KW_import";
    case TokenKind::KW_public: return "KW_public";
    case TokenKind::KW_protected: return "KW_protected";
    case TokenKind::KW_private: return "KW_private";
    case TokenKind::KW_internal: return "KW_internal";
    case TokenKind::KW_ignore: return "KW_ignore";
    case TokenKind::KW_fn: return "KW_fn";
    case TokenKind::KW_stage: return "KW_stage";
    case TokenKind::KW_const: return "KW_const";
    case TokenKind::KW_void: return "KW_void";
    case TokenKind::KW_true: return "KW_true";
    case TokenKind::KW_false: return "KW_false";
    case TokenKind::KW_return: return "KW_return";
    case TokenKind::KW_class: return "KW_class";
    case TokenKind::KW_static: return "KW_static";
    case TokenKind::KW_explicit: return "KW_explicit";
    case TokenKind::KW_virtual: return "KW_virtual";
    case TokenKind::KW_template: return "KW_template";
    case TokenKind::KW_typename: return "KW_typename";
    case TokenKind::KW_constraint: return "KW_constraint";
    case TokenKind::KW_requires: return "KW_requires";
    case TokenKind::KW_adapt: return "KW_adapt";
    case TokenKind::KW_instantiate: return "KW_instantiate";
    case TokenKind::KW_for: return "KW_for";
    case TokenKind::KW_comptime: return "KW_comptime";
    case TokenKind::KW_typefn: return "KW_typefn";
    case TokenKind::KW_match: return "KW_match";
    case TokenKind::KW_if: return "KW_if";
    case TokenKind::KW_else: return "KW_else";
    case TokenKind::KW_owned: return "KW_owned";
    case TokenKind::KW_shared: return "KW_shared";
    case TokenKind::KW_weak: return "KW_weak";
    case TokenKind::KW_operator: return "KW_operator";
    case TokenKind::KW_priority: return "KW_priority";
    case TokenKind::KW_type: return "KW_type";
    case TokenKind::KW_external: return "KW_external";
    case TokenKind::KW_with: return "KW_with";
    case TokenKind::KW_handler: return "KW_handler";
    case TokenKind::KW_flow: return "KW_flow";
    case TokenKind::KW_bool: return "KW_bool";
    case TokenKind::KW_i64: return "KW_i64";
    case TokenKind::KW_f64: return "KW_f64";
    case TokenKind::KW_string: return "KW_string";
    case TokenKind::KW_optional: return "KW_optional";
    case TokenKind::KW_slice: return "KW_slice";
    case TokenKind::KW_bytes: return "KW_bytes";
    case TokenKind::KW_u8: return "KW_u8";
    case TokenKind::KW_i32: return "KW_i32";
    case TokenKind::KW_u32: return "KW_u32";
    case TokenKind::KW_u64: return "KW_u64";
    case TokenKind::KW_f32: return "KW_f32";
    case TokenKind::KW_i8: return "KW_i8";
    case TokenKind::KW_i16: return "KW_i16";
    case TokenKind::KW_u16: return "KW_u16";
    case TokenKind::KW_time_ns: return "KW_time_ns";
    case TokenKind::KW_uuid: return "KW_uuid";
    case TokenKind::KW_decimal128: return "KW_decimal128";
    case TokenKind::KW_record: return "KW_record";
    case TokenKind::KW_array: return "KW_array";
    case TokenKind::KW_union: return "KW_union";
    case TokenKind::FatArrow: return "FatArrow";
    case TokenKind::Assign: return "Assign";
    case TokenKind::Arrow: return "Arrow";
    case TokenKind::ColonColon: return "ColonColon";
    case TokenKind::Dot: return "Dot";
    case TokenKind::Plus: return "Plus";
    case TokenKind::Minus: return "Minus";
    case TokenKind::Slash: return "Slash";
    case TokenKind::Bang: return "Bang";
    case TokenKind::EqEq: return "EqEq";
    case TokenKind::NotEq: return "NotEq";
    case TokenKind::LessEq: return "LessEq";
    case TokenKind::GreaterEq: return "GreaterEq";
    case TokenKind::Percent: return "Percent";
    case TokenKind::Pipe: return "Pipe";
    case TokenKind::Caret: return "Caret";
    case TokenKind::ShiftLeft: return "ShiftLeft";
    case TokenKind::ShiftRight: return "ShiftRight";
    case TokenKind::AmpAmp: return "AmpAmp";
    case TokenKind::PipePipe: return "PipePipe";
    case TokenKind::LCurly: return "LCurly";
    case TokenKind::RCurly: return "RCurly";
    case TokenKind::LParen: return "LParen";
    case TokenKind::RParen: return "RParen";
    case TokenKind::LAngle: return "LAngle";
    case TokenKind::RAngle: return "RAngle";
    case TokenKind::Comma: return "Comma";
    case TokenKind::Semicolon: return "Semicolon";
    case TokenKind::Colon: return "Colon";
    case TokenKind::Amp: return "Amp";
    case TokenKind::Star: return "Star";
    case TokenKind::Tilde: return "Tilde";
    case TokenKind::DotDot: return "DotDot";
    case TokenKind::Ellipsis: return "Ellipsis";
    case TokenKind::At: return "At";
    case TokenKind::Hash: return "Hash";
    case TokenKind::LBracket: return "LBracket";
    case TokenKind::RBracket: return "RBracket";
    }
    return "Unknown";
}

} // namespace topo

#endif // TOPO_BASIC_TOKENKINDS_H
