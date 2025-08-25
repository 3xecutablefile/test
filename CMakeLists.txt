cmake_minimum_required(VERSION 3.20)
project(blackhatos-driver LANGUAGES C CXX)

# Windows driver (KMDF)
if(WIN32)
    add_subdirectory(win)
endif()

# macOS driver (kext / DriverKit)
if(APPLE)
    add_subdirectory(mac)
endif()

# Linux cooperative kernel hooks
add_subdirectory(kernel)

# Userland tools
add_subdirectory(user)
