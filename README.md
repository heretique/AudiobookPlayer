# AudiobookPlayer (Playground project)

Small, audiobook player that uses `dear imgui` for interface and `libVLC`  for media player and metadata extraction.



The application uses `Cmake` as build system and `vcpkg` for fetching and building some of dependencies.

Dependencies from `vcpkg`:

-  dear imgui
- glad
- glfw3
- sqlite3

Dependencies included with the repository:

- libVlc (for now includes only Windows binaries and only x86, the reason application is build as x86)
- sqlite3pp
