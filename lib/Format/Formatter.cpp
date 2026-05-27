#include "topo/Format/Formatter.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace topo::format {

// ============================================================================
// Public API
// ============================================================================

std::string Formatter::format(const TopoFile& ast,
                              const std::string& source,
                              const std::vector<CommentToken>& comments) {
    output_.clear();
    source_ = &source;
    comments_ = &comments;
    nextCommentIdx_ = 0;
    lastWasBlank_ = false;
    rangeStart_ = -1;
    rangeEnd_ = -1;

    formatFile(ast);

    while (nextCommentIdx_ < comments_->size()) {
        const auto& c = (*comments_)[nextCommentIdx_++];
        emit(c.text);
        emitLine();
    }

    while (!output_.empty() && output_.back() == '\n') {
        output_.pop_back();
    }
    output_ += '\n';

    return output_;
}

std::string Formatter::formatRange(const TopoFile& ast,
                                   const std::string& source,
                                   const std::vector<CommentToken>& comments,
                                   int startLine,
                                   int endLine) {
    rangeStart_ = startLine;
    rangeEnd_ = endLine;
    return format(ast, source, comments);
}

// ============================================================================
// Output helpers
// ============================================================================

void Formatter::emit(const std::string& text) {
    output_ += text;
    lastWasBlank_ = false;
}

void Formatter::emitLine(const std::string& text) {
    if (!text.empty()) emit(text);
    output_ += '\n';
    lastWasBlank_ = text.empty() && (output_.size() >= 2 && output_[output_.size() - 2] == '\n');
}

void Formatter::emitIndent(int level) {
    for (int i = 0; i < level; ++i) {
        output_ += "    ";
    }
}

void Formatter::emitBlankLine() {
    if (!lastWasBlank_) {
        output_ += '\n';
        lastWasBlank_ = true;
    }
}

void Formatter::emitCommentsBefore(int line, int indentLevel) {
    while (nextCommentIdx_ < comments_->size()) {
        const auto& c = (*comments_)[nextCommentIdx_];
        if (c.location.line >= line) break;

        emitIndent(indentLevel);
        // Normalize: ensure "// " (space after //)
        std::string text = c.text;
        if (!c.isBlock && text.size() >= 2 && text[0] == '/' && text[1] == '/') {
            if (text.size() == 2) {
                // Empty comment: just "//"
            } else if (text[2] != ' ') {
                text = "// " + text.substr(2);
            }
        }
        emit(text);
        emitLine();
        ++nextCommentIdx_;
    }
}

void Formatter::emitEndOfLineComment(int line) {
    if (nextCommentIdx_ < comments_->size()) {
        const auto& c = (*comments_)[nextCommentIdx_];
        if (c.location.line == line) {
            // Normalize comment text
            std::string text = c.text;
            if (!c.isBlock && text.size() >= 2 && text[0] == '/' && text[1] == '/') {
                if (text.size() > 2 && text[2] != ' ') {
                    text = "// " + text.substr(2);
                }
            }
            emit("  " + text);
            ++nextCommentIdx_;
        }
    }
}

// ============================================================================
// File formatting
// ============================================================================

