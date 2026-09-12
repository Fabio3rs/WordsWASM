
add_executable(utf8proc_destilation
    utf8proc_destilation/main.cpp
)

target_link_libraries(utf8proc_destilation
    PRIVATE utf8proc::utf8proc
)
