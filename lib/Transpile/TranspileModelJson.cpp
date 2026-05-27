#include "topo/Transpile/TranspileModelJson.h"

using json = nlohmann::json;

namespace topo::transpile {

// ===================================================================
// Local TypeNode / Parameter JSON (topo-core cannot depend on topo-build)
// Same wire format as topo-build/lib/SymbolTableJson.cpp.
// ===================================================================

namespace detail {

static void typeNodeToJson(json& j, const TypeNode& t) {
    j = json::object();
    j["nameParts"] = t.nameParts;
    if (t.isConst) j["isConst"] = true;
    if (t.ownership != OwnershipKind::None) {
        switch (t.ownership) {
        case OwnershipKind::Owned: j["ownership"] = "owned"; break;
        case OwnershipKind::Shared: j["ownership"] = "shared"; break;
        case OwnershipKind::Weak: j["ownership"] = "weak"; break;
        default: break;
        }
    }
    if (t.modifier != TypeNode::None) {
        j["modifier"] = (t.modifier == TypeNode::Ref) ? "ref" : "ptr";
    }
    if (!t.templateArgs.empty()) {
        json args = json::array();
        for (const auto& arg : t.templateArgs) {
            json a;
            typeNodeToJson(a, arg);
            args.push_back(std::move(a));
        }
        j["templateArgs"] = std::move(args);
    }
    if (t.isTemplateParam) j["isTemplateParam"] = true;
    if (t.isVariadic) j["isVariadic"] = true;
    if (t.nonTypeValue) j["nonTypeValue"] = *t.nonTypeValue;
    // Associated-type bindings (Rust `Iterator<Item = u8>`). Omitted when
    // empty so a plain parameterised trait bound (`Iterator` with no
    // assoc-type clause) stays byte-identical to pre-assoc-binding wire
    // output. Each entry carries `{name, type: TypeNode}` and the type is
    // recursive so nested bindings (`Container<Item = optional<T>>`) round-
    // trip naturally.
    if (!t.assocBindings.empty()) {
        json bindings = json::array();
        for (const auto& b : t.assocBindings) {
            json entry = json::object();
            entry["name"] = b.name;
            json ty;
            typeNodeToJson(ty, b.type());
            entry["type"] = std::move(ty);
            bindings.push_back(std::move(entry));
        }
        j["assocBindings"] = std::move(bindings);
    }
    // HRTB lifetimes (Rust `for<'a, 'b> Fn(...)`). Stored sans-apostrophe in
    // the wire (`["a", "b"]`); the leading `'` is added at emit time. Omitted
    // when empty so non-HRTB bound payloads stay byte-identical to pre-HRTB
    // wire output.
    if (!t.hrtbLifetimes.empty()) {
        j["hrtbLifetimes"] = t.hrtbLifetimes;
    }
}

static void typeNodeFromJson(const json& j, TypeNode& t) {
    t.nameParts = j.at("nameParts").get<std::vector<std::string>>();
    t.isConst = j.value("isConst", false);
    // ownership
    if (j.contains("ownership")) {
        auto s = j["ownership"].get<std::string>();
        if (s == "owned")
            t.ownership = OwnershipKind::Owned;
        else if (s == "shared")
            t.ownership = OwnershipKind::Shared;
        else if (s == "weak")
            t.ownership = OwnershipKind::Weak;
        else
            t.ownership = OwnershipKind::None;
    } else {
        t.ownership = OwnershipKind::None;
    }
    // modifier
    if (j.contains("modifier")) {
        auto s = j["modifier"].get<std::string>();
        if (s == "ref")
            t.modifier = TypeNode::Ref;
        else if (s == "ptr")
            t.modifier = TypeNode::Ptr;
        else
            t.modifier = TypeNode::None;
    } else {
        t.modifier = TypeNode::None;
    }
    // templateArgs (recursive)
    if (j.contains("templateArgs")) {
        for (const auto& a : j["templateArgs"]) {
            TypeNode arg;
            typeNodeFromJson(a, arg);
            t.templateArgs.push_back(std::move(arg));
        }
    }
    t.isTemplateParam = j.value("isTemplateParam", false);
    t.isVariadic = j.value("isVariadic", false);
    if (j.contains("nonTypeValue")) t.nonTypeValue = j["nonTypeValue"].get<int>();
    // Associated-type bindings (recursive): mirror the to_json shape and
    // tolerate absent / non-array forms by leaving assocBindings empty so
    // older payloads round-trip byte-identical.
    if (auto it = j.find("assocBindings"); it != j.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (!entry.is_object()) continue;
            TypeNode::RecordField b;
            b.name = entry.value("name", std::string{});
            TypeNode inner;
            if (auto tyIt = entry.find("type"); tyIt != entry.end() && tyIt->is_object()) {
                typeNodeFromJson(*tyIt, inner);
            }
            b.typeBox.push_back(std::move(inner));
            t.assocBindings.push_back(std::move(b));
        }
    }
    // HRTB lifetimes (`for<'a, 'b>`): tolerate absent / non-array forms by
    // leaving hrtbLifetimes empty so non-HRTB payloads round-trip identically.
    if (auto it = j.find("hrtbLifetimes"); it != j.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (entry.is_string()) {
                t.hrtbLifetimes.push_back(entry.get<std::string>());
            }
        }
    }
}

static void paramToJson(json& j, const Parameter& p) {
    j = json::object();
    json ty;
    typeNodeToJson(ty, p.type);
    j["type"] = std::move(ty);
    j["name"] = p.name;
}

