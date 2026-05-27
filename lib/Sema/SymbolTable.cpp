#include "topo/Sema/SymbolTable.h"

namespace topo {

bool SymbolTable::addFunction(const FunctionSymbol& sym) {
    auto [it, inserted] = functions_.emplace(sym.qualifiedName, sym);
    return inserted;
}

bool SymbolTable::addTypeAlias(const TypeAliasEntry& entry) {
    auto [it, inserted] = typeAliases_.emplace(entry.name, entry);
    return inserted;
}

bool SymbolTable::addLogicBlock(const LogicBlockEntry& entry) {
    auto [it, inserted] = logicBlocks_.emplace(entry.qualifiedName, entry);
    return inserted;
}

const FunctionSymbol* SymbolTable::findFunction(const std::string& qualifiedName) const {
    auto it = functions_.find(qualifiedName);
    if (it != functions_.end()) return &it->second;
    return nullptr;
}

const FunctionSymbol* SymbolTable::findFunctionBySimpleName(const std::string& simpleName) const {
    for (const auto& [qname, fn] : functions_) {
        if (fn.simpleName == simpleName) return &fn;
    }
    return nullptr;
}

const TypeAliasEntry* SymbolTable::findTypeAlias(const std::string& name) const {
    auto it = typeAliases_.find(name);
    if (it != typeAliases_.end()) return &it->second;
    return nullptr;
}

const LogicBlockEntry* SymbolTable::findLogicBlock(const std::string& qualifiedName) const {
    auto it = logicBlocks_.find(qualifiedName);
    if (it != logicBlocks_.end()) return &it->second;
    return nullptr;
}

bool SymbolTable::addClassSymbol(const ClassSymbol& sym) {
    auto [it, inserted] = classSymbols_.emplace(sym.qualifiedName, sym);
    return inserted;
}

const ClassSymbol* SymbolTable::findClassSymbol(const std::string& qualifiedName) const {
    auto it = classSymbols_.find(qualifiedName);
    if (it != classSymbols_.end()) return &it->second;
    return nullptr;
}

const ClassSymbol* SymbolTable::findClassBySimpleName(const std::string& simpleName) const {
    for (const auto& [qname, cls] : classSymbols_) {
        if (cls.simpleName == simpleName) return &cls;
    }
    return nullptr;
}

bool SymbolTable::addConstraintSymbol(const ConstraintSymbol& sym) {
    auto [it, inserted] = constraintSymbols_.emplace(sym.qualifiedName, sym);
    return inserted;
}

const ConstraintSymbol* SymbolTable::findConstraintSymbol(const std::string& name) const {
    // Try exact match first
    auto it = constraintSymbols_.find(name);
    if (it != constraintSymbols_.end()) return &it->second;
    // Try simple name match
    for (const auto& [qname, cs] : constraintSymbols_) {
        if (cs.simpleName == name) return &cs;
    }
    return nullptr;
}

void SymbolTable::addImport(const ImportEntry& entry) {
    imports_.push_back(entry);
}

void SymbolTable::mergeFrom(const SymbolTable& other, bool filterInternal) {
    for (const auto& [name, ta] : other.typeAliases()) {
        addTypeAlias(ta);
    }
    for (const auto& entry : other.imports()) {
        addImport(entry);
    }
    for (const auto& [name, fn] : other.functions()) {
        if (filterInternal && fn.visibility == Visibility::Internal) continue;
        addFunction(fn);
    }
    for (const auto& [name, lb] : other.logicBlocks()) {
        addLogicBlock(lb);
    }
    for (const auto& [name, cs] : other.classSymbols()) {
        if (filterInternal && cs.visibility == Visibility::Internal) continue;
        addClassSymbol(cs);
    }
    for (const auto& [name, cs] : other.constraintSymbols()) {
        addConstraintSymbol(cs);
    }
    for (const auto& inst : other.instantiates()) {
        addInstantiate(inst);
    }
    for (const auto& adapt : other.adapts()) {
        addAdapt(adapt);
    }
    for (const auto& cs : other.callSites()) {
        addCallSite(cs);
    }
    for (const auto& lg : other.lifetimeGroups()) {
        addLifetimeGroup(lg);
    }
    for (const auto& de : other.debugEntries()) {
        addDebugEntry(de);
    }
}

std::vector<std::string> SymbolTable::mergeSelected(const SymbolTable& source,
                                                    const std::vector<std::string>& selectedSymbols,
                                                    bool filterInternal) {
    if (selectedSymbols.empty()) {
        mergeFrom(source, filterInternal);
        return {};
    }

    // Always merge type aliases and std::import entries (needed for type resolution)
    for (const auto& [name, ta] : source.typeAliases()) {
        addTypeAlias(ta);
    }
    for (const auto& entry : source.imports()) {
        addImport(entry);
    }

    std::vector<std::string> unresolved;
    for (const auto& sym : selectedSymbols) {
        bool found = false;

        // Match against functions (by simpleName)
        for (const auto& [qname, fn] : source.functions()) {
            if (fn.simpleName == sym) {
                if (filterInternal && fn.visibility == Visibility::Internal) continue;
                addFunction(fn);
                found = true;
            }
        }

        // Match against class symbols (by simpleName)
        for (const auto& [qname, cs] : source.classSymbols()) {
            if (cs.simpleName == sym) {
                if (filterInternal && cs.visibility == Visibility::Internal) continue;
                addClassSymbol(cs);
                found = true;
            }
        }

        // Match against constraint symbols (by simpleName)
        for (const auto& [qname, cs] : source.constraintSymbols()) {
            if (cs.simpleName == sym) {
                addConstraintSymbol(cs);
                found = true;
            }
        }

        // Match against logic blocks (by simpleName)
        for (const auto& [qname, lb] : source.logicBlocks()) {
            if (lb.simpleName == sym) {
                addLogicBlock(lb);
                found = true;
            }
        }

        if (!found) {
            unresolved.push_back(sym);
        }
    }
    return unresolved;
}

bool SymbolTable::isImportedType(const std::string& name) const {
    for (const auto& imp : imports_) {
        if (imp.typeName == name) return true;
    }
    return false;
}

} // namespace topo