void Formatter::formatFile(const TopoFile& ast) {
    // Separate DataDecl nodes into: imports, std::imports, type aliases
    std::vector<const Import*> imports;
    std::vector<const DataDecl*> stdImports;
    std::vector<const DataDecl*> typeAliases;
    std::vector<const ASTNode*> rest;

    for (const auto& decl : ast.declarations) {
        if (decl->kind == ASTKind::Import) {
            imports.push_back(static_cast<const Import*>(decl.get()));
        } else if (decl->kind == ASTKind::DataDecl) {
            const auto& data = static_cast<const DataDecl&>(*decl);
            if (data.isStdImport()) {
                stdImports.push_back(&data);
            } else {
                typeAliases.push_back(&data);
            }
        } else {
            rest.push_back(decl.get());
        }
    }

    // Sort file imports alphabetically
    std::sort(imports.begin(), imports.end(), [](const Import* a, const Import* b) { return a->path < b->path; });

    // Sort std imports alphabetically by path
    std::sort(stdImports.begin(), stdImports.end(), [](const DataDecl* a, const DataDecl* b) {
        return a->importPath < b->importPath;
    });

    // Sort type aliases alphabetically
    std::sort(
        typeAliases.begin(), typeAliases.end(), [](const DataDecl* a, const DataDecl* b) { return a->name < b->name; });

    bool needBlank = false;

    if (!ast.declarations.empty()) {
        emitCommentsBefore(ast.declarations.front()->location.line, 0);
    }

    // File imports
    if (!imports.empty()) {
        for (const auto* imp : imports) {
            emitCommentsBefore(imp->location.line, 0);
            formatImport(*imp, 0);
        }
        needBlank = true;
    }

    // Std imports
    if (!stdImports.empty()) {
        if (needBlank) emitBlankLine();
        for (const auto* imp : stdImports) {
            emitCommentsBefore(imp->location.line, 0);
            formatStdImport(*imp, 0);
        }
        needBlank = true;
    }

    // Type aliases
    if (!typeAliases.empty()) {
        if (needBlank) emitBlankLine();
        for (const auto* ta : typeAliases) {
            emitCommentsBefore(ta->location.line, 0);
            formatTypeAlias(*ta, 0);
        }
        needBlank = true;
    }

    // Rest (namespaces, etc.)
    for (const auto* node : rest) {
        if (needBlank) emitBlankLine();
        emitCommentsBefore(node->location.line, 0);
        if (node->kind == ASTKind::NamespaceDecl) {
            formatNamespace(static_cast<const NamespaceDecl&>(*node), 0);
        }
        needBlank = true;
    }
}

// ============================================================================
// Declaration formatting
// ============================================================================

void Formatter::formatImport(const Import& imp, int indent) {
    emitIndent(indent);
    emit("import " + imp.path);
    if (!imp.selectedSymbols.empty()) {
        emit(" { ");
        for (size_t i = 0; i < imp.selectedSymbols.size(); ++i) {
            if (i > 0) emit(", ");
            emit(imp.selectedSymbols[i]);
        }
        emit(" }");
    }
    emit(";");
    emitEndOfLineComment(imp.location.line);
    emitLine();
}

void Formatter::formatStdImport(const DataDecl& imp, int indent) {
    emitIndent(indent);
    emit("std::import(\"" + imp.importPath + "\"");
    if (!imp.name.empty()) {
        emit(", " + imp.name);
    }
    emit(");");
    emitEndOfLineComment(imp.location.line);
    emitLine();
}

void Formatter::formatTypeAlias(const DataDecl& ta, int indent) {
    emitIndent(indent);
    emit("using " + ta.name + " = " + ta.type.toString() + ";");
    emitEndOfLineComment(ta.location.line);
    emitLine();
}

void Formatter::formatNamespace(const NamespaceDecl& ns, int indent) {
    emitIndent(indent);
    emit("namespace " + ns.pathString() + " {");
    emitEndOfLineComment(ns.location.line);
    emitLine();

    bool prevWasVisibility = false;
    for (size_t i = 0; i < ns.sections.size(); ++i) {
        const auto& section = ns.sections[i];
        if (section->kind == ASTKind::VisibilitySection) {
            if (i > 0) emitBlankLine();
            emitCommentsBefore(section->location.line, indent + 1);
            formatVisibilitySection(static_cast<const VisibilitySection&>(*section), indent);
            prevWasVisibility = true;
        } else if (section->kind == ASTKind::NamespaceDecl) {
            if (prevWasVisibility || i > 0) emitBlankLine();
            emitCommentsBefore(section->location.line, indent + 1);
            formatNamespace(static_cast<const NamespaceDecl&>(*section), indent + 1);
            prevWasVisibility = false;
        }
    }

    emitIndent(indent);
    emit("}");
    emitLine();
}

