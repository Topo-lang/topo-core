# topo-core/test/FeatureMap.cmake — Feature dimension labels for topo-core-tests.
# Extracted from the root test/FeatureMap.cmake during repo-split preparation.
#
# Only contains labels for tests built by topo-core-tests (zero LLVM dependency).
# Labels for topo-llvm, topo-runtime, topo-build, and topo-lang tests live in
# their respective component FeatureMap.cmake files.
#
# Mechanism: set_tests_properties applies feature labels at CTest time via
# TEST_INCLUDE_FILES. CTest silently ignores non-existent test names.

# --- Priority analysis ---
set_tests_properties(
    PriorityAnalysis.ExplicitPrioritiesPreserved
    PriorityAnalysis.PropagationFromCriticalCaller
    PriorityAnalysis.ExplicitOverridesPropagation
    PriorityAnalysis.NormalCallerNoUpgrade
    PriorityAnalysis.EmptySymbolTable
    PROPERTIES LABELS "toolchain;priority")

# --- Lifetime analysis ---
set_tests_properties(
    LifetimeAnalysis.BasicRangeResolvesToFunctions
    LifetimeAnalysis.SingleFuncScope
    LifetimeAnalysis.AnnotatedFunctionsDetected
    PROPERTIES LABELS "toolchain;lifetime")

# --- Priority — Parser ---
set_tests_properties(
    Parser.PrioritySectionBasic
    Parser.PrioritySectionAllLevels
    Parser.PriorityInvalidLevel
    PROPERTIES LABELS "toolchain;priority")

# --- Priority — Sema ---
set_tests_properties(
    Sema.PriorityPropagatedToSymbol
    PROPERTIES LABELS "toolchain;priority")

# --- Priority — VisibilityCollector ---
set_tests_properties(VisibilityCollector.PriorityCollected
    PROPERTIES LABELS "toolchain;priority")

# --- Priority — Lexer ---
set_tests_properties(Lexer.PriorityKeyword
    PROPERTIES LABELS "toolchain;priority")

# --- Data-aware hints — Parser ---
set_tests_properties(
    Parser.HintsRustStyleCardinalityRange
    Parser.HintsRustStyleAccessPattern
    Parser.HintsRustStyleVoidWithHints
    Parser.HintsCppStyleCardinality
    Parser.HintsTiledAccessWithSize
    Parser.HintsOpenEndedCardinality
    Parser.HintsBothCardinalityAndAccess
    Parser.HintsUnknownAccessPatternError
    Parser.HintsGatherScatterAccess
    PROPERTIES LABELS "toolchain;hints")

# --- VerificationOrchestrator ---
set_tests_properties(
    VerificationOrchestrator.StageIsolationWithNoLogicBlocks
    VerificationOrchestrator.StageIsolationWithSingleFunctionPerStage
    VerificationOrchestrator.StageIsolationWithMultipleFunctionsInStage
    PROPERTIES LABELS "toolchain;checker;stages")
set_tests_properties(
    VerificationOrchestrator.PipelineIndependenceWithNoPipelines
    PROPERTIES LABELS "toolchain;checker;pipeline")
set_tests_properties(
    VerificationOrchestrator.VerifyAllRunsBothChecks
    PROPERTIES LABELS "toolchain;checker")

# --- Syntax — Lexer ---
set_tests_properties(
    Lexer.Keywords
    Lexer.IntBoolStdAreIdentifiers
    Lexer.Identifiers
    Lexer.IntegerLiterals
    Lexer.StringLiterals
    Lexer.Operators
    Lexer.Delimiters
    Lexer.LineCommentSkipped
    Lexer.BlockCommentSkipped
    Lexer.UnterminatedBlockComment
    Lexer.UnterminatedString
    Lexer.IllegalCharacter
    Lexer.SourceLocationTracking
    Lexer.EmptyInput
    Lexer.SemicolonSeparator
    Lexer.ArithmeticOperators
    Lexer.ComparisonOperators
    Lexer.LogicalOperators
    Lexer.OperatorDisambiguation
    Lexer.MinusVsArrow
    Lexer.AmpVsAmpAmp
    Lexer.PreserveLineComment
    Lexer.PreserveBlockComment
    Lexer.PreserveCommentsDefaultOff
    Lexer.PreserveMultipleCommentsOrdered
    Lexer.CommentsAvailableAfterParsing
    Lexer.ClassKeywords
    Lexer.TildeToken
    Lexer.EllipsisToken
    Lexer.DotVsEllipsis
    Lexer.Phase4Keywords
    PROPERTIES LABELS "toolchain;syntax")

# --- Declarations — Parser + Sema ---
set_tests_properties(
    Parser.EmptyFile
    Parser.FileImport
    Parser.UsingDeclaration
    Parser.NamespacePath
    Parser.ThreeVisibilitySections
    Parser.CppStyleFunction
    Parser.RustStyleFunction
    Parser.ConstFunction
    Parser.PointerAndRefModifiers
    Parser.ErrorRecovery
    Parser.StdImportWithType
    Parser.StdImportWithoutType
    Parser.ClassRejectVirtual
    Parser.ClassRejectFunctionBody
    Parser.ClassRejectMemberInitializer
    PROPERTIES LABELS "toolchain;declarations")
set_tests_properties(
    Sema.DuplicateFunctionDecl
    Sema.BuiltinTypesPass
    Sema.BareIntFails
    Sema.UsingIntPass
    Sema.StdCpp17TypesPass
    Sema.UnknownTypeFails
    Sema.UsingAliasAllowsType
    Sema.StdImportMakesTypeValid
    Sema.StdImportRecorded
    PROPERTIES LABELS "toolchain;declarations")

# --- Multi-return — Parser + Sema ---
set_tests_properties(
    Parser.MultiReturnDeclaration
    Parser.MultiReturnSignature
    Parser.ReturnBindingSingleValue
    Parser.ReturnBindingDestructuring
    Parser.ReturnBindingWithStage
    PROPERTIES LABELS "toolchain;multireturn")
set_tests_properties(
    Sema.MultiReturnBasic
    Sema.MultiReturnDuplicateParamName
    Sema.MultiReturnTooFewParams
    PROPERTIES LABELS "toolchain;multireturn")

# --- Binding — VisibilityCollector ---
set_tests_properties(
    VisibilityCollector.BindingTargetExtracted
    VisibilityCollector.NoBindingTarget
    PROPERTIES LABELS "toolchain;declarations")
