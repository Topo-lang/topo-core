#include "topo/Platform/Process.h"
#include "topo/Transpile/ModelOptimizer.h"
#include "topo/Transpile/TranspileModelJson.h"
#include "CppEmitter.h"
#include "JavaEmitter.h"
#include "PythonEmitter.h"
#include "RustEmitter.h"
#include "V8Codegen.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace topo::transpile;

namespace {
ExprPtr variable(const std::string& name) {
    auto e = std::make_unique<VarRefExpr>(); e->name = name; return e;
}
ExprPtr loss(const std::string& description) {
    auto e = std::make_unique<UnsupportedExpr>(); e->description = description; return e;
}
StmtPtr expression(ExprPtr expr) {
    auto s = std::make_unique<ExprStmt>(); s->expr = std::move(expr); return s;
}
ExprPtr call(const std::string& name, ExprPtr receiver = {}) {
    auto e = std::make_unique<CallExpr>(); e->callee = name; e->receiver = std::move(receiver); return e;
}
std::unique_ptr<MethodReferenceExpr> boundReference() {
    auto ref = std::make_unique<MethodReferenceExpr>();
    ref->referenceKind = "bound"; ref->callee = "touch";
    ref->receiver = call("obtain"); ref->evaluateReceiverAtCreation = true;
    ref->targetType.nameParts = {"Runnable"}; return ref;
}
TranspileModule returning(ExprPtr expr, const std::string& returnType = "int") {
    TranspileModule module;
    TranspileFunction fn; fn.qualifiedName = "evaluate"; fn.returnType.nameParts = {returnType};
    auto ret = std::make_unique<ReturnStmt>(); ret->value = std::move(expr);
    fn.body.push_back(std::move(ret)); module.functions.push_back(std::move(fn)); return module;
}
std::vector<EmitResult> emitEverywhere(const TranspileModule& module) {
    return {CppEmitter{}.emit(module), RustEmitter{}.emit(module), JavaEmitter{}.emit(module),
            PythonEmitter{}.emit(module), V8Codegen{}.emit(module)};
}
} // namespace

TEST(TranspileJavaSemantics, CallReceiverAndSourceResolutionRoundTrip) {
    auto expr = call("value", call("next", call("obtain")));
    expr->sourceSpan = SourceSpan{"Example.java", 10, 38, 2, 4, 2, 32};
    expr->rawSource = "obtain().next().value()";
    expr->resolvedQualifiedName = "example.Owner.value";
    expr->resolvedSignature = "example.Owner.value():int";
    const auto json = serializeExpr(*expr);
    auto restored = deserializeExpr(json);
    EXPECT_EQ(serializeExpr(*restored), json);
    auto& outer = static_cast<const CallExpr&>(*restored);
    ASSERT_TRUE(outer.receiver);
    ASSERT_EQ(outer.receiver->kind(), Expr::Kind::Call);
    EXPECT_EQ(static_cast<const CallExpr&>(*outer.receiver).callee, "next");
}

TEST(TranspileJavaSemantics, LambdaAndMethodReferenceRoundTrip) {
    auto lambda = std::make_unique<LambdaExpr>();
    lambda->captures.push_back({"owner", CaptureMode::ByValue});
    lambda->targetType.nameParts = {"java", "util", "function", "Supplier"};
    lambda->returnType.nameParts = {"Runnable"};
    auto stmt = std::make_unique<ReturnStmt>(); stmt->value = boundReference();
    lambda->body.push_back(std::move(stmt));
    auto canonical = serializeExpr(*lambda);
    EXPECT_EQ(serializeExpr(*deserializeExpr(canonical)), canonical);
    for (const auto* kind : {"bound", "type", "super", "constructor"}) {
        auto ref = boundReference(); ref->referenceKind = kind;
        ref->ownerType.nameParts = {"example", "Owner"};
        ref->resolvedQualifiedName = "example.Owner.touch";
        ref->resolvedSignature = "example.Owner.touch():void";
        auto json = serializeExpr(*ref);
        EXPECT_EQ(serializeExpr(*deserializeExpr(json)), json);
    }
}