static void paramFromJson(const json& j, Parameter& p) {
    typeNodeFromJson(j.at("type"), p.type);
    j.at("name").get_to(p.name);
}

// Declaration-site generic type parameter. The wire shape has grown
// incrementally: kind + name is the MVP core; `bound`/`bounds`/`default`/
// `defaultValue` carry constraints and defaults; `isVariadic` and
// `innerParams` carry C++ parameter packs (`typename... Ts`) and
// template-template parameters (`template<typename> class C`). Every
// optional key is omit-when-empty so a payload that does not exercise a
// feature stays byte-identical to pre-feature output.
static const char* tplKindStr(TemplateParamDecl::Kind k) {
    switch (k) {
    case TemplateParamDecl::NonTypeParam: return "nontype";
    case TemplateParamDecl::TemplateTemplateParam: return "template";
    case TemplateParamDecl::LifetimeParam: return "lifetime";
    case TemplateParamDecl::TypeParam: break;
    }
    return "type";
}

// Forward declarations: templateParamToJson / templateParamFromJson recurse
// through `innerParams` (template-template parameters carry an inner list of
// TemplateParamDecl).
static void templateParamToJson(json& j, const TemplateParamDecl& d);
static void templateParamFromJson(const json& j, TemplateParamDecl& d);

static void templateParamToJson(json& j, const TemplateParamDecl& d) {
    j = json::object();
    j["kind"] = tplKindStr(d.kind);
    j["name"] = d.name;
    // Single-bound stays on the legacy `bound: TypeNode` key (byte-identical
    // wire output for the 1-bound case the existing 5-host extractors all
    // produce). Multi-bound (Rust `T: A + B`, Java/TS intersection
    // `<T extends A & B>`) graduates to `bounds: [TypeNode]`. Readers
    // accept either form, with `bounds` taking precedence when present.
    //
    // For NonTypeParam the same `constraintType` field carries the *value
    // type* (`template <int N>` ⇒ value type `int`; Rust `const N: usize`
    // ⇒ value type `usize`). It rides the same `bound: TypeNode` wire key
    // — the kind discriminator at the head tells consumers which semantic
    // interpretation to apply.
    //
    // For LifetimeParam the same `bound: TypeNode` key carries the
    // outlives target as a single nameParts entry (`["'b"]`, with the
    // apostrophe kept on the outlives target — see ASTNode.h comment). A
    // lifetime param with no outlives clause omits `bound` entirely.
    if (!d.constraintType.nameParts.empty() &&
        (d.kind == TemplateParamDecl::TypeParam ||
         d.kind == TemplateParamDecl::NonTypeParam ||
         d.kind == TemplateParamDecl::LifetimeParam)) {
        if (d.kind == TemplateParamDecl::NonTypeParam ||
            d.kind == TemplateParamDecl::LifetimeParam ||
            d.extraBounds.empty()) {
            json b;
            typeNodeToJson(b, d.constraintType);
            j["bound"] = std::move(b);
        } else {
            json bs = json::array();
            json first;
            typeNodeToJson(first, d.constraintType);
            bs.push_back(std::move(first));
            for (const auto& eb : d.extraBounds) {
                json b;
                typeNodeToJson(b, eb);
                bs.push_back(std::move(b));
            }
            j["bounds"] = std::move(bs);
        }
    }
    // Optional default: `<T = Default>` in Rust (struct/enum/trait only),
    // C++17 (`template <typename T = int>`), TS, and Python PEP 696 (3.13+).
    // Same omit-if-empty rule as `bound` so the wire stays byte-identical
    // for type params without defaults.
    if (d.kind == TemplateParamDecl::TypeParam && d.defaultType.has_value() &&
        !d.defaultType->nameParts.empty()) {
        json def;
        typeNodeToJson(def, *d.defaultType);
        j["default"] = std::move(def);
    }
    // NonTypeParam default literal expression (`<int N = 10>` /
    // `<const N: usize = 16>`). Carried as a string under the
    // `defaultValue` wire key — separate from `default` (TypeNode for
    // TypeParam) so the two semantic axes never collide. omit-when-empty
    // keeps the wire byte-identical for nontype params without a default.
    if (d.kind == TemplateParamDecl::NonTypeParam &&
        d.defaultValue.has_value() && !d.defaultValue->empty()) {
        j["defaultValue"] = *d.defaultValue;
    }
    // Variadic parameter pack (C++ `typename... Ts`). Carried as the
    // boolean `isVariadic` wire key; omit-when-false keeps a non-variadic
    // param byte-identical to pre-Phase-6 output. A TypeParam stays
    // kind="type" — `isVariadic` is an orthogonal flag, not a kind.
    if (d.isVariadic) {
        j["isVariadic"] = true;
    }
    // Template-template parameter inner list (C++ `template<typename> class
    // C` ⇒ innerParams = [{kind:"type", name:""}]). Serialized recursively
    // as `innerParams: [TemplateParamDecl]`; omit-when-empty so non-template
    // kinds stay byte-identical.
    if (!d.innerParams.empty()) {
        json ips = json::array();
        for (const auto& ip : d.innerParams) {
            json ipj;
            templateParamToJson(ipj, ip);
            ips.push_back(std::move(ipj));
        }
        j["innerParams"] = std::move(ips);
    }
}

