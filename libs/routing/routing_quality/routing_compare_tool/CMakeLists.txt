project(routing_compare_tool)
omim_add_executable(${PROJECT_NAME} routing_compare_tool.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE routes_builder routing platform coding geometry base gflags::gflags)