TEST(TranspileJavaSemantics, TypeInitializersAndAllSourceMetadataRoundTrip) {
    TranspileModule module;
    TranspileType type; type.qualifiedName = "Owner";
    type.sourceSpan = SourceSpan{"Owner.java", 0, 40, 1, 1, 3, 2}; type.rawSource = "class Owner {}";
    TranspileField field; field.name = "value"; field.type.nameParts = {"int"};
    field.init = call("initialValue"); field.sourceSpan = type.sourceSpan; field.rawSource = "int value = initialValue();";
    type.fields.push_back(field);
    auto statement = expression(loss("initializer"));
    statement->sourceSpan = type.sourceSpan; statement->rawSource = "initialize();";
    type.initializers.push_back(std::move(statement)); type.unsupported = {"initializer"};
    module.types.push_back(type);
    TranspileFunction fn; fn.qualifiedName = "Owner::work"; fn.sourceSpan = type.sourceSpan;
    fn.rawSource = "void work() {}"; fn.resolvedQualifiedName = "Owner.work"; fn.resolvedSignature = "Owner.work():void";
    module.functions.push_back(std::move(fn));
    const auto json = serializeModule(module);
    EXPECT_EQ(serializeModule(deserializeModule(json)), json);
}

TEST(TranspileJavaSemantics, VerificationCountsNestedLossAndDeduplicatesLegacySummaries) {
    auto lambda = std::make_unique<LambdaExpr>(); lambda->body.push_back(expression(loss("nested")));
    auto receiver = std::make_unique<CallExpr>(); receiver->callee = "bind";
    receiver->receiver = loss("receiver"); receiver->args.push_back(std::move(lambda));
    auto module = returning(std::move(receiver));
    auto& fn = module.functions.front(); fn.unsupported = {"nested", "receiver", "summary-only"};
    fn.body.push_back(expression(loss("nested")));
    auto verification = verifyModule(module);
    EXPECT_EQ(verification.totalUnsupported, 4);
    ASSERT_EQ(verification.perFunction.size(), 1u);
    EXPECT_EQ(verification.perFunction.front().constructs.size(), 4u);
    EXPECT_TRUE(applyVerificationGate(verification, 0).failed);
}

TEST(TranspileJavaSemantics, VerificationVisitsLoopsBranchesSwitchAndExceptionBodies) {
    TranspileModule module; TranspileFunction fn; fn.qualifiedName = "allBodies";
    auto loop = std::make_unique<ForStmt>();
    auto init = std::make_unique<VarDeclStmt>(); init->init = loss("init"); loop->init = std::move(init);
    loop->condition = loss("condition"); loop->increment = loss("increment");
    auto branch = std::make_unique<IfStmt>(); branch->condition = loss("branch");
    branch->thenBody.push_back(expression(loss("then"))); branch->elseBody.push_back(expression(loss("else")));
    loop->body.push_back(std::move(branch)); fn.body.push_back(std::move(loop));
    auto tc = std::make_unique<TryCatchStmt>(); tc->tryBody.push_back(expression(loss("try")));
    CatchClause catchClause; catchClause.body.push_back(expression(loss("catch")));
    tc->catchClauses.push_back(std::move(catchClause)); tc->finallyBody.push_back(expression(loss("finally")));
    fn.body.push_back(std::move(tc));
    auto sw = std::make_unique<SwitchStmt>(); sw->subject = loss("subject");
    SwitchCase clause; clause.value = loss("case"); clause.body.push_back(expression(loss("case-body")));
    sw->cases.push_back(std::move(clause)); fn.body.push_back(std::move(sw));
    auto loop2 = std::make_unique<WhileStmt>(); loop2->condition = loss("while");
    loop2->body.push_back(expression(loss("while-body"))); fn.body.push_back(std::move(loop2));
    module.functions.push_back(std::move(fn));
    EXPECT_EQ(verifyModule(module).totalUnsupported, 14);
}