void Formatter::formatVisibilitySection(const VisibilitySection& vis, int nsIndent) {
    int labelIndent = nsIndent + 1;
    int memberIndent = nsIndent + 2;

    emitIndent(labelIndent);
    emit(std::string(visibilityName(vis.visibility)) + ":");
    emitEndOfLineComment(vis.location.line);
    emitLine();

    for (size_t i = 0; i < vis.members.size(); ++i) {
        const auto& member = vis.members[i];
        emitCommentsBefore(member->location.line, memberIndent);

        if (member->kind == ASTKind::FnDecl) {
            const auto& fn = static_cast<const FnDecl&>(*member);
            if (!fn.isConstructor && !fn.isDestructor && !fn.isOperator()) {
                formatFnDecl(fn, memberIndent);
                if (i + 1 < vis.members.size() && vis.members[i + 1]->kind == ASTKind::FnLogicBlock) {
                    // No blank line between decl and its fn block
                }
            } else {
                // Constructor, destructor, operator — handled later in type body
                formatFnDecl(fn, memberIndent);
            }
        } else if (member->kind == ASTKind::FnLogicBlock) {
            formatFnLogicBlock(static_cast<const FnLogicBlock&>(*member), memberIndent);
            if (i + 1 < vis.members.size()) {
                emitBlankLine();
            }
        } else if (member->kind == ASTKind::TypeDecl) {
            emitBlankLine();
            formatTypeDecl(static_cast<const TypeDecl&>(*member), memberIndent);
            if (i + 1 < vis.members.size()) {
                emitBlankLine();
            }
        } else if (member->kind == ASTKind::NamespaceDecl) {
            emitBlankLine();
            formatNamespace(static_cast<const NamespaceDecl&>(*member), memberIndent);
        } else if (member->kind == ASTKind::DataDecl) {
            const auto& data = static_cast<const DataDecl&>(*member);
            if (data.isStdImport()) {
                formatStdImport(data, memberIndent);
            } else if (data.isInstantiate()) {
                emitIndent(memberIndent);
                emit("instantiate " + data.type.toString() + ";");
                emitEndOfLineComment(data.location.line);
                emitLine();
            } else {
                formatTypeAlias(data, memberIndent);
            }
        } else if (member->kind == ASTKind::IfDecl) {
            formatComptimeIf(static_cast<const IfDecl&>(*member), memberIndent);
        }
    }
}

void Formatter::formatComptimeIf(const IfDecl& node, int indent) {
    emitIndent(indent);
    std::string cond = extractParenContent(node.location.line);
    emit("comptime if (" + cond + ") {");
    emitEndOfLineComment(node.location.line);
    emitLine();

    int bodyIndent = indent + 1;
    for (const auto& member : node.thenBody) {
        emitCommentsBefore(member->location.line, bodyIndent);
        formatComptimeIfMember(*member, bodyIndent);
    }

    emitIndent(indent);
    if (node.elseBody.empty()) {
        emit("}");
        emitLine();
        return;
    }

    emit("} else {");
    emitLine();
    for (const auto& member : node.elseBody) {
        emitCommentsBefore(member->location.line, bodyIndent);
        formatComptimeIfMember(*member, bodyIndent);
    }
    emitIndent(indent);
    emit("}");
    emitLine();
}

void Formatter::formatComptimeIfMember(const ASTNode& member, int indent) {
    switch (member.kind) {
        case ASTKind::FnDecl:
            formatFnDecl(static_cast<const FnDecl&>(member), indent);
            break;
        case ASTKind::FnLogicBlock:
            formatFnLogicBlock(static_cast<const FnLogicBlock&>(member), indent);
            break;
        case ASTKind::TypeDecl:
            formatTypeDecl(static_cast<const TypeDecl&>(member), indent);
            break;
        case ASTKind::NamespaceDecl:
            formatNamespace(static_cast<const NamespaceDecl&>(member), indent);
            break;
        case ASTKind::IfDecl:
            formatComptimeIf(static_cast<const IfDecl&>(member), indent);
            break;
        case ASTKind::DataDecl: {
            const auto& data = static_cast<const DataDecl&>(member);
            if (data.isStdImport()) {
                formatStdImport(data, indent);
            } else if (data.isInstantiate()) {
                emitIndent(indent);
                emit("instantiate " + data.type.toString() + ";");
                emitEndOfLineComment(data.location.line);
                emitLine();
            } else {
                formatTypeAlias(data, indent);
            }
            break;
        }
        default:
            break;
    }
}

