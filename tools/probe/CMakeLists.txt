# hitux-probe -- the headless Phase 0 client.
#
# It exists to prove the wire protocol against a real server without a GUI in the
# picture, so that any misbehaviour later is unambiguously a GUI or logic bug rather
# than a "is the wire format even right?" mystery.  It also doubles as a permanent
# regression client that can be run over SSH.

add_executable(hitux-probe main.cpp)

target_link_libraries(hitux-probe PRIVATE hitux-core)

target_compile_options(hitux-probe PRIVATE -Wall -Wextra -Wno-multichar)