static void templateParamFromJson(const json& j, TemplateParamDecl& d) {
    const std::string k = j.value("kind", "type");
    d.kind = k == "nontype"    ? TemplateParamDecl::NonTypeParam
             : k == "template" ? TemplateParamDecl::TemplateTemplateParam
             : k == "lifetime" ? TemplateParamDecl::LifetimeParam
                               : TemplateParamDecl::TypeParam;
    d.name = j.value("name", std::string{});
    d.constraintType = TypeNode{};
    d.extraBounds.clear();
    // `bounds: [TypeNode]` (multi-bound) takes precedence over the legacy
    // `bound: TypeNode` (single-bound). When both are present `bounds` wins.
    if (auto it = j.find("bounds"); it != j.end() && it->is_array() &&
        !it->empty()) {
        typeNodeFromJson((*it)[0], d.constraintType);
        for (size_t i = 1; i < it->size(); ++i) {
            TypeNode eb;
            typeNodeFromJson((*it)[i], eb);
            d.extraBounds.push_back(std::move(eb));
        }
    } else if (auto it2 = j.find("bound"); it2 != j.end() && it2->is_object()) {
        typeNodeFromJson(*it2, d.constraintType);
    }
    d.defaultType.reset();
    if (auto it = j.find("default"); it != j.end() && it->is_object()) {
        TypeNode tn;
        typeNodeFromJson(*it, tn);
        d.defaultType = std::move(tn);
    }
    d.defaultValue.reset();
    if (auto it = j.find("defaultValue"); it != j.end() && it->is_string()) {
        std::string s = it->get<std::string>();
        if (!s.empty()) d.defaultValue = std::move(s);
    }
    d.isVariadic = j.value("isVariadic", false);
    d.innerParams.clear();
    if (auto it = j.find("innerParams"); it != j.end() && it->is_array()) {
        for (const auto& ipj : *it) {
            TemplateParamDecl ip;
            templateParamFromJson(ipj, ip);
            d.innerParams.push_back(std::move(ip));
        }
    }
}

} // namespace detail

// ===================================================================
// Enum serialization
// ===================================================================

void to_json(json& j, Fidelity f) {
    switch (f) {
    case Fidelity::Source: j = "source"; break;
    case Fidelity::Recovered: j = "recovered"; break;
    case Fidelity::Inferred: j = "inferred"; break;
    }
}

void from_json(const json& j, Fidelity& f) {
    auto s = j.get<std::string>();
    if (s == "recovered")
        f = Fidelity::Recovered;
    else if (s == "inferred")
        f = Fidelity::Inferred;
    else
        f = Fidelity::Source;
}

void to_json(json& j, BinaryOp op) {
    switch (op) {
    case BinaryOp::Add: j = "add"; break;
    case BinaryOp::Sub: j = "sub"; break;
    case BinaryOp::Mul: j = "mul"; break;
    case BinaryOp::Div: j = "div"; break;
    case BinaryOp::Mod: j = "mod"; break;
    case BinaryOp::Eq: j = "eq"; break;
    case BinaryOp::NotEq: j = "noteq"; break;
    case BinaryOp::Less: j = "less"; break;
    case BinaryOp::Greater: j = "greater"; break;
    case BinaryOp::LessEq: j = "lesseq"; break;
    case BinaryOp::GreaterEq: j = "greatereq"; break;
    case BinaryOp::And: j = "and"; break;
    case BinaryOp::Or: j = "or"; break;
    case BinaryOp::BitAnd: j = "bitand"; break;
    case BinaryOp::BitOr: j = "bitor"; break;
    case BinaryOp::BitXor: j = "bitxor"; break;
    case BinaryOp::Shl: j = "shl"; break;
    case BinaryOp::Shr: j = "shr"; break;
    }
}

void from_json(const json& j, BinaryOp& op) {
    auto s = j.get<std::string>();
    if (s == "add")
        op = BinaryOp::Add;
    else if (s == "sub")
        op = BinaryOp::Sub;
    else if (s == "mul")
        op = BinaryOp::Mul;
    else if (s == "div")
        op = BinaryOp::Div;
    else if (s == "mod")
        op = BinaryOp::Mod;
    else if (s == "eq")
        op = BinaryOp::Eq;
    else if (s == "noteq")
        op = BinaryOp::NotEq;
    else if (s == "less")
        op = BinaryOp::Less;
    else if (s == "greater")
        op = BinaryOp::Greater;
    else if (s == "lesseq")
        op = BinaryOp::LessEq;
    else if (s == "greatereq")
        op = BinaryOp::GreaterEq;
    else if (s == "and")
        op = BinaryOp::And;
    else if (s == "or")
        op = BinaryOp::Or;
    else if (s == "bitand")
        op = BinaryOp::BitAnd;
    else if (s == "bitor")
        op = BinaryOp::BitOr;
    else if (s == "bitxor")
        op = BinaryOp::BitXor;
    else if (s == "shl")
        op = BinaryOp::Shl;
    else
        op = BinaryOp::Shr;
}

void to_json(json& j, UnaryOp op) {
    switch (op) {
    case UnaryOp::Negate: j = "negate"; break;
    case UnaryOp::Not: j = "not"; break;
    case UnaryOp::BitNot: j = "bitnot"; break;
    case UnaryOp::PreIncrement: j = "preincrement"; break;
    case UnaryOp::PostIncrement: j = "postincrement"; break;
    case UnaryOp::PreDecrement: j = "predecrement"; break;
    case UnaryOp::PostDecrement: j = "postdecrement"; break;
    }
}

