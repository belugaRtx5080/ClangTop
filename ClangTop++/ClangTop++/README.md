# ClangTop++ v0.2.0

**ClangTop++** is a modular, high-density C++17 TUI system & kernel monitor for Linux and BSD. It renders a `top`/`adbtop`-style dashboard using raw POSIX terminal control, 24-bit ANSI TrueColor, and Unicode Braille (`⠀..⣿`) / Block (`▁..█`) charts — with **zero external TUI libraries**.

## Features

- **7 chart-heavy tabs**
  1. **Overview** — 6-panel grid: CPU App vs Kernel (Braille), per-core mini heatmap, Memory/Cache stacked area, Power Watts & Battery V/Wh, Net RX/TX sparkline + session totals, Disk R/W + top processes.
  2. **Power** — battery model/V/A/Wh/cycles/health, multi-sensor wattage Braille chart & breakdown (RAPL `/sys/class/powercap`, hwmon, GPU, ACPI/BSD `sysctl`), sensor temperatures.
  3. **Kernel & IRQ** — IRQ/s chart & per-core spread (`/proc/interrupts`, BSD `vmstat -i`), kernel threads/daemons with exact per-process uptime, scheduler/ctx-switches, PSI stall latencies (`/proc/pressure/*`), Slab/PageTables & `/proc/slabinfo`.
  4. **CPU** — 5-panel layout: auto-scaling CPU usage (App/Kern/IO/IRQ, braille + stacked bars), “Now” stats (% of one core, kernel/peak/avg, busiest core, threads running/blocked, ctx/forks/page-faults rate, uptime, scheduler, thermal/freq), per-core load history matrix with live `%` + `GHz`, cluster frequency Braille chart, threads/processes table and aggregated process groups/thread pools.
  5. **Memory** — stacked Braille area (App RSS, Kernel/Slab, Page Cache/Buffers, Swap, Free), breakdown (Total/Avail/App/Cached/Buffers/Dirty/Swap/Pressure), top memory processes.
  6. **Net & Disk I/O** — bidirectional RX/TX speed chart, session & boot RX/TX totals (packets/errors/sockets), per-interface table, Disk R/W & queue latency chart, per-process and per-device disk I/O.
  7. **FS & Code** — mounted filesystems with df bars, async background `$HOME` source scan (`std::thread`) counting files/size and tokei-style LOC by language with share bars (skips `/proc,/sys,/dev,.git,build,node_modules,…`).
- **Crisp UI** — Unicode box borders with inline stats, multi-series Braille line/area charts, stacked bars, sparklines, heatmaps, badges.
- **Themes** — Gruvbox Dark (default, muted warm palette), Catppuccin Mocha, Nord, Tokyo Night.
- **Global formatting** — Unit Rounding (Auto/Bytes/KB/MB/GB) and Decimal Precision (0/1/2) applied to charts & tables; uptime formats (DD:HH:MM:SS, compact, `uptime -p` style); configurable refresh (250ms–2s).
- **Portable collectors** — Linux `/proc` + `/sys` with graceful fallbacks (BSD `sysctl` guarded where applicable). No external runtime dependencies.

## Build

### Requirements
- C++17 compiler: `clang++` (recommended) or `g++`
- CMake ≥ 3.16
- POSIX/Linux environment, pthreads

### CMake (recommended)
```bash
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Direct clang++
```bash
clang++ -std=c++17 -O2 -Wall -Wextra -pthread -Isrc \
  src/main.cpp src/collectors/*.cpp -o ClangTop++
```

## Run & verify
```bash
./build/ClangTop++ --version
./build/test_smoke
./build/ClangTop++                 # interactive TUI
./build/ClangTop++ --render-once=120x32   # 1-frame headless render
./build/ClangTop++ --render-all=160x40    # render all 7 tabs
./build/ClangTop++ --tab 3 --frames 3     # sample then render CPU tab
```

## Keybindings

| Key | Action |
| --- | --- |
| `1`-`7` | Switch page (Overview, Power, Kernel/IRQ, CPU, Memory, Net/IO, FS/Code) |
| `Tab` / `←` `→` | Cycle pages |
| `u` | Cycle uptime format (DD:HH:MM:SS / compact / uptime-p) |
| `b` | Cycle global unit rounding (Auto / Bytes / KB / MB / GB) |
| `t` | Cycle theme (Gruvbox / Catppuccin / Nord / Tokyo Night) |
| `s` | Cycle process sort (CPU% / MEM / PID / NAME / DISK / AGE) |
| `/` | Filter processes by name |
| `Space` | Pause / resume sampling |
| `,` | Settings (units, decimals, uptime, refresh, theme, code scan path) |
| `+` / `-` | Adjust refresh rate (250ms–2s) |
| `r` | Rescan code statistics |
| `?` | Toggle help |
| `q` / `Esc` | Quit / close overlay |

## Layout
```
├── CMakeLists.txt
├── src/{main.cpp, config.hpp}
├── src/core/{metrics_types.hpp, ring_buffer.hpp, unit_formatter.hpp, platform_collector.hpp}
├── src/collectors/{sys_kernel_collector.cpp, cpu_collector.cpp, power_collector.cpp, mem_io_net_collector.cpp, fs_code_scanner.cpp}
├── src/ui/{theme.hpp, terminal.hpp, canvas.hpp, widgets.hpp, views_system.hpp, views_resources.hpp}
└── tests/test_smoke.cpp
```

## License
MIT