void Formatter::formatFnDecl(const FnDecl& fn, int indent) {
    emitIndent(indent);

    std::string line;
    if (fn.isConstructor) {
        if (fn.isExplicit) line += "explicit ";
        line += fn.className + "(" + renderParams(fn.params) + ");";
    } else if (fn.isDestructor) {
        line += "~" + fn.className + "();";
    } else if (fn.isOperator()) {
        line += "fn operator" + std::string(overloadableOpName(*fn.operatorOp));
        line += "(" + renderParams(fn.params) + ")";
        line += " -> " + fn.returnType.toString() + ";";
    } else if (fn.isRustStyle) {
        line += fn.name + "(" + renderParams(fn.params) + ")";
        if (fn.isConst) line += " const";
        if (fn.isMultiReturn) {
            line += " -> (" + renderReturnParams(fn.returnParams) + ")";
        } else {
            line += " -> " + fn.returnType.toString();
        }
        // Render `with returns(a, _, _)` — names preserved in declared
        // order, wildcard positions rebuilt from the arity difference.
        if (fn.hasUsedReturnsClause && fn.isMultiReturn) {
            line += " with returns(";
            // Reconstruct in declared-param order: if a declared name
            // appears in declaredUsedReturns, emit it; otherwise emit `_`.
            std::set<std::string> named(fn.declaredUsedReturns.begin(), fn.declaredUsedReturns.end());
            for (size_t i = 0; i < fn.returnParams.size(); ++i) {
                if (i > 0) line += ", ";
                const auto& rp = fn.returnParams[i];
                line += named.count(rp.name) ? rp.name : std::string("_");
            }
            line += ")";
        }
        line += ";";
    } else {
        if (fn.hasModifier(ModifierData::Kind::External)) line += "external ";
        if (fn.isStatic) line += "static ";
        line += fn.returnType.toString() + " " + fn.name + "(";
        line += renderParams(fn.params) + ")";
        if (fn.isConst) line += " const";
        line += ";";
    }

    // Check if line exceeds column limit
    int totalLen = indent * 4 + static_cast<int>(line.size());
    bool multiLine = totalLen > 120 && !fn.params.empty() && !fn.isConstructor && !fn.isDestructor && !fn.isOperator();
    if (multiLine) {
        // Multi-line params
        if (fn.isRustStyle) {
            emit(fn.name + "(");
        } else {
            std::string prefix;
            if (fn.hasModifier(ModifierData::Kind::External)) prefix += "external ";
            if (fn.isStatic) prefix += "static ";
            emit(prefix + fn.returnType.toString() + " " + fn.name + "(");
        }
        emitLine();
        int paramIndent = indent + 1;
        for (size_t i = 0; i < fn.params.size(); ++i) {
            emitIndent(paramIndent);
            emit(fn.params[i].toString());
            if (i + 1 < fn.params.size()) emit(",");
            emitLine();
        }
        emitIndent(indent);
        emit(")");
        if (fn.isConst) emit(" const");
        if (fn.isRustStyle) {
            if (fn.isMultiReturn) {
                emit(" -> (" + renderReturnParams(fn.returnParams) + ")");
            } else {
                emit(" -> " + fn.returnType.toString());
            }
            if (fn.hasUsedReturnsClause && fn.isMultiReturn) {
                std::string clause = " with returns(";
                std::set<std::string> named(fn.declaredUsedReturns.begin(), fn.declaredUsedReturns.end());
                for (size_t i = 0; i < fn.returnParams.size(); ++i) {
                    if (i > 0) clause += ", ";
                    const auto& rp = fn.returnParams[i];
                    clause += named.count(rp.name) ? rp.name : std::string("_");
                }
                clause += ")";
                emit(clause);
            }
        }
        emit(";");
    } else {
        emit(line);
    }

    // A trailing end-of-line comment is attached only when the whole
    // declaration was emitted on a single line. For a multi-line param
    // list the comment that lexed on the input's signature line lands on
    // a *different* output line (the closing `);`) when the formatter's
    // own output is re-parsed — keying the attachment off the unstable
    // `fn.location.line` would consume it on the first pass and miss it
    // on the second, breaking idempotency. Leaving such comments pending
    // (they flush at the next stable drain point) keeps the round-trip a
    // fixed point.
    if (!multiLine) {
        emitEndOfLineComment(fn.location.line);
    }
    emitLine();
}

void Formatter::formatFnLogicBlock(const FnLogicBlock& fb, int indent) {
    emitIndent(indent);
    emit("fn " + fb.name + " {");
    emitEndOfLineComment(fb.location.line);
    emitLine();

    int bodyIndent = indent + 1;

    if (fb.isPipeline()) {
        for (const auto& edge : fb.pipelineEdges) {
            emitCommentsBefore(edge.loc.line, bodyIndent);
            formatPipelineEdge(edge, bodyIndent);
        }
    } else {
        for (const auto& op : fb.operations) {
            emitCommentsBefore(op->location.line, bodyIndent);
            if (op->kind == ASTKind::OperationDecl) {
                formatOperation(static_cast<const OperationDecl&>(*op), bodyIndent);
            }
        }
    }

    emitIndent(indent);
    emit("}");
    emitLine();
}