void from_json(const json& j, UnaryOp& op) {
    auto s = j.get<std::string>();
    if (s == "not")
        op = UnaryOp::Not;
    else if (s == "bitnot")
        op = UnaryOp::BitNot;
    else if (s == "preincrement")
        op = UnaryOp::PreIncrement;
    else if (s == "postincrement")
        op = UnaryOp::PostIncrement;
    else if (s == "predecrement")
        op = UnaryOp::PreDecrement;
    else if (s == "postdecrement")
        op = UnaryOp::PostDecrement;
    else
        op = UnaryOp::Negate;
}

void to_json(json& j, LiteralKind k) {
    switch (k) {
    case LiteralKind::Integer: j = "integer"; break;
    case LiteralKind::Float: j = "float"; break;
    case LiteralKind::Boolean: j = "boolean"; break;
    case LiteralKind::String: j = "string"; break;
    }
}

void from_json(const json& j, LiteralKind& k) {
    auto s = j.get<std::string>();
    if (s == "float")
        k = LiteralKind::Float;
    else if (s == "boolean")
        k = LiteralKind::Boolean;
    else if (s == "string")
        k = LiteralKind::String;
    else
        k = LiteralKind::Integer;
}

void to_json(json& j, CaptureMode m) {
    switch (m) {
    case CaptureMode::ByValue: j = "by_value"; break;
    case CaptureMode::ByReference: j = "by_reference"; break;
    }
}

void from_json(const json& j, CaptureMode& m) {
    auto s = j.get<std::string>();
    if (s == "by_reference")
        m = CaptureMode::ByReference;
    else
        m = CaptureMode::ByValue;
}

void to_json(json& j, DecompileLevel l) {
    switch (l) {
    case DecompileLevel::Direct: j = "direct"; break;
    case DecompileLevel::Structured: j = "structured"; break;
    case DecompileLevel::Idiomatic: j = "idiomatic"; break;
    }
}

void from_json(const json& j, DecompileLevel& l) {
    auto s = j.get<std::string>();
    if (s == "structured")
        l = DecompileLevel::Structured;
    else if (s == "idiomatic")
        l = DecompileLevel::Idiomatic;
    else
        l = DecompileLevel::Direct;
}

// ===================================================================
// Expression serialization (polymorphic via "kind" discriminator)
// ===================================================================

static const char* exprKindStr(Expr::Kind k) {
    switch (k) {
    case Expr::Kind::BinaryOp: return "binaryop";
    case Expr::Kind::UnaryOp: return "unaryop";
    case Expr::Kind::Call: return "call";
    case Expr::Kind::MemberAccess: return "memberaccess";
    case Expr::Kind::Index: return "index";
    case Expr::Kind::Literal: return "literal";
    case Expr::Kind::VarRef: return "varref";
    case Expr::Kind::Construct: return "construct";
    case Expr::Kind::Lambda: return "lambda";
    case Expr::Kind::Throw: return "throw";
    case Expr::Kind::Unsupported: return "unsupported";
    case Expr::Kind::Ternary: return "ternary";
    case Expr::Kind::CompoundAssign: return "compoundassign";
    }
    return "unsupported";
}

static Expr::Kind exprKindFromStr(const std::string& s) {
    if (s == "binaryop") return Expr::Kind::BinaryOp;
    if (s == "unaryop") return Expr::Kind::UnaryOp;
    if (s == "call") return Expr::Kind::Call;
    if (s == "memberaccess") return Expr::Kind::MemberAccess;
    if (s == "index") return Expr::Kind::Index;
    if (s == "literal") return Expr::Kind::Literal;
    if (s == "varref") return Expr::Kind::VarRef;
    if (s == "construct") return Expr::Kind::Construct;
    if (s == "lambda") return Expr::Kind::Lambda;
    if (s == "throw") return Expr::Kind::Throw;
    if (s == "ternary") return Expr::Kind::Ternary;
    if (s == "compoundassign") return Expr::Kind::CompoundAssign;
    return Expr::Kind::Unsupported;
}

// Forward declarations (Lambda serialization needs stmt helpers)
static json serializeStmtVec(const std::vector<StmtPtr>& stmts);
static std::vector<StmtPtr> deserializeStmtVec(const json& j);

