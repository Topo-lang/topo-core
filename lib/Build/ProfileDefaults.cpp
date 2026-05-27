#include "topo/Build/BuildConfig.h"

namespace topo::build {

const std::map<std::string, ProfileOverrides>& builtinProfiles() {
    static const auto profiles = []() {
        std::map<std::string, ProfileOverrides> m;

        {
            ProfileOverrides p;
            p.buildMode = BuildMode::Dev;
            p.optLevel = OptLevel::O2;
            p.parallel = FeatureMode::Off;
            p.adaptive = FeatureMode::Off;
            p.dataLayout = FeatureMode::Off;
            p.indirection = FeatureMode::Force;
            p.observability = FeatureMode::Off;
            p.lifetime = FeatureMode::Off;
            p.loopParallel = FeatureMode::Off;
            m["embedded"] = p;
        }
        {
            ProfileOverrides p;
            p.buildMode = BuildMode::Aggressive;
            p.parallel = FeatureMode::Auto;
            p.adaptive = FeatureMode::Auto;
            p.dataLayout = FeatureMode::Auto;
            p.indirection = FeatureMode::Auto;
            p.observability = FeatureMode::Auto;
            p.lifetime = FeatureMode::Auto;
            p.loopParallel = FeatureMode::Auto;
            p.parallelInstrument = true;
            p.embedIR = true;
            m["server"] = p;
        }
        {
            ProfileOverrides p;
            p.buildMode = BuildMode::Dev;
            p.parallel = FeatureMode::Off;
            p.adaptive = FeatureMode::Off;
            p.dataLayout = FeatureMode::Off;
            p.indirection = FeatureMode::Force;
            p.observability = FeatureMode::Off;
            p.lifetime = FeatureMode::Off;
            p.loopParallel = FeatureMode::Off;
            m["wasm"] = p;
        }
        {
            ProfileOverrides p;
            p.buildMode = BuildMode::Dev;
            p.parallel = FeatureMode::Auto;
            p.adaptive = FeatureMode::Off;
            p.dataLayout = FeatureMode::Off;
            p.indirection = FeatureMode::Auto;
            p.observability = FeatureMode::Off;
            p.lifetime = FeatureMode::Off;
            p.loopParallel = FeatureMode::Off;
            m["desktop"] = p;
        }

        return m;
    }();
    return profiles;
}

} // namespace topo::build