void Formatter::formatOperation(const OperationDecl& op, int indent) {
    emitIndent(indent);

    std::string line;
    if (op.stage >= 0) {
        line += "stage<" + std::to_string(op.stage) + "> ";
    }

    if (op.isAssignment()) {
        std::string srcLine = extractSourceLine(op.location.line);
        auto eqPos = srcLine.find('=');
        if (eqPos != std::string::npos) {
            std::string rhs = srcLine.substr(eqPos + 1);
            auto semiPos = rhs.rfind(';');
            if (semiPos != std::string::npos) {
                rhs = rhs.substr(0, semiPos);
            }
            size_t start = rhs.find_first_not_of(" \t");
            if (start != std::string::npos) {
                rhs = rhs.substr(start);
            }
            size_t end = rhs.find_last_not_of(" \t");
            if (end != std::string::npos) {
                rhs = rhs.substr(0, end + 1);
            }
            line += op.varName + " = " + rhs + ";";
        } else {
            line += op.varName + " = <expr>;";
        }
    } else {
        line += op.funcName + "()";
        if (op.returnBinding) {
            line += " -> ";
            if (op.returnBinding->isSingleValue) {
                line += op.returnBinding->singleName;
            } else {
                line += "(";
                for (size_t i = 0; i < op.returnBinding->targets.size(); ++i) {
                    if (i > 0) line += ", ";
                    line += op.returnBinding->targets[i].name;
                }
                line += ")";
            }
        }
        line += ";";
    }

    emit(line);
    emitEndOfLineComment(op.location.line);
    emitLine();
}

void Formatter::formatPipelineEdge(const PipelineEdge& edge, int indent) {
    emitIndent(indent);
    std::string line = edge.source + " -> ";
    if (edge.isTerminal) {
        line += edge.terminalType;
    } else {
        line += edge.target;
    }
    line += ";";
    emit(line);
    emitEndOfLineComment(edge.loc.line);
    emitLine();
}

// ============================================================================
// Parameter rendering
// ============================================================================

std::string Formatter::renderParams(const std::vector<Parameter>& params) {
    std::string result;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) result += ", ";
        result += params[i].toString();
    }
    return result;
}

std::string Formatter::renderReturnParams(const std::vector<ReturnParam>& params) {
    std::string result;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) result += ", ";
        result += params[i].toString();
    }
    return result;
}

// ============================================================================
// Source text extraction
// ============================================================================

std::string Formatter::extractSourceLine(int line) const {
    if (!source_) return "";
    std::istringstream stream(*source_);
    std::string lineStr;
    int currentLine = 0;
    while (std::getline(stream, lineStr)) {
        ++currentLine;
        if (currentLine == line) return lineStr;
    }
    return "";
}

// Extracts the text inside the first balanced `(...)` pair starting at or
// after `fromLine`, e.g. the condition of `comptime if (...) {`. Returns
// the inner text with leading/trailing whitespace trimmed and any
// embedded newlines collapsed to single spaces, so the re-emitted header
// is a stable single line that round-trips through the parser.
std::string Formatter::extractParenContent(int fromLine) const {
    if (!source_) return "";
    std::istringstream stream(*source_);
    std::string lineStr;
    int currentLine = 0;
    std::string joined;
    while (std::getline(stream, lineStr)) {
        ++currentLine;
        if (currentLine < fromLine) continue;
        if (!joined.empty()) joined += ' ';
        joined += lineStr;
        // Stop once the line containing the `(` plus its match is in view;
        // keep accumulating while no balanced close has been seen yet.
        int depth = 0;
        bool sawOpen = false;
        for (char c : joined) {
            if (c == '(') {
                ++depth;
                sawOpen = true;
            } else if (c == ')') {
                --depth;
            }
        }
        if (sawOpen && depth <= 0) break;
    }
    auto open = joined.find('(');
    if (open == std::string::npos) return "";
    int depth = 0;
    size_t close = std::string::npos;
    for (size_t i = open; i < joined.size(); ++i) {
        if (joined[i] == '(') {
            ++depth;
        } else if (joined[i] == ')') {
            --depth;
            if (depth == 0) {
                close = i;
                break;
            }
        }
    }
    if (close == std::string::npos || close <= open + 1) return "";
    std::string inner = joined.substr(open + 1, close - open - 1);
    size_t start = inner.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = inner.find_last_not_of(" \t");
    return inner.substr(start, end - start + 1);
}

