# The Qt 6 front-end.
#
# Everything Qt-specific lives here.  The network and protocol code is in
# libhitux-core and knows nothing about widgets -- see core/CMakeLists.txt.

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)

add_executable(hitux
    ChatInputLine.cpp
    ChatLogView.cpp
    MainWindow.cpp
    UserListModel.cpp
    main.cpp
)

target_link_libraries(hitux
    PRIVATE
        hitux-core
        Qt6::Widgets
)

target_compile_options(hitux PRIVATE -Wall -Wextra)

install(TARGETS hitux RUNTIME DESTINATION bin)
