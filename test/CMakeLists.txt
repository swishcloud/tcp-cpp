add_executable(${TEST_TARGET_NAME} tcp_session_test.cpp)
target_link_libraries(${TEST_TARGET_NAME} COMMON::COMMON ${CMAKE_PROJECT_NAME})
add_test(NAME test COMMAND ${TEST_TARGET_NAME})