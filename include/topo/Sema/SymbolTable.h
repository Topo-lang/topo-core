#ifndef TOPO_SEMA_SYMBOLTABLE_H
#define TOPO_SEMA_SYMBOLTABLE_H

#include "topo/AST/ASTNode.h"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace topo {

struct FunctionSymbol {
    std::string qualifiedName; // "engine::core::init"
    std::string simpleName;    // "init"
    Visibility visibility;
    SourceLocation location;
    TypeNode returnType;
    std::vector<Parameter> params;
    bool isConst = false;
    bool isStatic = false;
    bool hasLogicBlock = false;
    bool isMultiReturn = false;
    std::vector<ReturnParam> returnParams;
    // Selective-return ceiling declared via `with returns(a, _)`.
    // When populated, usedReturns at every call site is clamped to this
    // set — unnamed positions (written as `_`) are declared as
    // unobservable and are never added to any caller's usedReturns.
    // Empty set + hasUsedReturnsClause=false means "no declaration"
    // (every field observable by default).
    std::set<std::string> usedReturns;
    bool hasUsedReturnsClause = false;
    std::vector<TemplateParamDecl> templateParams;  // function templates
    std::optional<std::string> bindingTarget;       // e.g. "std::sort"
    PriorityLevel priority = PriorityLevel::Normal; // scheduling priority
    std::optional<CardinalityHint> cardinality;
    AccessPattern accessPattern = AccessPattern::None;
    int tiledSize = 0;
    bool isExternal = false;
};

struct MemberVarSymbol {
    std::string name;
    TypeNode type;
    bool isStatic = false;
    SourceLocation location;
};

struct OperatorSymbol {
    OverloadableOp op;
    std::vector<Parameter> params;
    TypeNode returnType;
    std::optional<std::string> bindingTarget;
    SourceLocation location;
};

struct ClassSymbol {
    std::string qualifiedName; // "engine::core::Vector"
    std::string simpleName;    // "Vector"
    Visibility visibility;
    SourceLocation location;
    std::optional<TypeNode> baseClass;
    std::vector<std::string> memberFunctions; // qualified names
    std::vector<std::string> constructors;    // qualified names
    std::string destructor;                   // qualified name (empty if none)
    std::vector<MemberVarSymbol> memberVars;
    std::vector<TemplateParamDecl> templateParams; // class templates
    std::vector<OperatorSymbol> operatorOverloads;
};

struct ConstraintSymbol {
    std::string qualifiedName; // "engine::Numeric"
    std::string simpleName;    // "Numeric"
    std::optional<std::string> parentConstraint;
    std::vector<ConstraintMember> members;
    SourceLocation location;
};

struct TypeAliasEntry {
    std::string name;
    TypeNode targetType;
    SourceLocation location;
};

struct InstantiateEntry {
    TypeNode type;             // e.g. Vector<Int>, with templateArgs
    std::string qualifiedName; // e.g. "container::Vector<Int>"
    SourceLocation location;
};

struct AdaptEntry {
    std::string constraintName;
    TypeNode targetType;
    std::vector<AdaptMapping> mappings;
    SourceLocation location;
};

struct LifetimeGroupEntry {
    std::string name;      // group name (e.g., "frame")
    std::string startFunc; // range start function
    std::string endFunc;   // range end function
    bool isOpenEnded = false;
    bool isSingleFunc = false;
    SourceLocation location;
    std::vector<std::string> scopeFunctions; // all functions covered by this scope (populated by analysis)
};

struct CallSiteInfo {
    std::string caller;                // qualified name of the enclosing fn block
    std::string callee;                // qualified name of the called function
    std::set<std::string> usedReturns; // names of return parameters actually consumed
    enum Style { Arrow, DotSelect, SmartPass, Full } style = Full;
    SourceLocation loc;
};

struct PipelineAnalysis {
    std::map<std::string, int> stages;    // node -> stage
    std::vector<std::string> sourceNodes; // nodes with no incoming edge
    std::string terminalNode;
    std::string terminalType;
    std::map<std::string, std::set<std::string>> demand; // node -> return values it requires
};

struct LogicBlockEntry {
    std::string qualifiedName; // "engine::core::run"
    std::string simpleName;    // "run"
    SourceLocation location;
    std::vector<std::string> calledFunctions; // operation-referenced function names
    std::vector<int> stages;                  // parallel array: stage per operation
    bool isPipeline = false;
    // True when this block came from a `flow` declaration. A flow is a
    // self-declaring pipeline (its name is the pipeline identity), so the
    // orphan-fn-block rule is waived for it. Inert to every checker.
    bool isFlow = false;
    std::vector<PipelineEdge> edges;
    std::optional<PipelineAnalysis> pipelineAnalysis;
};

struct ImportEntry {
    std::string path;
    std::string typeName;
    SourceLocation location;
};

// Debug metadata entries retained in the SymbolTable so
// downstream tools (the *.topo-dbg.json emitter, future Compute layer) can
// walk debug declarations without re-parsing the AST. Each entry stores the
// validated form: `targetQualifiedName` is the qualified name the entry
// resolves to (TypeDecl or DataDecl); `targetKind` records which.
enum class DebugTargetKind { Type, Data };

inline const char* debugTargetKindName(DebugTargetKind k) {
    switch (k) {
    case DebugTargetKind::Type: return "type";
    case DebugTargetKind::Data: return "data";
    }
    return "unknown";
}

