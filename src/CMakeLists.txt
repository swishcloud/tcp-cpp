set(EXPORT_NAME ${CMAKE_PROJECT_NAME}-TARGETS)
file(GLOB_RECURSE SOURCES LIST_DIRECTORIES true *.cpp)
add_library(${CMAKE_PROJECT_NAME} ${SOURCES})
<<<<<<< HEAD
target_link_libraries( ${CMAKE_PROJECT_NAME} common::common -lpthread -lstdc++fs)

=======
target_link_libraries( ${CMAKE_PROJECT_NAME} common::common Boost::date_time nlohmann_json::nlohmann_json -lpthread -lstdc++fs)
>>>>>>> f9e0ee31cf380c3c1ea72151bd9a1b5c8e1e5cb3
install(TARGETS ${CMAKE_PROJECT_NAME} EXPORT ${EXPORT_NAME}
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
    include/${CMAKE_PROJECT_NAME}
)
install(
  FILES
  "../cmake/${CMAKE_PROJECT_NAME}Config.cmake"
  DESTINATION
  lib/cmake/${CMAKE_PROJECT_NAME}
)
install(EXPORT ${EXPORT_NAME} NAMESPACE ThirdPartyLibrary:: DESTINATION lib/cmake/${CMAKE_PROJECT_NAME})