TEST(TranspileJavaSemantics, VerificationVisitsFieldAndTypeInitializers) {
    TranspileModule module; TranspileType type; type.qualifiedName = "Owner";
    TranspileField field; field.name = "setting"; field.init = loss("field"); type.fields.push_back(field);
    auto ref = boundReference(); ref->receiver = loss("bound-receiver");
    type.initializers.push_back(expression(std::move(ref))); type.unsupported = {"field", "type-only"};
    module.types.push_back(type);
    const auto verification = verifyModule(module);
    EXPECT_EQ(verification.totalUnsupported, 3);
    ASSERT_EQ(verification.perFunction.size(), 1u);
    EXPECT_EQ(verification.perFunction.front().qualifiedName, "Owner::<initializers>");
}

TEST(TranspileJavaSemantics, EveryEmitterPreservesCompleteReceiverChain) {
    auto module = returning(call("value", call("next", call("obtain"))));
    for (const auto& output : emitEverywhere(module)) {
        EXPECT_TRUE(output.errors.empty());
        EXPECT_NE(output.code.find("obtain().next().value()"), std::string::npos) << output.code;
        EXPECT_EQ(output.code.find("obtain", output.code.find("obtain") + 1), std::string::npos) << output.code;
    }
}

TEST(TranspileJavaSemantics, JavaTypeQualifiedCallRemainsAValidQualifier) {
    auto module = returning(call("random", variable("Math")), "double");
    auto output = JavaEmitter{}.emit(module);
    EXPECT_TRUE(output.errors.empty());
    EXPECT_NE(output.code.find("Math.random()"), std::string::npos);
    EXPECT_EQ(output.code.find("(Math)"), std::string::npos);
}

TEST(TranspileJavaSemantics, ForeignEmittersRejectMethodReferences) {
    auto module = returning(boundReference(), "Runnable");
    for (const auto& output : {CppEmitter{}.emit(module), RustEmitter{}.emit(module),
                              PythonEmitter{}.emit(module), V8Codegen{}.emit(module)}) {
        ASSERT_EQ(output.errors.size(), 1u);
        EXPECT_EQ(output.errors.front().construct, "methodref");
    }
    auto java = JavaEmitter{}.emit(module);
    EXPECT_TRUE(java.errors.empty());
    EXPECT_NE(java.code.find("(obtain())::touch"), std::string::npos);
    EXPECT_EQ(java.code.find("->"), std::string::npos);
}

TEST(TranspileJavaSemantics, UnsupportedNodesRemainVisibleToTheVerificationThreshold) {
    auto lambda = std::make_unique<LambdaExpr>();
    auto nestedReturn = std::make_unique<ReturnStmt>(); nestedReturn->value = loss("nested-loss");
    lambda->body.push_back(std::move(nestedReturn));
    auto module = returning(std::move(lambda), "Runnable");
    for (const auto& output : emitEverywhere(module)) {
        EXPECT_TRUE(output.errors.empty());
        EXPECT_NE(output.code.find("nested-loss"), std::string::npos);
    }
    const auto verification = verifyModule(module);
    EXPECT_EQ(verification.totalUnsupported, 1);
    EXPECT_TRUE(applyVerificationGate(verification, 0).failed);
    EXPECT_FALSE(applyVerificationGate(verification, 1).failed);
}

TEST(TranspileJavaSemantics, PythonRejectsBlockLambdaInsteadOfDroppingItsBody) {
    auto lambda = std::make_unique<LambdaExpr>(); lambda->body.push_back(expression(call("sideEffect")));
    auto module = returning(std::move(lambda), "Runnable");
    EXPECT_EQ(verifyModule(module).totalUnsupported, 0);
    const auto emitted = PythonEmitter{}.emit(module);
    ASSERT_EQ(emitted.errors.size(), 1u);
    EXPECT_EQ(emitted.errors.front().construct, "lambda");
}

