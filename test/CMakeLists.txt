include_directories(${CMAKE_SOURCE_DIR}/src)

set(list_of_tests "socket")

foreach(test_var ${list_of_tests})
  add_executable(test_${test_var} test_${test_var}.cpp)
endforeach()

foreach(test_var ${list_of_tests})
  target_link_libraries(test_${test_var} gtest gtest_main)
  target_link_libraries(test_${test_var} CppSukat)
  add_test(NAME TEST-${test_var} COMMAND test_${test_var})
endforeach()
