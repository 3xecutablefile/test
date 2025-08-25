add_custom_target(user_scripts ALL
    COMMENT "Userland scripts are distributed but not built")

install(PROGRAMS blackhat.sh blackhat.ps1 DESTINATION bin)

add_executable(coop_console_test coop_console_test.c)
target_link_libraries(coop_console_test PRIVATE pthread)
install(TARGETS coop_console_test RUNTIME DESTINATION bin)