TEST(TranspileJavaSemantics, JavaRejectsMethodReferencesNeedingAnEnclosingInstance) {
    auto ref = boundReference(); ref->receiver = variable("this");
    auto module = returning(std::move(ref), "Runnable");
    ASSERT_EQ(JavaEmitter{}.emit(module).errors.size(), 1u);
    auto superRef = boundReference(); superRef->referenceKind = "super"; superRef->receiver.reset();
    auto superModule = returning(std::move(superRef), "Runnable");
    ASSERT_EQ(JavaEmitter{}.emit(superModule).errors.size(), 1u);
}

TEST(TranspileJavaSemantics, InitializersNeverSilentlyDisappearFromEmission) {
    TranspileModule module; TranspileType type; type.qualifiedName = "Owner";
    TranspileField field; field.name = "state"; field.type.nameParts = {"int"}; field.init = call("sideEffect");
    type.fields.push_back(field); type.initializers.push_back(expression(call("initialize")));
    module.types.push_back(type);
    for (const auto& output : emitEverywhere(module)) EXPECT_EQ(output.errors.size(), 2u);
}

TEST(TranspileJavaSemantics, OptimizerPreservesBoundReceiverEvaluationAndCaptureScope) {
    auto module = returning(boundReference(), "Runnable");
    auto declaration = std::make_unique<VarDeclStmt>(); declaration->name = "_tmp0";
    declaration->type.nameParts = {"int"}; declaration->init = call("beforeBinding");
    module.functions.front().body.insert(module.functions.front().body.begin(), std::move(declaration));
    const auto before = serializeModule(module);
    TempFoldingPass{}.run(module.functions.front()); NameHeuristicsPass{}.run(module.functions.front());
    EXPECT_EQ(serializeModule(module), before);
}

TEST(TranspileJavaSemantics, JavaBoundReceiverIsEvaluatedAndNullCheckedAtCreation) {
    const char* home = std::getenv("JAVA_HOME");
    auto javaTool = [&](const char* name) {
        return home ? (std::filesystem::path(home) / "bin" / name).string() : std::string(name);
    };
    if (topo::platform::runProcessCapture(javaTool("javac"), {"-version"}).exitCode != 0)
        GTEST_SKIP() << "A Java compiler is required for the receiver timing behavior test";
    struct Scratch {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("topo-methodref-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Scratch() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } scratch;
    std::filesystem::create_directories(scratch.path);
    auto module = returning(boundReference(), "Runnable");
    auto output = JavaEmitter{}.emit(module); ASSERT_TRUE(output.errors.empty());
    auto source = scratch.path / "Probe.java";
    std::ofstream file(source);
    file << "public class Probe { static int reads, actions; static boolean absent;\n"
         << "static Probe obtain() { reads++; return absent ? null : new Probe(); }\n"
         << "void touch() { actions++; }\n" << output.code
         << "public static void main(String[] args) { Runnable callback = evaluate();\n"
         << "if (reads != 1 || actions != 0) throw new AssertionError();\n"
         << "callback.run(); callback.run(); if (reads != 1 || actions != 2) throw new AssertionError();\n"
         << "absent = true; boolean threw = false; try { evaluate(); } catch (NullPointerException e) { threw = true; }\n"
         << "if (!threw || reads != 2) throw new AssertionError(); System.out.println(\"binding-ok\"); }}\n";
    file.close();
    auto compiled = topo::platform::runProcessCapture(javaTool("javac"), {"-d", scratch.path.string(), source.string()});
    ASSERT_EQ(compiled.exitCode, 0) << compiled.stderrOutput;
    auto run = topo::platform::runProcessCapture(javaTool("java"), {"-cp", scratch.path.string(), "Probe"});
    ASSERT_EQ(run.exitCode, 0) << run.stderrOutput;
    EXPECT_NE(run.stdoutOutput.find("binding-ok"), std::string::npos);
}
