#include <catch2/catch_test_macros.hpp>

#include <teximp/teximp.h>

#include <filesystem>

namespace fs = std::filesystem;

std::array gBitmapTestFiles = {
    fs::path(L"test1.bmp")
};

TEST_CASE("bitmap") {
}