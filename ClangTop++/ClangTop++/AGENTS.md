# ClangTop++ v0.0.1 Rules
- Language: Modern C++17 (ZERO external TUI libraries; raw POSIX termios, ANSI 24-bit TrueColor, Unicode Braille/Block charts).
- Compiler: clang++ -std=c++17 -O2 -Wall -Wextra -pthread (CMake supported via cmake -B build -DCMAKE_CXX_COMPILER=clang++ && cmake --build build).
- Verification: Compile every file after creation, run ./build/test_smoke and ./build/ClangTop++ --version.
- Layout: src/core/ (types, ring buffer, formatters), src/collectors/ (Linux /proc & /sys + BSD sysctl), src/ui/ (terminal, canvas, widgets, views), tests/.
