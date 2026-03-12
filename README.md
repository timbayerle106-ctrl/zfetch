# zfetch

A fast system information fetcher written in C. No ASCII art, just the info.

## Performance

| Metric | Value |
|--------|-------|
| Execution time | 19-52 ms |
| Binary size | ~26.8 KB |
| Memory | Stack only (no heap allocations) |

## Installation

### Build from source

```bash
git clone https://github.com/timbayerle106-ctrl/zfetch.git
cd zfetch
make
sudo make install
```

### Requirements

- GCC or Clang
- pthreads

## Usage

```bash
zfetch           # Show system info
zfetch --time    # Show system info with timing
zfetch --help    # Show help
```

## Supported Platforms

| Platform | Status |
|----------|--------|
| Linux | ✅ Tested |
| macOS | ⚠️ Not tested (should work) |
| Android/Termux | ⚠️ Not tested (code exists) |

## Displayed Information

- **User/Hostname**
- **OS/Distro**
- **Kernel version**
- **Uptime**
- **Shell**
- **DE/WM**
- **Terminal**
- **CPU** (model + core count)
- **GPU** (requires `lspci`)
- **Memory** (used/total)
- **Resolution** (requires `xrandr`)
- **Packages** (apt, dpkg, pacman)

## Output Example

```
user@hostname
────────────────────────────────────────
OS:      ubuntu
Distro:  Ubuntu 24.04 LTS
Kernel:  6.8.0-41-generic
Uptime:  2h 32m
Shell:   bash
DE/WM:   GNOME
Terminal: gnome-terminal
CPU:     8 cores (Intel(R) Core(TM) i5-10300H CPU @ 2.50GHz)
GPU:     NVIDIA Corporation GA107M [GeForce RTX 3050 Mobile]
Memory:  4.2 GB / 16.0 GB (26%)
Packages: dpkg (2847)

████████████████████████████
████████████████████████████
```

## How it Works

- Uses pthreads to collect system info in parallel
- Reads directly from `/proc` filesystem (Linux)
- Zero heap allocations - all data stored on stack
- Skips unavailable commands (lspci, xrandr) automatically

## Building for Different Platforms

```bash
# Linux (default)
make

# macOS
make  # Should work, not tested

# Android/Termux (run inside Termux)
pkg install clang
make

# Cross-compile for Android ARM64
make android-arm64  # Requires cross-compiler
```

## Unlicense

This is free and unencumbered software released into the public domain.

## Contributing

Found a bug? Open an issue or submit a pull request.

---

*Inspired by fastfetch and neofetch, but smaller and faster.*
*Ai was used for Documentation and Comments*