nlohmann::json serializeExpr(const Expr& e) {
    json j = json::object();
    j["kind"] = exprKindStr(e.kind());
    j["fidelity"] = e.fidelity;

    switch (e.kind()) {
    case Expr::Kind::BinaryOp: {
        const auto& be = static_cast<const BinaryOpExpr&>(e);
        j["op"] = be.op;
        j["lhs"] = serializeExpr(*be.lhs);
        j["rhs"] = serializeExpr(*be.rhs);
        break;
    }
    case Expr::Kind::UnaryOp: {
        const auto& ue = static_cast<const UnaryOpExpr&>(e);
        j["op"] = ue.op;
        j["operand"] = serializeExpr(*ue.operand);
        break;
    }
    case Expr::Kind::Call: {
        const auto& ce = static_cast<const CallExpr&>(e);
        j["callee"] = ce.callee;
        json args = json::array();
        for (const auto& arg : ce.args)
            args.push_back(serializeExpr(*arg));
        j["args"] = std::move(args);
        break;
    }
    case Expr::Kind::MemberAccess: {
        const auto& me = static_cast<const MemberAccessExpr&>(e);
        j["object"] = serializeExpr(*me.object);
        j["member"] = me.member;
        break;
    }
    case Expr::Kind::Index: {
        const auto& ie = static_cast<const IndexExpr&>(e);
        j["object"] = serializeExpr(*ie.object);
        j["index"] = serializeExpr(*ie.index);
        break;
    }
    case Expr::Kind::Literal: {
        const auto& le = static_cast<const LiteralExpr&>(e);
        j["litKind"] = le.litKind;
        j["value"] = le.value;
        break;
    }
    case Expr::Kind::VarRef: {
        const auto& ve = static_cast<const VarRefExpr&>(e);
        j["name"] = ve.name;
        break;
    }
    case Expr::Kind::Construct: {
        const auto& ce = static_cast<const ConstructExpr&>(e);
        json ty;
        detail::typeNodeToJson(ty, ce.type);
        j["type"] = std::move(ty);
        json args = json::array();
        for (const auto& arg : ce.args)
            args.push_back(serializeExpr(*arg));
        j["args"] = std::move(args);
        break;
    }
    case Expr::Kind::Lambda: {
        const auto& le = static_cast<const LambdaExpr&>(e);
        json caps = json::array();
        for (const auto& c : le.captures) {
            json cj = json::object();
            cj["name"] = c.name;
            cj["mode"] = c.mode;
            caps.push_back(std::move(cj));
        }
        j["captures"] = std::move(caps);
        json params = json::array();
        for (const auto& p : le.params) {
            json pj;
            detail::paramToJson(pj, p);
            params.push_back(std::move(pj));
        }
        j["params"] = std::move(params);
        json rty;
        detail::typeNodeToJson(rty, le.returnType);
        j["returnType"] = std::move(rty);
        j["body"] = serializeStmtVec(le.body);
        break;
    }
    case Expr::Kind::Throw: {
        const auto& te = static_cast<const ThrowExpr&>(e);
        j["operand"] = serializeExpr(*te.operand);
        break;
    }
    case Expr::Kind::Unsupported: {
        const auto& ue = static_cast<const UnsupportedExpr&>(e);
        j["description"] = ue.description;
        break;
    }
    case Expr::Kind::Ternary: {
        const auto& te = static_cast<const TernaryExpr&>(e);
        j["condition"] = serializeExpr(*te.condition);
        j["trueExpr"] = serializeExpr(*te.trueExpr);
        j["falseExpr"] = serializeExpr(*te.falseExpr);
        break;
    }
    case Expr::Kind::CompoundAssign: {
        const auto& ca = static_cast<const CompoundAssignExpr&>(e);
        j["op"] = ca.op;
        j["target"] = serializeExpr(*ca.target);
        j["value"] = serializeExpr(*ca.value);
        break;
    }
    }
    return j;
}

ExprPtr deserializeExpr(const json& j) {
    auto k = exprKindFromStr(j.at("kind").get<std::string>());
    Fidelity fid = j.value("fidelity", Fidelity::Source);

    switch (k) {
    case Expr::Kind::BinaryOp: {
        auto e = std::make_unique<BinaryOpExpr>();
        e->fidelity = fid;
        e->op = j.at("op").get<BinaryOp>();
        e->lhs = deserializeExpr(j.at("lhs"));
        e->rhs = deserializeExpr(j.at("rhs"));
        return e;
    }
    case Expr::Kind::UnaryOp: {
        auto e = std::make_unique<UnaryOpExpr>();
        e->fidelity = fid;
        e->op = j.at("op").get<UnaryOp>();
        e->operand = deserializeExpr(j.at("operand"));
        return e;
    }
    case Expr::Kind::Call: {
        auto e = std::make_unique<CallExpr>();
        e->fidelity = fid;
        j.at("callee").get_to(e->callee);
        for (const auto& a : j.at("args"))
            e->args.push_back(deserializeExpr(a));
        return e;
    }
    case Expr::Kind::MemberAccess: {
        auto e = std::make_unique<MemberAccessExpr>();
        e->fidelity = fid;
        e->object = deserializeExpr(j.at("object"));
        j.at("member").get_to(e->member);
        return e;
    }
    case Expr::Kind::Index: {
        auto e = std::make_unique<IndexExpr>();
        e->fidelity = fid;
        e->object = deserializeExpr(j.at("object"));
        e->index = deserializeExpr(j.at("index"));
        return e;
    }
    case Expr::Kind::Literal: {
        auto e = std::make_unique<LiteralExpr>();
        e->fidelity = fid;
        e->litKind = j.at("litKind").get<LiteralKind>();
        j.at("value").get_to(e->value);
        return e;
    }
    case Expr::Kind::VarRef: {
        auto e = std::make_unique<VarRefExpr>();
        e->fidelity = fid;
        j.at("name").get_to(e->name);
        return e;
    }
    case Expr::Kind::Construct: {
        auto e = std::make_unique<ConstructExpr>();
        e->fidelity = fid;
        detail::typeNodeFromJson(j.at("type"), e->type);
        for (const auto& a : j.at("args"))
            e->args.push_back(deserializeExpr(a));
        return e;
    }
    case Expr::Kind::Lambda: {
        auto e = std::make_unique<LambdaExpr>();
        e->fidelity = fid;
        for (const auto& cj : j.at("captures")) {
            CaptureEntry ce;
            cj.at("name").get_to(ce.name);
            ce.mode = cj.at("mode").get<CaptureMode>();
            e->captures.push_back(std::move(ce));
        }
        for (const auto& pj : j.at("params")) {
            Parameter p;
            detail::paramFromJson(pj, p);
            e->params.push_back(std::move(p));
        }
        detail::typeNodeFromJson(j.at("returnType"), e->returnType);
        e->body = deserializeStmtVec(j.at("body"));
        return e;
    }
    case Expr::Kind::Throw: {
        auto e = std::make_unique<ThrowExpr>();
        e->fidelity = fid;
        e->operand = deserializeExpr(j.at("operand"));
        return e;
    }
    case Expr::Kind::Unsupported: {
        auto e = std::make_unique<UnsupportedExpr>();
        e->fidelity = fid;
        j.at("description").get_to(e->description);
        return e;
    }
    case Expr::Kind::Ternary: {
        auto e = std::make_unique<TernaryExpr>();
        e->fidelity = fid;
        e->condition = deserializeExpr(j.at("condition"));
        e->trueExpr = deserializeExpr(j.at("trueExpr"));
        e->falseExpr = deserializeExpr(j.at("falseExpr"));
        return e;
    }
    case Expr::Kind::CompoundAssign: {
        auto e = std::make_unique<CompoundAssignExpr>();
        e->fidelity = fid;
        e->op = j.at("op").get<BinaryOp>();
        e->target = deserializeExpr(j.at("target"));
        e->value = deserializeExpr(j.at("value"));
        return e;
    }
    }
    // Unreachable — all enum values handled above
    auto e = std::make_unique<UnsupportedExpr>();
    e->fidelity = fid;
    e->description = "unknown expression kind";
    return e;
}

