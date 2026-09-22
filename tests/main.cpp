#include <logos_test.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

// init() writes ~/.logos_storage/config.json: without this, the tests overwrite the real one.
[[maybe_unused]] static const bool isolatedHome = [] {
    const std::filesystem::path home =
        std::filesystem::temp_directory_path() /
        ("logos-storage-tests-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    std::filesystem::create_directories(home);

    return setenv("HOME", home.c_str(), 1) == 0;
}();

LOGOS_TEST_MAIN()
