# The Qt 6 front-end.
#
# Everything Qt-specific lives here.  The network and protocol code is in
# libhitux-core and knows nothing about widgets -- see core/CMakeLists.txt.

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(hitux
    ChatInputLine.cpp
    ChatLogView.cpp
    DesktopNotifier.cpp
    FileResultModel.cpp
    MainWindow.cpp
    SettingsDialog.cpp
    TransferModel.cpp
    UserListModel.cpp
    main.cpp
    resources.qrc
)

target_link_libraries(hitux
    PRIVATE
        hitux-core
        Qt6::Widgets
        Qt6::DBus
)

target_compile_options(hitux PRIVATE -Wall -Wextra -Wno-multichar)

install(TARGETS hitux RUNTIME DESTINATION bin)

# Desktop integration.  The scalable SVG is what modern desktops prefer; the PNGs
# cover themes and panels that still want fixed sizes.
install(FILES ${CMAKE_SOURCE_DIR}/packaging/hituxshare.desktop
    DESTINATION share/applications)

install(FILES ${CMAKE_SOURCE_DIR}/packaging/hituxshare.metainfo.xml
    DESTINATION share/metainfo)

install(FILES ${CMAKE_SOURCE_DIR}/assets/icon/hituxshare.svg
    DESTINATION share/icons/hicolor/scalable/apps)

foreach(ICON_SIZE 16 22 24 32 48 64 128 256)
    install(FILES ${CMAKE_SOURCE_DIR}/assets/icon/hituxshare-${ICON_SIZE}.png
        DESTINATION share/icons/hicolor/${ICON_SIZE}x${ICON_SIZE}/apps
        RENAME hituxshare.png)
endforeach()