// ===================================================================
// Statement serialization (polymorphic via "kind" discriminator)
// ===================================================================

static const char* stmtKindStr(Stmt::Kind k) {
    switch (k) {
    case Stmt::Kind::VarDecl: return "vardecl";
    case Stmt::Kind::Assign: return "assign";
    case Stmt::Kind::Return: return "return";
    case Stmt::Kind::If: return "if";
    case Stmt::Kind::For: return "for";
    case Stmt::Kind::While: return "while";
    case Stmt::Kind::ExprStmt: return "exprstmt";
    case Stmt::Kind::TryCatch: return "trycatch";
    case Stmt::Kind::Break: return "break";
    case Stmt::Kind::Continue: return "continue";
    case Stmt::Kind::Switch: return "switch";
    }
    return "exprstmt";
}

static Stmt::Kind stmtKindFromStr(const std::string& s) {
    if (s == "vardecl") return Stmt::Kind::VarDecl;
    if (s == "assign") return Stmt::Kind::Assign;
    if (s == "return") return Stmt::Kind::Return;
    if (s == "if") return Stmt::Kind::If;
    if (s == "for") return Stmt::Kind::For;
    if (s == "while") return Stmt::Kind::While;
    if (s == "trycatch") return Stmt::Kind::TryCatch;
    if (s == "break") return Stmt::Kind::Break;
    if (s == "continue") return Stmt::Kind::Continue;
    if (s == "switch") return Stmt::Kind::Switch;
    return Stmt::Kind::ExprStmt;
}

static json serializeStmtVec(const std::vector<StmtPtr>& stmts) {
    json arr = json::array();
    for (const auto& s : stmts)
        arr.push_back(serializeStmt(*s));
    return arr;
}

static std::vector<StmtPtr> deserializeStmtVec(const json& j) {
    std::vector<StmtPtr> result;
    for (const auto& elem : j)
        result.push_back(deserializeStmt(elem));
    return result;
}

nlohmann::json serializeStmt(const Stmt& s) {
    json j = json::object();
    j["kind"] = stmtKindStr(s.kind());
    j["fidelity"] = s.fidelity;

    switch (s.kind()) {
    case Stmt::Kind::VarDecl: {
        const auto& vd = static_cast<const VarDeclStmt&>(s);
        json ty;
        detail::typeNodeToJson(ty, vd.type);
        j["type"] = std::move(ty);
        j["name"] = vd.name;
        if (vd.init) j["init"] = serializeExpr(*vd.init);
        break;
    }
    case Stmt::Kind::Assign: {
        const auto& as = static_cast<const AssignStmt&>(s);
        j["target"] = serializeExpr(*as.target);
        j["value"] = serializeExpr(*as.value);
        break;
    }
    case Stmt::Kind::Return: {
        const auto& rs = static_cast<const ReturnStmt&>(s);
        if (rs.value) j["value"] = serializeExpr(*rs.value);
        break;
    }
    case Stmt::Kind::If: {
        const auto& is = static_cast<const IfStmt&>(s);
        j["condition"] = serializeExpr(*is.condition);
        j["thenBody"] = serializeStmtVec(is.thenBody);
        if (!is.elseBody.empty()) j["elseBody"] = serializeStmtVec(is.elseBody);
        break;
    }
    case Stmt::Kind::For: {
        const auto& fs = static_cast<const ForStmt&>(s);
        if (fs.init) j["init"] = serializeStmt(*fs.init);
        if (fs.condition) j["condition"] = serializeExpr(*fs.condition);
        if (fs.increment) j["increment"] = serializeExpr(*fs.increment);
        j["body"] = serializeStmtVec(fs.body);
        break;
    }
    case Stmt::Kind::While: {
        const auto& ws = static_cast<const WhileStmt&>(s);
        j["condition"] = serializeExpr(*ws.condition);
        j["body"] = serializeStmtVec(ws.body);
        break;
    }
    case Stmt::Kind::ExprStmt: {
        const auto& es = static_cast<const ExprStmt&>(s);
        j["expr"] = serializeExpr(*es.expr);
        break;
    }
    case Stmt::Kind::TryCatch: {
        const auto& tc = static_cast<const TryCatchStmt&>(s);
        j["tryBody"] = serializeStmtVec(tc.tryBody);
        json catches = json::array();
        for (const auto& c : tc.catchClauses) {
            json cj = json::object();
            json ety;
            detail::typeNodeToJson(ety, c.exceptionType);
            cj["exceptionType"] = std::move(ety);
            cj["varName"] = c.varName;
            cj["body"] = serializeStmtVec(c.body);
            catches.push_back(std::move(cj));
        }
        j["catchClauses"] = std::move(catches);
        if (!tc.finallyBody.empty()) j["finallyBody"] = serializeStmtVec(tc.finallyBody);
        break;
    }
    case Stmt::Kind::Break:
    case Stmt::Kind::Continue: break; // no additional fields
    case Stmt::Kind::Switch: {
        const auto& sw = static_cast<const SwitchStmt&>(s);
        j["subject"] = serializeExpr(*sw.subject);
        json cases = json::array();
        for (const auto& c : sw.cases) {
            json cj = json::object();
            if (c.value) cj["value"] = serializeExpr(*c.value);
            cj["body"] = serializeStmtVec(c.body);
            cases.push_back(std::move(cj));
        }
        j["cases"] = std::move(cases);
        break;
    }
    }
    return j;
}

