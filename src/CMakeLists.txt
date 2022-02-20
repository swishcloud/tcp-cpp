set(TARGET_NAME ${CMAKE_PROJECT_NAME})
set(OUTPUT_NAME ${TARGET_NAME}-${CMAKE_CXX_COMPILER_ID}-${OS_BIT})
string(TOUPPER ${OUTPUT_NAME} OUTPUT_NAME)
set(EXPORT_NAME ${OUTPUT_NAME})
file(GLOB_RECURSE SOURCES LIST_DIRECTORIES true *.cpp)
add_library(${TARGET_NAME} ${SOURCES})
set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME ${OUTPUT_NAME}-$<CONFIG:Debug>)
target_link_libraries( ${TARGET_NAME} PUBLIC COMMON::COMMON Boost::date_time nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
target_link_libraries(${TARGET_NAME} PUBLIC pthread)
endif()

# -lpthread -lstdc++fs
install(TARGETS ${TARGET_NAME} EXPORT ${EXPORT_NAME}
LIBRARY DESTINATION lib
ARCHIVE DESTINATION lib
RUNTIME DESTINATION bin
INCLUDES DESTINATION include
)



install(
  FILES
    "internal.h"
    "tcp.h"
  DESTINATION
    include/${TARGET_NAME}
)
install(
  FILES
  "../cmake/${TARGET_NAME}Config.cmake"
  DESTINATION
  lib/cmake/${TARGET_NAME}
)
install(EXPORT ${EXPORT_NAME} NAMESPACE ThirdPartyLibrary:: DESTINATION lib/cmake/${TARGET_NAME})