struct DebugEntry {
    std::string targetTypeName;          // user-written name (unqualified)
    std::string targetQualifiedName;     // resolved qualified name after Sema
    DebugTargetKind targetKind = DebugTargetKind::Type;
    std::vector<DebugViewEntry> views;
    std::optional<std::string> summaryTemplate;
    std::vector<DebugInactiveEntry> inactiveRegions;
    std::vector<DebugRenderRaw> renderDecls; // transparent passthrough
    SourceLocation location;
};

class SymbolTable {
public:
    // Returns false if a duplicate was detected
    bool addFunction(const FunctionSymbol& sym);
    bool addTypeAlias(const TypeAliasEntry& entry);
    bool addLogicBlock(const LogicBlockEntry& entry);
    bool addClassSymbol(const ClassSymbol& sym);
    bool addConstraintSymbol(const ConstraintSymbol& sym);
    void addImport(const ImportEntry& entry);

    const FunctionSymbol* findFunction(const std::string& qualifiedName) const;
    const FunctionSymbol* findFunctionBySimpleName(const std::string& simpleName) const;
    const TypeAliasEntry* findTypeAlias(const std::string& name) const;
    const LogicBlockEntry* findLogicBlock(const std::string& qualifiedName) const;
    const ClassSymbol* findClassSymbol(const std::string& qualifiedName) const;
    const ClassSymbol* findClassBySimpleName(const std::string& simpleName) const;
    const ConstraintSymbol* findConstraintSymbol(const std::string& name) const;

    const std::unordered_map<std::string, FunctionSymbol>& functions() const { return functions_; }
    const std::unordered_map<std::string, TypeAliasEntry>& typeAliases() const { return typeAliases_; }
    const std::unordered_map<std::string, LogicBlockEntry>& logicBlocks() const { return logicBlocks_; }
    const std::vector<ImportEntry>& imports() const { return imports_; }
    const std::unordered_map<std::string, ClassSymbol>& classSymbols() const { return classSymbols_; }
    const std::unordered_map<std::string, ConstraintSymbol>& constraintSymbols() const { return constraintSymbols_; }

    void addCallSite(const CallSiteInfo& info) { callSites_.push_back(info); }
    const std::vector<CallSiteInfo>& callSites() const { return callSites_; }

    void addInstantiate(const InstantiateEntry& entry) { instantiates_.push_back(entry); }
    const std::vector<InstantiateEntry>& instantiates() const { return instantiates_; }

    void addAdapt(const AdaptEntry& entry) { adapts_.push_back(entry); }
    const std::vector<AdaptEntry>& adapts() const { return adapts_; }

    // Debug entries (one per `debug T { ... }` block).
    // Order is preserved (declaration order) so emitter output is stable.
    void addDebugEntry(DebugEntry entry) { debugEntries_.push_back(std::move(entry)); }
    const std::vector<DebugEntry>& debugEntries() const { return debugEntries_; }

    void addLifetimeGroup(const LifetimeGroupEntry& entry) { lifetimeGroups_.push_back(entry); }
    const std::vector<LifetimeGroupEntry>& lifetimeGroups() const { return lifetimeGroups_; }
    void setLifetimeGroupScopeFunctions(size_t index, std::vector<std::string> funcs) {
        if (index < lifetimeGroups_.size()) lifetimeGroups_[index].scopeFunctions = std::move(funcs);
    }
    const LifetimeGroupEntry* findLifetimeGroup(const std::string& name) const {
        for (const auto& g : lifetimeGroups_) {
            if (g.name == name) return &g;
        }
        return nullptr;
    }

    // Find all adapt entries for a given constraint name
    std::vector<const AdaptEntry*> findAdaptsForConstraint(const std::string& constraintName) const {
        std::vector<const AdaptEntry*> result;
        for (const auto& a : adapts_) {
            if (a.constraintName == constraintName) {
                result.push_back(&a);
            }
        }
        return result;
    }

    // Merge symbols from another table (for cross-file import propagation).
    // When filterInternal is true, functions with Internal visibility are skipped.
    void mergeFrom(const SymbolTable& other, bool filterInternal = true);

    // Merge only selected symbols from another table.
    // Returns names from selectedSymbols that were not found in source.
    // When selectedSymbols is empty, delegates to mergeFrom().
    std::vector<std::string> mergeSelected(const SymbolTable& source,
                                           const std::vector<std::string>& selectedSymbols,
                                           bool filterInternal = true);

    // Check if a type name was imported via std::import
    bool isImportedType(const std::string& name) const;

private:
    std::unordered_map<std::string, FunctionSymbol> functions_;
    std::unordered_map<std::string, TypeAliasEntry> typeAliases_;
    std::unordered_map<std::string, LogicBlockEntry> logicBlocks_;
    std::unordered_map<std::string, ClassSymbol> classSymbols_;
    std::unordered_map<std::string, ConstraintSymbol> constraintSymbols_;
    std::vector<ImportEntry> imports_;
    std::vector<CallSiteInfo> callSites_;
    std::vector<InstantiateEntry> instantiates_;
    std::vector<AdaptEntry> adapts_;
    std::vector<LifetimeGroupEntry> lifetimeGroups_;
    std::vector<DebugEntry> debugEntries_;
};

} // namespace topo

#endif // TOPO_SEMA_SYMBOLTABLE_H
