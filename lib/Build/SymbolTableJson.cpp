#include "topo/Build/SymbolTableJson.h"

using json = nlohmann::json;

namespace topo {

// ===================================================================
// Enum serialization
// ===================================================================

void to_json(json& j, Visibility v) {
    switch (v) {
    case Visibility::Public: j = "public"; break;
    case Visibility::Protected: j = "protected"; break;
    case Visibility::Private: j = "private"; break;
    case Visibility::Internal: j = "internal"; break;
    case Visibility::Ignore: j = "ignore"; break;
    }
}

void from_json(const json& j, Visibility& v) {
    auto s = j.get<std::string>();
    if (s == "public")
        v = Visibility::Public;
    else if (s == "protected")
        v = Visibility::Protected;
    else if (s == "private")
        v = Visibility::Private;
    else if (s == "ignore")
        v = Visibility::Ignore;
    else
        v = Visibility::Internal;
}

void to_json(json& j, OwnershipKind k) {
    switch (k) {
    case OwnershipKind::None: j = "none"; break;
    case OwnershipKind::Owned: j = "owned"; break;
    case OwnershipKind::Shared: j = "shared"; break;
    case OwnershipKind::Weak: j = "weak"; break;
    }
}

void from_json(const json& j, OwnershipKind& k) {
    auto s = j.get<std::string>();
    if (s == "owned")
        k = OwnershipKind::Owned;
    else if (s == "shared")
        k = OwnershipKind::Shared;
    else if (s == "weak")
        k = OwnershipKind::Weak;
    else
        k = OwnershipKind::None;
}

void to_json(json& j, AccessPattern p) {
    switch (p) {
    case AccessPattern::None: j = "none"; break;
    case AccessPattern::Streaming: j = "streaming"; break;
    case AccessPattern::Random: j = "random"; break;
    case AccessPattern::Tiled: j = "tiled"; break;
    case AccessPattern::GatherScatter: j = "gather_scatter"; break;
    }
}

void from_json(const json& j, AccessPattern& p) {
    auto s = j.get<std::string>();
    if (s == "streaming")
        p = AccessPattern::Streaming;
    else if (s == "random")
        p = AccessPattern::Random;
    else if (s == "tiled")
        p = AccessPattern::Tiled;
    else if (s == "gather_scatter")
        p = AccessPattern::GatherScatter;
    else
        p = AccessPattern::None;
}

void to_json(json& j, PriorityLevel p) {
    switch (p) {
    case PriorityLevel::Critical: j = "critical"; break;
    case PriorityLevel::High: j = "high"; break;
    case PriorityLevel::Normal: j = "normal"; break;
    case PriorityLevel::Low: j = "low"; break;
    case PriorityLevel::Background: j = "background"; break;
    }
}

void from_json(const json& j, PriorityLevel& p) {
    auto s = j.get<std::string>();
    if (s == "critical")
        p = PriorityLevel::Critical;
    else if (s == "high")
        p = PriorityLevel::High;
    else if (s == "low")
        p = PriorityLevel::Low;
    else if (s == "background")
        p = PriorityLevel::Background;
    else
        p = PriorityLevel::Normal;
}

void to_json(json& j, TypeNode::Modifier m) {
    switch (m) {
    case TypeNode::None: j = "none"; break;
    case TypeNode::Ref: j = "ref"; break;
    case TypeNode::Ptr: j = "ptr"; break;
    }
}

void from_json(const json& j, TypeNode::Modifier& m) {
    auto s = j.get<std::string>();
    if (s == "ref")
        m = TypeNode::Ref;
    else if (s == "ptr")
        m = TypeNode::Ptr;
    else
        m = TypeNode::None;
}

void to_json(json& j, CallSiteInfo::Style s) {
    switch (s) {
    case CallSiteInfo::Arrow: j = "arrow"; break;
    case CallSiteInfo::DotSelect: j = "dotselect"; break;
    case CallSiteInfo::SmartPass: j = "smartpass"; break;
    case CallSiteInfo::Full: j = "full"; break;
    }
}

void from_json(const json& j, CallSiteInfo::Style& s) {
    auto str = j.get<std::string>();
    if (str == "arrow")
        s = CallSiteInfo::Arrow;
    else if (str == "dotselect")
        s = CallSiteInfo::DotSelect;
    else if (str == "smartpass")
        s = CallSiteInfo::SmartPass;
    else
        s = CallSiteInfo::Full;
}

void to_json(json& j, TemplateParamDecl::Kind k) {
    switch (k) {
    case TemplateParamDecl::TypeParam: j = "type"; break;
    case TemplateParamDecl::NonTypeParam: j = "nontype"; break;
    case TemplateParamDecl::TemplateTemplateParam: j = "template"; break;
    case TemplateParamDecl::LifetimeParam: j = "lifetime"; break;
    }
}

void from_json(const json& j, TemplateParamDecl::Kind& k) {
    auto s = j.get<std::string>();
    if (s == "nontype")
        k = TemplateParamDecl::NonTypeParam;
    else if (s == "template")
        k = TemplateParamDecl::TemplateTemplateParam;
    else if (s == "lifetime")
        k = TemplateParamDecl::LifetimeParam;
    else
        k = TemplateParamDecl::TypeParam;
}

// ===================================================================
// TypeNode (recursive due to templateArgs)
// ===================================================================

void to_json(json& j, const TypeNode& t) {
    j = json::object();
    j["nameParts"] = t.nameParts;
    if (t.isConst) j["isConst"] = true;
    if (t.ownership != OwnershipKind::None) j["ownership"] = t.ownership;
    if (t.modifier != TypeNode::None) j["modifier"] = t.modifier;
    if (!t.templateArgs.empty()) j["templateArgs"] = t.templateArgs;
    if (t.isTemplateParam) j["isTemplateParam"] = true;
    if (t.isVariadic) j["isVariadic"] = true;
    if (t.nonTypeValue) j["nonTypeValue"] = *t.nonTypeValue;
}

void from_json(const json& j, TypeNode& t) {
    t.nameParts = j.at("nameParts").get<std::vector<std::string>>();
    t.isConst = j.value("isConst", false);
    t.ownership = j.value("ownership", OwnershipKind::None);
    t.modifier = j.value("modifier", TypeNode::None);
    if (j.contains("templateArgs")) t.templateArgs = j["templateArgs"].get<std::vector<TypeNode>>();
    t.isTemplateParam = j.value("isTemplateParam", false);
    t.isVariadic = j.value("isVariadic", false);
    if (j.contains("nonTypeValue")) t.nonTypeValue = j["nonTypeValue"].get<int>();
}

// ===================================================================
// Parameter types
// ===================================================================

void to_json(json& j, const Parameter& p) {
    j = json::object();
    j["type"] = p.type;
    j["name"] = p.name;
}

void from_json(const json& j, Parameter& p) {
    j.at("type").get_to(p.type);
    j.at("name").get_to(p.name);
}

void to_json(json& j, const ReturnParam& p) {
    j = json::object();
    j["type"] = p.type;
    j["name"] = p.name;
}

void from_json(const json& j, ReturnParam& p) {
    j.at("type").get_to(p.type);
    j.at("name").get_to(p.name);
}

void to_json(json& j, const PipelineEdge& e) {
    j = json::object();
    j["source"] = e.source;
    j["target"] = e.target;
    if (!e.terminalType.empty()) j["terminalType"] = e.terminalType;
    if (e.isTerminal) j["isTerminal"] = true;
}

void from_json(const json& j, PipelineEdge& e) {
    j.at("source").get_to(e.source);
    j.at("target").get_to(e.target);
    e.terminalType = j.value("terminalType", "");
    e.isTerminal = j.value("isTerminal", false);
}

void to_json(json& j, const TemplateParamDecl& d) {
    j = json::object();
    j["kind"] = d.kind;
    j["name"] = d.name;
    j["constraintType"] = d.constraintType;
    if (d.isVariadic) j["isVariadic"] = true;
    if (!d.innerParams.empty()) j["innerParams"] = d.innerParams;
    if (d.defaultType) j["defaultType"] = *d.defaultType;
}

void from_json(const json& j, TemplateParamDecl& d) {
    j.at("kind").get_to(d.kind);
    j.at("name").get_to(d.name);
    j.at("constraintType").get_to(d.constraintType);
    d.isVariadic = j.value("isVariadic", false);
    if (j.contains("innerParams")) d.innerParams = j["innerParams"].get<std::vector<TemplateParamDecl>>();
    if (j.contains("defaultType")) d.defaultType = j["defaultType"].get<TypeNode>();
}

// ===================================================================
// Symbol types
// ===================================================================

void to_json(json& j, const FunctionSymbol& s) {
    j = json::object();
    j["qualifiedName"] = s.qualifiedName;
    j["simpleName"] = s.simpleName;
    j["visibility"] = s.visibility;
    j["returnType"] = s.returnType;
    j["params"] = s.params;
    if (s.isConst) j["isConst"] = true;
    if (s.isStatic) j["isStatic"] = true;
    if (s.isExternal) j["isExternal"] = true;
    if (s.hasLogicBlock) j["hasLogicBlock"] = true;
    if (s.isMultiReturn) {
        j["isMultiReturn"] = true;
        j["returnParams"] = s.returnParams;
    }
    // Selective-return clause state (`with returns(a, _)`).  Serialize
    // only when the clause was present, so legacy JSON round-trips
    // cleanly and consumers can tell "no clause" from "empty-named
    // clause" (the latter is actually rejected by the parser, but
    // preserving the bit is still useful for downstream verifiers).
    if (s.hasUsedReturnsClause) {
        j["hasUsedReturnsClause"] = true;
        json ur = json::array();
        for (const auto& n : s.usedReturns) ur.push_back(n);
        j["usedReturns"] = ur;
    }
    if (!s.templateParams.empty()) j["templateParams"] = s.templateParams;
    if (s.bindingTarget) j["bindingTarget"] = *s.bindingTarget;
    if (s.cardinality) {
        json card = json::object();
        card["min"] = s.cardinality->min;
        card["max"] = s.cardinality->max;
        j["cardinality"] = card;
    }
    if (s.accessPattern != AccessPattern::None) j["accessPattern"] = s.accessPattern;
    if (s.tiledSize > 0) j["tiledSize"] = s.tiledSize;
    // @priority drives scheduling on the LLVM parallel backend; serialize it
    // so declared priorities survive the symbol-table round-trip.
    if (s.priority != PriorityLevel::Normal) j["priority"] = s.priority;
}

void from_json(const json& j, FunctionSymbol& s) {
    j.at("qualifiedName").get_to(s.qualifiedName);
    j.at("simpleName").get_to(s.simpleName);
    j.at("visibility").get_to(s.visibility);
    j.at("returnType").get_to(s.returnType);
    j.at("params").get_to(s.params);
    s.isConst = j.value("isConst", false);
    s.isStatic = j.value("isStatic", false);
    s.isExternal = j.value("isExternal", false);
    s.hasLogicBlock = j.value("hasLogicBlock", false);
    s.isMultiReturn = j.value("isMultiReturn", false);
    if (j.contains("returnParams")) j["returnParams"].get_to(s.returnParams);
    s.hasUsedReturnsClause = j.value("hasUsedReturnsClause", false);
    s.usedReturns.clear();
    if (j.contains("usedReturns")) {
        for (const auto& n : j["usedReturns"]) s.usedReturns.insert(n.get<std::string>());
    }
    if (j.contains("templateParams")) j["templateParams"].get_to(s.templateParams);
    if (j.contains("bindingTarget")) s.bindingTarget = j["bindingTarget"].get<std::string>();
    if (j.contains("cardinality")) {
        CardinalityHint card;
        card.min = j["cardinality"].value("min", (int64_t)-1);
        card.max = j["cardinality"].value("max", (int64_t)-1);
        s.cardinality = card;
    }
    s.accessPattern = j.value("accessPattern", AccessPattern::None);
    s.tiledSize = j.value("tiledSize", 0);
    s.priority = j.value("priority", PriorityLevel::Normal);
}

void to_json(json& j, const MemberVarSymbol& s) {
    j = json::object();
    j["name"] = s.name;
    j["type"] = s.type;
    if (s.isStatic) j["isStatic"] = true;
}

void from_json(const json& j, MemberVarSymbol& s) {
    j.at("name").get_to(s.name);
    j.at("type").get_to(s.type);
    s.isStatic = j.value("isStatic", false);
}

void to_json(json& j, const ClassSymbol& s) {
    j = json::object();
    j["qualifiedName"] = s.qualifiedName;
    j["simpleName"] = s.simpleName;
    j["visibility"] = s.visibility;
    if (s.baseClass) j["baseClass"] = *s.baseClass;
    j["memberFunctions"] = s.memberFunctions;
    j["constructors"] = s.constructors;
    if (!s.destructor.empty()) j["destructor"] = s.destructor;
    if (!s.memberVars.empty()) j["memberVars"] = s.memberVars;
    if (!s.templateParams.empty()) j["templateParams"] = s.templateParams;
}

void from_json(const json& j, ClassSymbol& s) {
    j.at("qualifiedName").get_to(s.qualifiedName);
    j.at("simpleName").get_to(s.simpleName);
    j.at("visibility").get_to(s.visibility);
    if (j.contains("baseClass")) s.baseClass = j["baseClass"].get<TypeNode>();
    j.at("memberFunctions").get_to(s.memberFunctions);
    j.at("constructors").get_to(s.constructors);
    s.destructor = j.value("destructor", "");
    if (j.contains("memberVars")) j["memberVars"].get_to(s.memberVars);
    if (j.contains("templateParams")) j["templateParams"].get_to(s.templateParams);
}

void to_json(json& j, const ConstraintMember& m) {
    j = json::object();
    j["type"] = m.type;
    j["name"] = m.name;
    if (!m.params.empty()) j["params"] = m.params;
    j["isFunction"] = m.isFunction;
}

void from_json(const json& j, ConstraintMember& m) {
    j.at("type").get_to(m.type);
    j.at("name").get_to(m.name);
    if (j.contains("params")) j["params"].get_to(m.params);
    j.at("isFunction").get_to(m.isFunction);
}

void to_json(json& j, const ConstraintSymbol& s) {
    j = json::object();
    j["qualifiedName"] = s.qualifiedName;
    j["simpleName"] = s.simpleName;
    if (s.parentConstraint) j["parentConstraint"] = *s.parentConstraint;
    j["members"] = s.members;
}

void from_json(const json& j, ConstraintSymbol& s) {
    j.at("qualifiedName").get_to(s.qualifiedName);
    j.at("simpleName").get_to(s.simpleName);
    if (j.contains("parentConstraint")) s.parentConstraint = j["parentConstraint"].get<std::string>();
    j.at("members").get_to(s.members);
}

void to_json(json& j, const TypeAliasEntry& e) {
    j = json::object();
    j["name"] = e.name;
    j["targetType"] = e.targetType;
}

void from_json(const json& j, TypeAliasEntry& e) {
    j.at("name").get_to(e.name);
    j.at("targetType").get_to(e.targetType);
}

void to_json(json& j, const ImportEntry& e) {
    j = json::object();
    j["path"] = e.path;
    j["typeName"] = e.typeName;
}

void from_json(const json& j, ImportEntry& e) {
    j.at("path").get_to(e.path);
    j.at("typeName").get_to(e.typeName);
}

void to_json(json& j, const PipelineAnalysis& a) {
    j = json::object();

    // stages: map<string, int>
    json stages = json::object();
    for (const auto& [node, stage] : a.stages)
        stages[node] = stage;
    j["stages"] = stages;

    j["sourceNodes"] = a.sourceNodes;
    j["terminalNode"] = a.terminalNode;
    j["terminalType"] = a.terminalType;

    // demand: map<string, set<string>> → object of arrays
    json demand = json::object();
    for (const auto& [node, fields] : a.demand) {
        json arr = json::array();
        for (const auto& f : fields)
            arr.push_back(f);
        demand[node] = arr;
    }
    j["demand"] = demand;
}

void from_json(const json& j, PipelineAnalysis& a) {
    for (const auto& [key, val] : j.at("stages").items())
        a.stages[key] = val.get<int>();
    j.at("sourceNodes").get_to(a.sourceNodes);
    j.at("terminalNode").get_to(a.terminalNode);
    j.at("terminalType").get_to(a.terminalType);
    for (const auto& [key, val] : j.at("demand").items()) {
        std::set<std::string> fields;
        for (const auto& f : val)
            fields.insert(f.get<std::string>());
        a.demand[key] = fields;
    }
}

void to_json(json& j, const LogicBlockEntry& e) {
    j = json::object();
    j["qualifiedName"] = e.qualifiedName;
    j["simpleName"] = e.simpleName;
    j["calledFunctions"] = e.calledFunctions;
    j["stages"] = e.stages;
    if (e.isPipeline) {
        j["isPipeline"] = true;
        j["edges"] = e.edges;
    }
    if (e.pipelineAnalysis) j["pipelineAnalysis"] = *e.pipelineAnalysis;
}

void from_json(const json& j, LogicBlockEntry& e) {
    j.at("qualifiedName").get_to(e.qualifiedName);
    j.at("simpleName").get_to(e.simpleName);
    j.at("calledFunctions").get_to(e.calledFunctions);
    j.at("stages").get_to(e.stages);
    e.isPipeline = j.value("isPipeline", false);
    if (j.contains("edges")) j["edges"].get_to(e.edges);
    if (j.contains("pipelineAnalysis")) e.pipelineAnalysis = j["pipelineAnalysis"].get<PipelineAnalysis>();
}

void to_json(json& j, const CallSiteInfo& c) {
    j = json::object();
    j["caller"] = c.caller;
    j["callee"] = c.callee;
    json ur = json::array();
    for (const auto& r : c.usedReturns)
        ur.push_back(r);
    j["usedReturns"] = ur;
    j["style"] = c.style;
}

void from_json(const json& j, CallSiteInfo& c) {
    j.at("caller").get_to(c.caller);
    j.at("callee").get_to(c.callee);
    c.usedReturns.clear();
    for (const auto& r : j.at("usedReturns"))
        c.usedReturns.insert(r.get<std::string>());
    j.at("style").get_to(c.style);
}

void to_json(json& j, const InstantiateEntry& e) {
    j = json::object();
    j["type"] = e.type;
    j["qualifiedName"] = e.qualifiedName;
}

void from_json(const json& j, InstantiateEntry& e) {
    j.at("type").get_to(e.type);
    j.at("qualifiedName").get_to(e.qualifiedName);
}

void to_json(json& j, const AdaptMapping& m) {
    j = json::object();
    j["memberName"] = m.memberName;
    j["targetName"] = m.targetName;
}

void from_json(const json& j, AdaptMapping& m) {
    j.at("memberName").get_to(m.memberName);
    j.at("targetName").get_to(m.targetName);
}

void to_json(json& j, const AdaptEntry& e) {
    j = json::object();
    j["constraintName"] = e.constraintName;
    j["targetType"] = e.targetType;
    j["mappings"] = e.mappings;
}

void from_json(const json& j, AdaptEntry& e) {
    j.at("constraintName").get_to(e.constraintName);
    j.at("targetType").get_to(e.targetType);
    j.at("mappings").get_to(e.mappings);
}

// ===================================================================
// Visibility entries
// ===================================================================

void to_json(json& j, const ParamConstInfo& p) {
    j = json::object();
    if (p.isConst) j["isConst"] = true;
    if (p.modifier != TypeNode::None) j["modifier"] = p.modifier;
    if (p.ownership != OwnershipKind::None) j["ownership"] = p.ownership;
}

void from_json(const json& j, ParamConstInfo& p) {
    p.isConst = j.value("isConst", false);
    p.modifier = j.value("modifier", TypeNode::None);
    p.ownership = j.value("ownership", OwnershipKind::None);
}

void to_json(json& j, const VisibilityEntry& e) {
    j = json::object();
    j["qualifiedName"] = e.qualifiedName;
    j["visibility"] = e.visibility;
    if (e.isConst) j["isConst"] = true;
    if (!e.paramConsts.empty()) j["paramConsts"] = e.paramConsts;
    if (e.bindingTarget) j["bindingTarget"] = *e.bindingTarget;
}

void from_json(const json& j, VisibilityEntry& e) {
    j.at("qualifiedName").get_to(e.qualifiedName);
    j.at("visibility").get_to(e.visibility);
    e.isConst = j.value("isConst", false);
    if (j.contains("paramConsts")) j["paramConsts"].get_to(e.paramConsts);
    if (j.contains("bindingTarget")) e.bindingTarget = j["bindingTarget"].get<std::string>();
}

// LifetimeGroupEntry
void to_json(json& j, const LifetimeGroupEntry& e) {
    j = json{{"name", e.name},
             {"startFunc", e.startFunc},
             {"endFunc", e.endFunc},
             {"isOpenEnded", e.isOpenEnded},
             {"isSingleFunc", e.isSingleFunc}};
    if (!e.scopeFunctions.empty()) {
        j["scopeFunctions"] = e.scopeFunctions;
    }
}

void from_json(const json& j, LifetimeGroupEntry& e) {
    j.at("name").get_to(e.name);
    j.at("startFunc").get_to(e.startFunc);
    if (j.contains("endFunc")) j["endFunc"].get_to(e.endFunc);
    if (j.contains("isOpenEnded")) j["isOpenEnded"].get_to(e.isOpenEnded);
    if (j.contains("isSingleFunc")) j["isSingleFunc"].get_to(e.isSingleFunc);
    if (j.contains("scopeFunctions")) j["scopeFunctions"].get_to(e.scopeFunctions);
}

// ===================================================================
// SymbolTable (top-level)
// ===================================================================

json serializeSymbolTable(const SymbolTable& symbols) {
    json j = json::object();

    // Functions
    json funcs = json::object();
    for (const auto& [name, fn] : symbols.functions())
        funcs[name] = fn;
    j["functions"] = funcs;

    // Logic blocks
    json blocks = json::object();
    for (const auto& [name, lb] : symbols.logicBlocks())
        blocks[name] = lb;
    j["logicBlocks"] = blocks;

    // Classes
    json classes = json::object();
    for (const auto& [name, cls] : symbols.classSymbols())
        classes[name] = cls;
    j["classes"] = classes;

    // Constraints
    json constraints = json::object();
    for (const auto& [name, c] : symbols.constraintSymbols())
        constraints[name] = c;
    j["constraints"] = constraints;

    // Type aliases
    json aliases = json::object();
    for (const auto& [name, a] : symbols.typeAliases())
        aliases[name] = a;
    j["typeAliases"] = aliases;

    // Imports
    j["imports"] = symbols.imports();

    // Call sites
    j["callSites"] = symbols.callSites();

    // Instantiates
    j["instantiates"] = symbols.instantiates();

    // Adapts
    j["adapts"] = symbols.adapts();

    // Lifetime groups
    j["lifetimeGroups"] = symbols.lifetimeGroups();

    return j;
}

bool deserializeSymbolTable(const json& j, SymbolTable& symbols) {
    try {
        // Functions
        if (j.contains("functions")) {
            for (const auto& [key, val] : j["functions"].items()) {
                auto fn = val.get<FunctionSymbol>();
                symbols.addFunction(fn);
            }
        }

        // Logic blocks
        if (j.contains("logicBlocks")) {
            for (const auto& [key, val] : j["logicBlocks"].items()) {
                auto lb = val.get<LogicBlockEntry>();
                symbols.addLogicBlock(lb);
            }
        }

        // Classes
        if (j.contains("classes")) {
            for (const auto& [key, val] : j["classes"].items()) {
                auto cls = val.get<ClassSymbol>();
                symbols.addClassSymbol(cls);
            }
        }

        // Constraints
        if (j.contains("constraints")) {
            for (const auto& [key, val] : j["constraints"].items()) {
                auto c = val.get<ConstraintSymbol>();
                symbols.addConstraintSymbol(c);
            }
        }

        // Type aliases
        if (j.contains("typeAliases")) {
            for (const auto& [key, val] : j["typeAliases"].items()) {
                auto a = val.get<TypeAliasEntry>();
                symbols.addTypeAlias(a);
            }
        }

        // Imports
        if (j.contains("imports")) {
            for (const auto& elem : j["imports"]) {
                auto imp = elem.get<ImportEntry>();
                symbols.addImport(imp);
            }
        }

        // Call sites
        if (j.contains("callSites")) {
            for (const auto& elem : j["callSites"]) {
                auto cs = elem.get<CallSiteInfo>();
                symbols.addCallSite(cs);
            }
        }

        // Instantiates
        if (j.contains("instantiates")) {
            for (const auto& elem : j["instantiates"]) {
                auto inst = elem.get<InstantiateEntry>();
                symbols.addInstantiate(inst);
            }
        }

        // Adapts
        if (j.contains("adapts")) {
            for (const auto& elem : j["adapts"]) {
                auto adp = elem.get<AdaptEntry>();
                symbols.addAdapt(adp);
            }
        }

        // Lifetime groups
        if (j.contains("lifetimeGroups")) {
            for (const auto& elem : j["lifetimeGroups"]) {
                auto lg = elem.get<LifetimeGroupEntry>();
                symbols.addLifetimeGroup(lg);
            }
        }

        return true;
    } catch (const json::exception&) {
        return false;
    }
}

} // namespace topo
