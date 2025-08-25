add_library(bhcoop STATIC cooperative.c)
target_include_directories(bhcoop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
