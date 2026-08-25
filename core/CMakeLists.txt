# libhitux-core -- the toolkit-free heart of HiTuxShare.
#
# HARD RULE: nothing in this directory may include a Qt header, or any other
# GUI toolkit header.  Everything here must be usable from the headless probe,
# from a unit test with no display, and from any future front-end.

add_library(hitux-core STATIC
    ApplicationSettings.cpp
    ChatCommandParser.cpp
    DownloadManager.cpp
    FileDownload.cpp
    FormatUtilities.cpp
    ServerConnection.cpp
    UserRegistry.cpp
)

target_include_directories(hitux-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${MUSCLE_INCLUDE_DIR}
)

target_link_libraries(hitux-core PUBLIC muscle)

# -Wno-multichar: the transfer message types are four-character constants
# ('tshr' and friends) because that is what BeShare put on the wire. The warning
# is right in general and wrong here.
target_compile_options(hitux-core PRIVATE -Wall -Wextra -Wno-multichar)