StmtPtr deserializeStmt(const json& j) {
    auto k = stmtKindFromStr(j.at("kind").get<std::string>());
    Fidelity fid = j.value("fidelity", Fidelity::Source);

    switch (k) {
    case Stmt::Kind::VarDecl: {
        auto s = std::make_unique<VarDeclStmt>();
        s->fidelity = fid;
        detail::typeNodeFromJson(j.at("type"), s->type);
        j.at("name").get_to(s->name);
        if (j.contains("init")) s->init = deserializeExpr(j["init"]);
        return s;
    }
    case Stmt::Kind::Assign: {
        auto s = std::make_unique<AssignStmt>();
        s->fidelity = fid;
        s->target = deserializeExpr(j.at("target"));
        s->value = deserializeExpr(j.at("value"));
        return s;
    }
    case Stmt::Kind::Return: {
        auto s = std::make_unique<ReturnStmt>();
        s->fidelity = fid;
        if (j.contains("value")) s->value = deserializeExpr(j["value"]);
        return s;
    }
    case Stmt::Kind::If: {
        auto s = std::make_unique<IfStmt>();
        s->fidelity = fid;
        s->condition = deserializeExpr(j.at("condition"));
        s->thenBody = deserializeStmtVec(j.at("thenBody"));
        if (j.contains("elseBody")) s->elseBody = deserializeStmtVec(j["elseBody"]);
        return s;
    }
    case Stmt::Kind::For: {
        auto s = std::make_unique<ForStmt>();
        s->fidelity = fid;
        if (j.contains("init")) s->init = deserializeStmt(j["init"]);
        if (j.contains("condition")) s->condition = deserializeExpr(j["condition"]);
        if (j.contains("increment")) s->increment = deserializeExpr(j["increment"]);
        s->body = deserializeStmtVec(j.at("body"));
        return s;
    }
    case Stmt::Kind::While: {
        auto s = std::make_unique<WhileStmt>();
        s->fidelity = fid;
        s->condition = deserializeExpr(j.at("condition"));
        s->body = deserializeStmtVec(j.at("body"));
        return s;
    }
    case Stmt::Kind::ExprStmt: {
        auto s = std::make_unique<ExprStmt>();
        s->fidelity = fid;
        s->expr = deserializeExpr(j.at("expr"));
        return s;
    }
    case Stmt::Kind::TryCatch: {
        auto s = std::make_unique<TryCatchStmt>();
        s->fidelity = fid;
        s->tryBody = deserializeStmtVec(j.at("tryBody"));
        for (const auto& cj : j.at("catchClauses")) {
            CatchClause c;
            detail::typeNodeFromJson(cj.at("exceptionType"), c.exceptionType);
            cj.at("varName").get_to(c.varName);
            c.body = deserializeStmtVec(cj.at("body"));
            s->catchClauses.push_back(std::move(c));
        }
        if (j.contains("finallyBody")) s->finallyBody = deserializeStmtVec(j["finallyBody"]);
        return s;
    }
    case Stmt::Kind::Break: {
        auto s = std::make_unique<BreakStmt>();
        s->fidelity = fid;
        return s;
    }
    case Stmt::Kind::Continue: {
        auto s = std::make_unique<ContinueStmt>();
        s->fidelity = fid;
        return s;
    }
    case Stmt::Kind::Switch: {
        auto s = std::make_unique<SwitchStmt>();
        s->fidelity = fid;
        s->subject = deserializeExpr(j.at("subject"));
        for (const auto& cj : j.at("cases")) {
            SwitchCase sc;
            if (cj.contains("value")) sc.value = deserializeExpr(cj["value"]);
            sc.body = deserializeStmtVec(cj.at("body"));
            s->cases.push_back(std::move(sc));
        }
        return s;
    }
    }
    // Unreachable — all enum values handled above
    auto s = std::make_unique<ExprStmt>();
    s->fidelity = fid;
    auto unsup = std::make_unique<UnsupportedExpr>();
    unsup->description = "unknown statement kind";
    s->expr = std::move(unsup);
    return s;
}

// ===================================================================
// Top-level types
// ===================================================================

void to_json(json& j, const TranspileField& f) {
    j = json::object();
    json ty;
    detail::typeNodeToJson(ty, f.type);
    j["type"] = std::move(ty);
    j["name"] = f.name;
    j["fidelity"] = f.fidelity;
}

