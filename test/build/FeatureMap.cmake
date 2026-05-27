# topo-build/test/FeatureMap.cmake — Feature labels for topo-build tests

# Incremental Build
set_tests_properties(
    IncrementalCacheTest.FingerprintStability
    IncrementalCacheTest.FingerprintChangesWithCompiler
    IncrementalCacheTest.FingerprintChangesWithSources
    IncrementalCacheTest.FingerprintSourceOrderIndependent
    IncrementalCacheTest.FingerprintIgnoresOptLevel
    IncrementalCacheTest.ManifestRoundTrip
    IncrementalCacheTest.ManifestVersionMismatch
    IncrementalCacheTest.SymbolTableRoundTrip
    IncrementalCacheTest.VisibilityRoundTrip
    IncrementalCacheTest.CleanRemovesCache
    IncrementalCacheTest.MtimeValidityDetectsChange
    IncrementalCacheTest.PipelineAnalysisRoundTrip
    PROPERTIES LABELS "toolchain;build;incremental")

# ConfigValidator
set_tests_properties(
    ConfigValidator.ParallelDisabledIsValid
    PROPERTIES LABELS "toolchain;build")
