cmake_minimum_required(VERSION 3.8)
project(TCP)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(TEST_TARGET_NAME ${CMAKE_PROJECT_NAME}_TEST)
enable_testing()
include_directories(src)

set(COMMON_DIR "/usr/local/lib/cmake/COMMON")
find_package(COMMON REQUIRED)

if (${CMAKE_SYSTEM_NAME} MATCHES "Android")
message("Finding android package: OpenSSL, Boost")
set(OPENSSL_ROOT_DIR "/home/debian/Desktop/android-openssl/openssl-3.0.0/dest/")
set(Boost_ROOT_DIR "/home/debian/Desktop/boost_1_75_0/dest/")
list(APPEND CMAKE_FIND_ROOT_PATH "${OPENSSL_ROOT_DIR}" "${Boost_ROOT_DIR}")
endif()
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS date_time)

set(nlohmann_json_DIR "/usr/local/lib/x86_64-linux-gnu/cmake/nlohmann_json")
find_package(nlohmann_json 3.2.0 REQUIRED)

add_subdirectory(src)
add_subdirectory(test)