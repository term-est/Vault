
enable_testing()


find_package(GTest REQUIRED)
find_package(cereal CONFIG REQUIRED)
add_executable(SerializerTests SerializerTests.cpp)
target_link_libraries(SerializerTests GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main range_v3 cereal::cereal)
target_include_directories(SerializerTests PUBLIC ${CMAKE_SOURCE_DIR})

include(GoogleTest)

gtest_discover_tests(SerializerTests)