add_library(CppSukat socket.cpp logging.cpp)
include_directories(${CMAKE_SOURCE_DIR}/include)

target_include_directories (CppSukat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
