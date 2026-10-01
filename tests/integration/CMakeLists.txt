cmake_minimum_required(VERSION 3.16)
project(tinyrt_integration_tests C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/HostWamr.cmake")
add_executable(test_integration test_integration.c ../store/fake_nor.c
    ../../runtime/store/tinyrt_store.c ../../runtime/store/tinyrt_directory.c
    ../../runtime/package/package.c ../../runtime/package/package_bcrypt.c
    ../../runtime/manager/tinyrt_manager.c)
target_include_directories(test_integration PRIVATE ../../runtime/include ../store
    "${WAMR_ROOT_DIR}/core/iwasm/include")
target_compile_definitions(test_integration PRIVATE
    PUBLIC_KEY_FILE="${CMAKE_CURRENT_LIST_DIR}/../package/development-public.sec1")
target_link_libraries(test_integration PRIVATE tinyrt_runtime bcrypt ws2_32)
if(MSVC)
    target_compile_options(test_integration PRIVATE /W4 /WX /utf-8 /wd4996)
endif()
enable_testing()
add_test(NAME signed_fixtures COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/make_packages.py" "${CMAKE_CURRENT_BINARY_DIR}/packages")
set_tests_properties(signed_fixtures PROPERTIES FIXTURES_SETUP signed_packages)
add_test(NAME integration COMMAND test_integration "${CMAKE_CURRENT_BINARY_DIR}/packages")
set_tests_properties(integration PROPERTIES FIXTURES_REQUIRED signed_packages TIMEOUT 60)
