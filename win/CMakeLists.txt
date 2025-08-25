cmake_minimum_required(VERSION 3.20)
project(blackhatos-win-driver-stub LANGUAGES C)

add_executable(blackhatos-win-driver-stub blackhat.c)

# Link against ntdll for DbgPrint
target_link_libraries(blackhatos-win-driver-stub ntdll)