void Formatter::formatTypeDecl(const TypeDecl& cls, int indent) {
    emitCommentsBefore(cls.location.line, indent);
    emitIndent(indent);

    // Constraint declaration
    if (cls.isConstraint()) {
        std::string header = "constraint " + cls.name;
        if (cls.parentConstraint) {
            header += " : " + *cls.parentConstraint;
        }
        header += " {";
        emit(header);
        emitEndOfLineComment(cls.location.line);
        emitLine();

        for (const auto& member : cls.constraintMembers) {
            emitIndent(indent + 1);
            if (member.isFunction) {
                std::string line = member.type.toString() + " " + member.name + "(";
                for (size_t i = 0; i < member.params.size(); ++i) {
                    if (i > 0) line += ", ";
                    line += member.params[i].toString();
                }
                line += ");";
                emit(line);
            } else {
                emit(member.type.toString() + " " + member.name + ";");
            }
            emitEndOfLineComment(member.location.line);
            emitLine();
        }

        emitIndent(indent);
        emit("}");
        emitLine();
        return;
    }

    // Adapt declaration
    if (cls.isAdapt()) {
        std::string header = "adapt " + cls.adaptConstraintName + " for " + cls.adaptTargetType.toString() + " {";
        emit(header);
        emitEndOfLineComment(cls.location.line);
        emitLine();

        for (const auto& mapping : cls.adaptMappings) {
            emitIndent(indent + 1);
            emit(mapping.memberName + " = " + mapping.targetName + ";");
            emitEndOfLineComment(mapping.location.line);
            emitLine();
        }

        emitIndent(indent);
        emit("}");
        emitLine();
        return;
    }

    // TypeFn declaration
    if (cls.isTypeFn()) {
        emit("typefn " + cls.name + "(typename " + cls.matchTarget + ") -> typename {");
        emitEndOfLineComment(cls.location.line);
        emitLine();

        emitIndent(indent + 1);
        emit("match " + cls.matchTarget + " {");
        emitLine();
        for (const auto& arm : cls.matchArms) {
            emitIndent(indent + 2);
            emit(arm.pattern.toString() + " => " + arm.result.toString() + ",");
            emitLine();
        }
        emitIndent(indent + 1);
        emit("}");
        emitLine();

        emitIndent(indent);
        emit("}");
        emitLine();
        return;
    }

    // Standard type declaration
    std::string header = "type " + cls.name;
    if (cls.baseClass) {
        header += " : public " + cls.baseClass->toString();
    }
    header += " {";
    emit(header);
    emitEndOfLineComment(cls.location.line);
    emitLine();

    int labelIndent = indent + 1;
    int memberIndent = indent + 2;

    for (const auto& section : cls.sections) {
        if (section->kind != ASTKind::VisibilitySection) continue;
        const auto& vis = static_cast<const VisibilitySection&>(*section);

        emitIndent(labelIndent);
        emit(std::string(visibilityName(vis.visibility)) + ":");
        emitEndOfLineComment(vis.location.line);
        emitLine();

        for (const auto& member : vis.members) {
            emitCommentsBefore(member->location.line, memberIndent);
            formatTypeMember(*member, memberIndent);
        }
    }

    emitIndent(indent);
    emit("}");
    emitLine();
}

void Formatter::formatTypeMember(const ASTNode& member, int indent) {
    if (member.kind == ASTKind::FnDecl) {
        const auto& fn = static_cast<const FnDecl&>(member);
        formatFnDecl(fn, indent);
    } else if (member.kind == ASTKind::DataDecl) {
        const auto& data = static_cast<const DataDecl&>(member);
        emitIndent(indent);
        std::string line;
        if (data.isStatic()) line += "static ";
        line += data.type.toString() + " " + data.name + ";";
        emit(line);
        emitEndOfLineComment(data.location.line);
        emitLine();
    }
}

} // namespace topo::format
