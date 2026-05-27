#include "CppPlugin.h"
#include "RustPlugin.h"
#include "JavaPlugin.h"
#include "PythonPlugin.h"
#include "TypeScriptPlugin.h"

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    topo::lang::registerCppPlugin();
    topo::lang::registerRustPlugin();
    topo::lang::registerJavaPlugin();
    topo::lang::registerPythonPlugin();
    topo::lang::registerTypeScriptPlugin();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
