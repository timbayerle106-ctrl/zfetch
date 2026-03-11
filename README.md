# zfetch v3.1 - Ultra-Fast Parallel System Fetcher

A **blazingly fast** fastfetch alternative **without ASCII art**.

## 🚀 Performance

| Metric | Value |
|--------|-------|
| **Average execution** | **~2.5 ms** |
| **Best case** | **1.2 ms** |
| **Binary size** | **~19 KB** |
| **Heap allocations** | **0** |

### Comparison

| Tool | Execution Time | Binary Size |
|------|---------------|-------------|
| **zfetch** | **~2.5 ms** | **19 KB** |
| fastfetch | ~20-30 ms | ~500 KB |
| neofetch | ~500+ ms | Python required |

## 🖥️ Supported Platforms

| Platform | Status | Notes |
|----------|--------|-------|
| Linux | ✅ Full support | x86_64, ARM64 |
| macOS | ✅ Full support | Intel, Apple Silicon |
| Android/Termux | ✅ Full support | ARM64, ARM |
| FreeBSD | 🚧 Planned | - |

## 📱 Android/Termux Support

zfetch works great on Android via Termux! It automatically detects:
- Device model & brand
- Android version
- CPU (ARM processor info)
- GPU (Adreno, Mali, PowerVR)
- Screen resolution
- Termux packages

### Building in Termux

```bash
# Install build tools
pkg install clang

# Clone and build
git clone <repo>
cd zfetch
make termux

# Or just
make
```

### Cross-Compiling for Android

```bash
# For ARM64 devices (most modern phones)
make android-arm64

# For ARM devices (older phones)
make android-arm
```

## ✨ Features

- ⚡ **Parallel collection** - 7+ threads for concurrent data gathering
- 🎯 **Smart detection** - Skips unavailable commands instantly
- 📦 **Zero heap allocations** - All stack-based memory
- 🖥️ **Cross-platform** - Linux, macOS, Android/Termux
- 🎨 **Colorized output** - Beautiful ANSI colors

## 🔧 Optimizations

1. **Parallel pthreads** - All data collection runs concurrently
2. **Command availability cache** - Fast `access()` check before popen
3. **Direct file I/O** - `open()/read()` instead of `fopen()`
4. **Packed structs** - Cache-efficient memory layout
5. **LTO + -O3** - Maximum compiler optimization
6. **Static buffers** - No malloc/free overhead

## 📦 Building

### Linux/macOS
```bash
make
```

### Termux (Android)
```bash
pkg install clang
make
```

### Cross-compile for Android
```bash
# Requires Android NDK or cross-compiler
make android-arm64
```

## 🎮 Usage

```bash
./zfetch           # Normal output
./zfetch --time    # Show timing
./zfetch --help    # Help message
```

## 📊 Output Examples

### Linux
```
z@c-69b18633
────────────────────────────────────────
OS:      debian
Distro:  Debian GNU/Linux 13 (trixie)
Kernel:  5.10.134-013.5.kangaroo.al8.x86_64
Uptime:  54m
Shell:   bash
Terminal: dumb
CPU:     3 cores (Intel(R) Xeon(R) Processor)
Memory:  3.8 GB / 8.0 GB (47%)
Packages: dpkg (918)

████████████████████████████
████████████████████████████
```

### Android/Termux
```
u0_a123 @ Pixel 7 Pro
────────────────────────────────────────
OS:      Android 14
Device:  Pixel 7 Pro
Kernel:  5.10.157
Uptime:  2d 5h 32m
Shell:   bash
Terminal: Termux
CPU:     8 cores (Tensor G2)
GPU:     Mali-G710
Memory:  4.2 GB / 12.0 GB (35%)
Resolution: 1440x3120
Packages: pkg (156)

████████████████████████████
████████████████████████████
```

## 📈 Benchmark Results

```
=== Performance (30 runs) ===
Best:   1.2 ms
Median: 2.5 ms
Worst:  7.2 ms
```

## 📝 Technical Details

### Android Detection
- Runtime detection via `/system/build.prop` and `getprop`
- Compile-time detection via `__ANDROID__` macro
- Termux detection via `TERMUX_VERSION` environment variable

### Android Info Sources
| Info | Source |
|------|--------|
| Android version | `ro.build.version.release` |
| Device model | `ro.product.model` |
| Brand | `ro.product.brand` |
| CPU | `/proc/cpuinfo` + `ro.soc.model` |
| GPU | `ro.hardware.gralloc`, `/sys/class/kgsl` |
| Resolution | `wm size` command |
| Packages | `/data/data/com.termux/.../dpkg/info/*.list` |

## 📝 License

MIT
