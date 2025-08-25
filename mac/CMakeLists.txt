cmake_minimum_required(VERSION 3.20)
project(blackhatos-mac-driver-stub LANGUAGES CXX)

add_executable(blackhatos-mac-driver-stub blackhat.cpp)

# Link against DriverKit framework
# target_link_libraries(blackhatos-mac-driver-stub PRIVATE -framework DriverKit)