void from_json(const json& j, TranspileField& f) {
    detail::typeNodeFromJson(j.at("type"), f.type);
    j.at("name").get_to(f.name);
    f.fidelity = j.value("fidelity", Fidelity::Source);
}

void to_json(json& j, const TranspileType& t) {
    j = json::object();
    j["qualifiedName"] = t.qualifiedName;
    j["fields"] = t.fields;
    // Omit when empty (mirrors the throwsClause / unsupported idiom): keeps
    // pre-inheritance JSON byte-identical and lets old payloads round-trip.
    if (!t.baseClasses.empty()) {
        json bases = json::array();
        for (const auto& b : t.baseClasses) {
            json bj;
            detail::typeNodeToJson(bj, b);
            bases.push_back(std::move(bj));
        }
        j["baseClasses"] = std::move(bases);
    }
    // Parallel discriminator, same omit-when-empty rule. Serialized as a
    // string array ("class"/"interface") for forward readability.
    if (!t.baseClassKinds.empty()) {
        json kinds = json::array();
        for (const auto& k : t.baseClassKinds) {
            kinds.push_back(k == BaseClassKind::Interface ? "interface" : "class");
        }
        j["baseClassKinds"] = std::move(kinds);
    }
    if (!t.templateParams.empty()) {
        json tps = json::array();
        for (const auto& p : t.templateParams) {
            json pj;
            detail::templateParamToJson(pj, p);
            tps.push_back(std::move(pj));
        }
        j["templateParams"] = std::move(tps);
    }
    j["fidelity"] = t.fidelity;
}

void from_json(const json& j, TranspileType& t) {
    j.at("qualifiedName").get_to(t.qualifiedName);
    j.at("fields").get_to(t.fields);
    t.baseClasses.clear();
    if (j.contains("baseClasses")) {
        for (const auto& bj : j["baseClasses"]) {
            TypeNode b;
            detail::typeNodeFromJson(bj, b);
            t.baseClasses.push_back(std::move(b));
        }
    }
    t.baseClassKinds.clear();
    if (j.contains("baseClassKinds")) {
        for (const auto& kj : j["baseClassKinds"]) {
            t.baseClassKinds.push_back(kj.get<std::string>() == "interface" ? BaseClassKind::Interface
                                                                            : BaseClassKind::Class);
        }
    }
    t.templateParams.clear();
    if (j.contains("templateParams")) {
        for (const auto& pj : j["templateParams"]) {
            TemplateParamDecl p;
            detail::templateParamFromJson(pj, p);
            t.templateParams.push_back(std::move(p));
        }
    }
    t.fidelity = j.value("fidelity", Fidelity::Source);
}

void to_json(json& j, const TranspileFunction& f) {
    j = json::object();
    j["qualifiedName"] = f.qualifiedName;
    json rty;
    detail::typeNodeToJson(rty, f.returnType);
    j["returnType"] = std::move(rty);

    json params = json::array();
    for (const auto& p : f.params) {
        json pj;
        detail::paramToJson(pj, p);
        params.push_back(std::move(pj));
    }
    j["params"] = std::move(params);

    j["body"] = serializeStmtVec(f.body);

    if (!f.unsupported.empty()) j["unsupported"] = f.unsupported;
    j["fidelity"] = f.fidelity;
    if (!f.accessModifier.empty()) j["accessModifier"] = f.accessModifier;

    if (!f.throwsClause.empty()) {
        json tc = json::array();
        for (const auto& t : f.throwsClause) {
            json tj;
            detail::typeNodeToJson(tj, t);
            tc.push_back(std::move(tj));
        }
        j["throwsClause"] = std::move(tc);
    }

    if (!f.templateParams.empty()) {
        json tps = json::array();
        for (const auto& p : f.templateParams) {
            json pj;
            detail::templateParamToJson(pj, p);
            tps.push_back(std::move(pj));
        }
        j["templateParams"] = std::move(tps);
    }
}

void from_json(const json& j, TranspileFunction& f) {
    j.at("qualifiedName").get_to(f.qualifiedName);
    detail::typeNodeFromJson(j.at("returnType"), f.returnType);

    f.params.clear();
    for (const auto& pj : j.at("params")) {
        Parameter p;
        detail::paramFromJson(pj, p);
        f.params.push_back(std::move(p));
    }

    f.body = deserializeStmtVec(j.at("body"));

    if (j.contains("unsupported")) j["unsupported"].get_to(f.unsupported);
    f.fidelity = j.value("fidelity", Fidelity::Source);
    if (j.contains("accessModifier")) f.accessModifier = j["accessModifier"].get<std::string>();

    f.throwsClause.clear();
    if (j.contains("throwsClause")) {
        for (const auto& tj : j["throwsClause"]) {
            TypeNode t;
            detail::typeNodeFromJson(tj, t);
            f.throwsClause.push_back(std::move(t));
        }
    }

    f.templateParams.clear();
    if (j.contains("templateParams")) {
        for (const auto& pj : j["templateParams"]) {
            TemplateParamDecl p;
            detail::templateParamFromJson(pj, p);
            f.templateParams.push_back(std::move(p));
        }
    }
}

// ===================================================================
// Module (top-level)
// ===================================================================

nlohmann::json serializeModule(const TranspileModule& m) {
    json j = json::object();
    j["types"] = m.types;
    j["functions"] = m.functions;
    return j;
}

TranspileModule deserializeModule(const json& j) {
    TranspileModule m;
    j.at("types").get_to(m.types);
    j.at("functions").get_to(m.functions);
    return m;
}

} // namespace topo::transpile
