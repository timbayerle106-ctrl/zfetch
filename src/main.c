/*
 * zfetch v3.2 - Ultra-Fast Parallel System Fetcher
 * Supports: Linux, macOS, Android/Termux
 * Target: <10ms execution time
 *
 * Fixes from v3.1:
 * - Fixed resolution showing DPI instead of actual screen resolution on Android
 * - Fixed uptime detection on Android/Termux (fallback methods)
 *
 * Key optimizations:
 * - Parallel thread collection
 * - Skip unavailable commands (fast path check)
 * - Direct file I/O instead of shell commands
 * - Zero heap allocations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <pwd.h>
#include <dirent.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

/* Platform detection */
#if defined(__ANDROID__)
#define PLATFORM_ANDROID 1
#elif defined(__APPLE__) && defined(__MACH__)
#define PLATFORM_MACOS 1
#define _DARWIN_C_SOURCE
#else
#define PLATFORM_LINUX 1
#endif

/* Maximum buffer sizes */
#define MAX_CPU 128
#define MAX_GPU 256

/* System info - packed for cache efficiency */
typedef struct __attribute__((packed)) {
    char username[64];
    char hostname[64];
    char os_name[64];
    char distro[128];
    char kernel[64];
    char shell[64];
    char de_wm[64];
    char terminal[32];
    char cpu[MAX_CPU];
    char gpu[MAX_GPU];
    char resolution[32];
    char pkg_manager[16];
    char device_model[64];  /* Android: device model */
    char android_ver[16];   /* Android: version */

    uint8_t cpu_cores;
    uint8_t has_gpu;
    uint8_t has_resolution;
    uint8_t is_android;
    uint8_t is_termux;
    uint64_t mem_total;
    uint64_t mem_used;
    uint32_t packages;
    uint32_t uptime_secs;
} SystemInfo;

/* Global flags for fast command availability check */
static int has_lspci = 0;
static int has_xrandr = 0;
static int has_getprop = 0;

/* High-resolution time in nanoseconds */
static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Fast file read - reads entire file */
static inline ssize_t read_file(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    
    ssize_t total = 0;
    ssize_t n;
    while ((n = read(fd, buf + total, size - 1 - total)) > 0) {
        total += n;
        if (total >= (ssize_t)(size - 1)) break;
    }
    close(fd);
    
    if (total > 0) buf[total] = '\0';
    return total;
}

/* Check if command exists */
static inline int cmd_exists(const char *cmd) {
    char path[256];
    /* Check common paths */
    const char *paths[] = {"/usr/bin", "/bin", "/system/bin", "/data/data/com.termux/files/usr/bin", NULL};
    for (int i = 0; paths[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", paths[i], cmd);
        if (access(path, X_OK) == 0) return 1;
    }
    /* Try PATH */
    return access(cmd, X_OK) == 0;
}

/* Check if running in Termux */
static inline int is_termux(void) {
    return getenv("TERMUX_VERSION") != NULL ||
           getenv("TERMUX_MAIN_PACKAGE_VERSION") != NULL ||
           access("/data/data/com.termux", F_OK) == 0;
}

/* Check if running on Android */
static inline int is_android(void) {
#ifdef PLATFORM_ANDROID
    return 1;
#else
    /* Runtime detection for non-Android builds running on Android */
    return access("/system/build.prop", F_OK) == 0 ||
           access("/system/bin/getprop", X_OK) == 0;
#endif
}

/* Get Android property via getprop */
static void getprop(const char *key, char *out, size_t max_len) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "getprop %s 2>/dev/null", key);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        if (fgets(out, max_len, fp)) {
            /* Trim newline */
            size_t len = strlen(out);
            if (len > 0 && out[len-1] == '\n') out[len-1] = '\0';
        }
        pclose(fp);
    }
}

/* Thread: Get user info */
static void* thread_user(void *arg) {
    SystemInfo *info = (SystemInfo*)arg;
    
    struct passwd *pw = getpwuid(getuid());
    if (pw) strncpy(info->username, pw->pw_name, 63);
    
    gethostname(info->hostname, 63);
    
    /* Android: override username if needed */
    if (info->is_android && info->username[0] == '\0') {
        strncpy(info->username, "u0_a0", 63);  /* Default Android app user */
    }
    
    return NULL;
}

/* Thread: Get OS info */
static void* thread_os(void *arg) {
    SystemInfo *info = (SystemInfo*)arg;
    struct utsname uts;
    uname(&uts);
    strncpy(info->kernel, uts.release, 63);
    
    if (info->is_android) {
        /* Android-specific info */
        strncpy(info->os_name, "Android", 63);
        
        /* Get Android version */
        if (has_getprop) {
            getprop("ro.build.version.release", info->android_ver, 15);
            getprop("ro.product.model", info->device_model, 63);
            
            /* Build distro string */
            char brand[32] = {0};
            getprop("ro.product.brand", brand, 31);
            if (brand[0] && info->device_model[0]) {
                snprintf(info->distro, 127, "%s %s (Android %s)", 
                         brand, info->device_model, info->android_ver);
            } else if (info->device_model[0]) {
                snprintf(info->distro, 127, "%s (Android %s)", 
                         info->device_model, info->android_ver);
            }
        }
        
        /* Termux detection */
        if (info->is_termux) {
            const char *tv = getenv("TERMUX_VERSION");
            if (tv) {
                size_t len = strlen(info->distro);
                snprintf(info->distro + len, 127 - len, " [Termux %s]", tv);
            }
        }
    } else {
        /* Standard Linux/macOS */
        char buf[4096];
        if (read_file("/etc/os-release", buf, sizeof(buf)) > 0) {
            char *line = strtok(buf, "\n");
            while (line) {
                if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                    char *val = line + 12;
                    if (*val == '"') val++;
                    size_t len = strlen(val);
                    if (len > 0 && val[len-1] == '"') val[--len] = '\0';
                    strncpy(info->distro, val, 127);
                } else if (strncmp(line, "ID=", 3) == 0) {
                    strncpy(info->os_name, line + 3, 63);
                }
                line = strtok(NULL, "\n");
            }
        }
        
#ifdef PLATFORM_MACOS
        if (info->distro[0] == '\0') {
            /* macOS version */
            FILE *fp = popen("sw_vers -productVersion 2>/dev/null", "r");
            if (fp) {
                char ver[32];
                if (fgets(ver, sizeof(ver), fp)) {
                    ver[strcspn(ver, "\n")] = '\0';
                    snprintf(info->distro, 127, "macOS %s", ver);
                }
                pclose(fp);
            }
            strncpy(info->os_name, "macOS", 63);
        }
#endif
    }
    return NULL;
}

/* Thread: Get memory info */
static void* thread_memory(void *arg) {
    SystemInfo *info = (SystemInfo*)arg;
    
    char buf[4096];
    if (read_file("/proc/meminfo", buf, sizeof(buf)) > 0) {
        uint64_t mem_total = 0, mem_available = 0, mem_free = 0;
        char *line = strtok(buf, "\n");
        while (line) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                mem_total = strtoull(line + 9, NULL, 10);
            } else if (strncmp(line, "MemAvailable:", 13) == 0) {
                mem_available = strtoull(line + 13, NULL, 10);
            } else if (strncmp(line, "MemFree:", 8) == 0) {
                mem_free = strtoull(line + 8, NULL, 10);
            }
            line = strtok(NULL, "\n");
        }
        info->mem_total = mem_total;
        info->mem_used = mem_total - (mem_available ? mem_available : mem_free);
    }
    return NULL;
}

/* Thread: Get CPU info */
static void* thread_cpu(void *arg) {
    SystemInfo *info = (SystemInfo*)arg;
    
    /* Increased buffer for systems with many cores */
    char buf[32768];
    if (read_file("/proc/cpuinfo", buf, sizeof(buf)) > 0) {
        int cores = 0;
        char *line = strtok(buf, "\n");
        
        if (info->is_android) {
            /* Android ARM/ARM64 CPU detection */
            char hardware[128] = {0};
            while (line) {
                if (strncmp(line, "Hardware", 8) == 0) {
                    char *colon = strchr(line, ':');
                    if (colon) {
                        colon++;
                        while (*colon == ' ' || *colon == '\t') colon++;
                        strncpy(hardware, colon, 127);
                        hardware[strcspn(hardware, "\n")] = '\0';
                    }
                } else if (strncmp(line, "processor", 9) == 0) {
                    cores++;
                }
                line = strtok(NULL, "\n");
            }
            
            if (hardware[0]) {
                strncpy(info->cpu, hardware, MAX_CPU - 1);
            } else {
                /* Fallback: try to get from getprop */
                if (has_getprop) {
                    getprop("ro.soc.model", info->cpu, MAX_CPU - 1);
                    if (info->cpu[0] == '\0') {
                        getprop("ro.board.platform", info->cpu, MAX_CPU - 1);
                    }
                }
            }
            
            /* If still no CPU, show CPU features */
            if (info->cpu[0] == '\0') {
                /* Check for ARM features */
                if (strstr(buf, "aarch64") || strstr(buf, "ARMv8")) {
                    strncpy(info->cpu, "ARMv8 (64-bit)", MAX_CPU - 1);
                } else if (strstr(buf, "ARMv7")) {
                    strncpy(info->cpu, "ARMv7 (32-bit)", MAX_CPU - 1);
                }
            }
        } else {
            /* Standard Linux/macOS x86/x86_64 detection */
            while (line) {
                if (strncmp(line, "model name", 10) == 0 && info->cpu[0] == '\0') {
                    char *colon = strchr(line, ':');
                    if (colon) {
                        colon++;
                        while (*colon == ' ') colon++;
                        strncpy(info->cpu, colon, MAX_CPU - 1);
                        char *nl = strchr(info->cpu, '\n');
                        if (nl) *nl = '\0';
                    }
                } else if (strncmp(line, "processor", 9) == 0) {
                    cores++;
                }
                line = strtok(NULL, "\n");
            }
        }
        
        info->cpu_cores = (uint8_t)cores;
        
#ifdef PLATFORM_MACOS
        if (info->cpu[0] == '\0') {
            size_t len = MAX_CPU;
            sysctlbyname("machdep.cpu.brand_string", info->cpu, &len, NULL, 0);
        }
#endif
    }
    return NULL;
}

/* Thread: Get uptime - FIXED for Android/Termux */
static void* thread_uptime(void *arg) {
    SystemInfo *info = (SystemInfo*)arg;
    
#ifdef __linux__
    /* Primary method: /proc/uptime */
    char buf[64];
    if (read_file("/proc/uptime", buf, sizeof(buf)) > 0) {
        info->uptime_secs = (uint32_t)strtod(buf, NULL);
    }
    
    /* Android/Termux fallback if /proc/uptime failed or returned 0 */
    if (info->uptime_secs == 0 && info->is_android) {
        /* Method 2: Try /proc/stat btime (boot time) */
        char stat_buf[4096];
        if (read_file("/proc/stat", stat_buf, sizeof(stat_buf)) > 0) {
            char *btime = strstr(stat_buf, "btime ");
            if (btime) {
                btime += 6;  /* Skip "btime " */
                time_t boot_time = (time_t)strtoll(btime, NULL, 10);
                if (boot_time > 0) {
                    info->uptime_secs = (uint32_t)(time(NULL) - boot_time);
                }
            }
        }
    }
    
    /* Method 3: Try sysinfo syscall as final fallback */
    if (info->uptime_secs == 0) {
        struct sysinfo s_info;
        if (sysinfo(&s_info) == 0) {
            info->uptime_secs = (uint32_t)s_info.uptime;
        }
    }
#endif

#ifdef PLATFORM_MACOS
    struct timeval boot_time;
    size_t len = sizeof(boot_time);
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    sysctl(mib, 2, &boot_time, &len, NULL, 0);
    time_t now = time(NULL);
    info->uptime_secs = (uint32_t)(now - boot_time.tv_sec);
#endif
    
    return NULL;
}

/* Thread: Get environment (shell, DE, terminal) */
static void* thread_env(void *arg) {
    SystemInfo *info = (SystemInfo*)arg;
    
    const char *shell = getenv("SHELL");
    if (shell) {
        const char *name = strrchr(shell, '/');
        name = name ? name + 1 : shell;
        strncpy(info->shell, name, 63);
    }
    
    if (info->is_android) {
        /* Android typically doesn't have DE, but check for Termux:X11 */
        if (getenv("DISPLAY") || getenv("WAYLAND_DISPLAY")) {
            if (is_termux()) {
                strncpy(info->de_wm, "Termux:X11", 63);
            }
        }
    } else {
        const char *de_vars[] = {"XDG_CURRENT_DESKTOP", "DESKTOP_SESSION", "GDMSESSION", NULL};
        for (int i = 0; de_vars[i]; i++) {
            const char *val = getenv(de_vars[i]);
            if (val && *val) {
                strncpy(info->de_wm, val, 63);
                break;
            }
        }
    }
    
    /* Terminal detection */
    if (info->is_termux) {
        strncpy(info->terminal, "Termux", 31);
    } else {
        const char *term = getenv("TERM_PROGRAM");
        if (term) {
            if (strcmp(term, "iTerm.app") == 0) strcpy(info->terminal, "iTerm2");
            else if (strcmp(term, "Apple_Terminal") == 0) strcpy(info->terminal, "Terminal");
            else strncpy(info->terminal, term, 31);
        } else if (getenv("KITTY_WINDOW_ID") || getenv("KITTY_INSTALLATION_DIR")) {
            strcpy(info->terminal, "kitty");
        } else {
            term = getenv("TERM");
            if (term) strncpy(info->terminal, term, 31);
        }
    }
    return NULL;
}

/* Thread: Count packages */
static void* thread_packages(void *arg) {
    SystemInfo *info = (SystemInfo*)arg;
    
    if (info->is_termux) {
        /* Termux uses apt/dpkg, but in a different path */
        DIR *dir = opendir("/data/data/com.termux/files/usr/var/lib/dpkg/info");
        if (dir) {
            uint32_t count = 0;
            struct dirent *entry;
            while ((entry = readdir(dir))) {
                const char *name = entry->d_name;
                size_t len = strlen(name);
                if (len > 5 && name[len-5] == '.' && name[len-4] == 'l' && 
                    name[len-3] == 'i' && name[len-2] == 's' && name[len-1] == 't') {
                    count++;
                }
            }
            closedir(dir);
            if (count > 0) {
                info->packages = count;
                memcpy(info->pkg_manager, "pkg", 4);
                return NULL;
            }
        }
    }
    
    /* Standard Linux dpkg */
    DIR *dir = opendir("/var/lib/dpkg/info");
    if (dir) {
        uint32_t count = 0;
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            const char *name = entry->d_name;
            size_t len = strlen(name);
            if (len > 5 && name[len-5] == '.' && name[len-4] == 'l' && 
                name[len-3] == 'i' && name[len-2] == 's' && name[len-1] == 't') {
                count++;
            }
        }
        closedir(dir);
        if (count > 0) {
            info->packages = count;
            memcpy(info->pkg_manager, "dpkg", 5);
            return NULL;
        }
    }
    
    /* Try pacman */
    dir = opendir("/var/lib/pacman/local");
    if (dir) {
        uint32_t count = 0;
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (entry->d_name[0] != '.') count++;
        }
        closedir(dir);
        if (count > 2) {
            info->packages = count - 2;
            memcpy(info->pkg_manager, "pacman", 7);
        }
    }
    
    return NULL;
}

/* Thread: Get GPU */
static void* thread_gpu(void *arg) {
    SystemInfo *info = (SystemInfo*)arg;
    
    if (info->is_android) {
        /* Android GPU detection via getprop */
        if (has_getprop) {
            /* Try various GPU properties */
            getprop("ro.hardware.gralloc", info->gpu, MAX_GPU - 1);
            if (info->gpu[0] == '\0') {
                getprop("ro.hardware.egl", info->gpu, MAX_GPU - 1);
            }
            
            /* Translate common GPU names */
            if (strstr(info->gpu, "adreno") || strstr(info->gpu, "qcom")) {
                strncpy(info->gpu, "Adreno", MAX_GPU - 1);
            } else if (strstr(info->gpu, "mali")) {
                strncpy(info->gpu, "Mali", MAX_GPU - 1);
            } else if (strstr(info->gpu, "powervr")) {
                strncpy(info->gpu, "PowerVR", MAX_GPU - 1);
            }
        }
        
        /* Fallback: check /sys/class/kgsl */
        if (info->gpu[0] == '\0') {
            char buf[256];
            if (read_file("/sys/class/kgsl/kgsl-3d0/devfreq/max_freq", buf, sizeof(buf)) > 0) {
                strncpy(info->gpu, "Adreno", MAX_GPU - 1);
            }
        }
        
        info->has_gpu = (info->gpu[0] != '\0');
        return NULL;
    }
    
    if (!has_lspci) return NULL;
    
    FILE *fp = popen("lspci 2>/dev/null | grep -E 'VGA|3D|Display' | head -1", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            char *colon = strstr(line, ": ");
            if (colon) {
                colon += 2;
                char *end = colon + strlen(colon) - 1;
                while (end > colon && (*end == '\n' || *end == '\r')) end--;
                *(end + 1) = '\0';
                strncpy(info->gpu, colon, MAX_GPU - 1);
                info->has_gpu = 1;
            }
        }
        pclose(fp);
    }
    return NULL;
}

/* Thread: Get resolution - FIXED for Android */
static void* thread_resolution(void *arg) {
    SystemInfo *info = (SystemInfo*)arg;
    
    if (info->is_android) {
        /* 
         * Android resolution detection - try multiple methods
         * Priority: actual resolution > fallback to DPI info
         */
        
        /* Method 1: Try wm size command (may fail without permissions) */
        FILE *fp = popen("wm size 2>/dev/null", "r");
        if (fp) {
            char line[128];
            if (fgets(line, sizeof(line), fp)) {
                /* Format: "Physical size: 1080x2400" or "Override size: 1080x2400" */
                char *size = strstr(line, "size: ");
                if (size) {
                    size += 6;  /* Skip "size: " */
                    size[strcspn(size, "\n")] = '\0';
                    /* Now we have actual resolution like "1080x2400" */
                    strncpy(info->resolution, size, 31);
                    info->has_resolution = 1;
                    pclose(fp);
                    return NULL;  /* Success! */
                }
            }
            pclose(fp);
        }
        
        /* Method 2: Try framebuffer info (works on some devices) */
        char buf[256];
        
        /* Try fb0 virtual_size - format: "1080,2400" */
        if (read_file("/sys/class/graphics/fb0/virtual_size", buf, sizeof(buf)) > 0) {
            /* Format: width,height */
            char *comma = strchr(buf, ',');
            if (comma) {
                int width = atoi(buf);
                int height = atoi(comma + 1);
                if (width > 0 && height > 0 && width < 10000 && height < 10000) {
                    snprintf(info->resolution, 31, "%dx%d", width, height);
                    info->has_resolution = 1;
                    return NULL;
                }
            }
        }
        
        /* Method 3: Try fb0 modes - may contain resolution */
        if (read_file("/sys/class/graphics/fb0/modes", buf, sizeof(buf)) > 0) {
            /* Format might be: "U:1080x2400p-0" or similar */
            char *x = strchr(buf, 'x');
            if (x) {
                /* Find start of width (before 'x') */
                char *start = x - 1;
                while (start > buf && isdigit(*(start-1))) start--;
                /* Find end of height (after 'x') */
                char *end = x + 1;
                while (*end && isdigit(*end)) end++;
                
                if (end > start + 3) {
                    int width = atoi(start);
                    int height = atoi(x + 1);
                    if (width > 0 && height > 0 && width < 10000 && height < 10000) {
                        snprintf(info->resolution, 31, "%dx%d", width, height);
                        info->has_resolution = 1;
                        return NULL;
                    }
                }
            }
        }
        
        /* Method 4: Try SurfaceFlinger display info (requires dumpsys, may fail) */
        fp = popen("dumpsys SurfaceFlinger 2>/dev/null | grep -E 'width|height' | head -4", "r");
        if (fp) {
            char line[256];
            int width = 0, height = 0;
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "width=") && width == 0) {
                    char *w = strstr(line, "width=");
                    if (w) width = atoi(w + 6);
                }
                if (strstr(line, "height=") && height == 0) {
                    char *h = strstr(line, "height=");
                    if (h) height = atoi(h + 7);
                }
                if (width > 0 && height > 0) break;
            }
            pclose(fp);
            if (width > 0 && height > 0 && width < 10000 && height < 10000) {
                snprintf(info->resolution, 31, "%dx%d", width, height);
                info->has_resolution = 1;
                return NULL;
            }
        }
        
        /* Method 5: Try getprop for display metrics */
        if (has_getprop) {
            char width_str[16] = {0};
            char height_str[16] = {0};
            char density_str[16] = {0};
            
            /* Try qemu.sf.lcd_density for emulator */
            getprop("qemu.sf.lcd_density", density_str, 15);
            
            /* Try actual display dimensions from ro.sf. */
            getprop("ro.sf.lcd_width", width_str, 15);
            getprop("ro.sf.lcd_height", height_str, 15);
            
            int w = atoi(width_str);
            int h = atoi(height_str);
            if (w > 0 && h > 0 && w < 10000 && h < 10000) {
                snprintf(info->resolution, 31, "%dx%d", w, h);
                info->has_resolution = 1;
                return NULL;
            }
        }
        
        /* 
         * Method 6: Last resort - show DPI with better label
         * Only use this if we couldn't get actual resolution
         */
        if (has_getprop) {
            char density[16] = {0};
            getprop("ro.sf.lcd_density", density, 15);
            if (density[0]) {
                /* Label it clearly as DPI, not resolution */
                snprintf(info->resolution, 31, "DPI: %s", density);
                info->has_resolution = 1;
            }
        }
        
        return NULL;
    }
    
    /* Non-Android: use xrandr */
    if (!has_xrandr) return NULL;
    
    FILE *fp = popen("xrandr --current 2>/dev/null | grep ' connected' | head -1", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            char *x = strchr(line, 'x');
            while (x) {
                char *start = x - 1;
                while (start > line && isdigit(*(start-1))) start--;
                char *end = x + 1;
                while (*end && (isdigit(*end) || *end == '.' || *end == ' ')) end++;
                if (end > start + 3) {
                    size_t len = end - start;
                    if (len < 32) {
                        memcpy(info->resolution, start, len);
                        info->resolution[len] = '\0';
                        char *spc = strchr(info->resolution, ' ');
                        if (spc) *spc = '\0';
                        info->has_resolution = 1;
                        break;
                    }
                }
                x = strchr(x + 1, 'x');
            }
        }
        pclose(fp);
    }
    return NULL;
}

/* Color codes */
#define R "\x1b[0m"
#define B "\x1b[1m"
#define D "\x1b[2m"
#define C1 "\x1b[96m"
#define C2 "\x1b[92m"
#define C3 "\x1b[93m"
#define C4 "\x1b[95m"
#define C5 "\x1b[94m"
#define C6 "\x1b[91m"

/* Print output */
static void print_output(SystemInfo *info) {
    /* Header */
    if (info->is_android && info->device_model[0]) {
        printf(B C1 "%s" R " @ " C1 "%s\n", info->username, info->device_model);
    } else {
        printf(B C1 "%s" R "@" C1 "%s\n", info->username, info->hostname);
    }
    printf(D "────────────────────────────────────────\n" R);
    
    /* OS info */
    if (info->is_android) {
        printf(B C2 "OS" R D ":" R " Android %s\n", info->android_ver);
        if (info->distro[0]) {
            printf(B C2 "Device" R D ":" R " %s\n", info->device_model);
        }
    } else {
        if (info->os_name[0]) printf(B C2 "OS" R D ":" R " %s\n", info->os_name);
        if (info->distro[0]) printf(B C2 "Distro" R D ":" R " %s\n", info->distro);
    }
    printf(B C3 "Kernel" R D ":" R " %s\n", info->kernel);
    
    /* Uptime */
    uint32_t d = info->uptime_secs / 86400;
    uint32_t h = (info->uptime_secs % 86400) / 3600;
    uint32_t m = (info->uptime_secs % 3600) / 60;
    printf(B C4 "Uptime" R D ":" R);
    if (d) printf(" %ud", d);
    if (h || d) printf(" %uh", h);
    printf(" %um\n", m);
    
    printf(B C5 "Shell" R D ":" R " %s\n", info->shell);
    if (info->de_wm[0]) printf(B C3 "DE/WM" R D ":" R " %s\n", info->de_wm);
    if (info->terminal[0]) printf(B C5 "Terminal" R D ":" R " %s\n", info->terminal);
    
    /* Hardware */
    if (info->cpu[0]) {
        printf(B C6 "CPU" R D ":" R " %u cores (%s)\n", info->cpu_cores, info->cpu);
    }
    if (info->has_gpu && info->gpu[0]) {
        printf(B C1 "GPU" R D ":" R " %s\n", info->gpu);
    }
    
    /* Memory */
    if (info->mem_total > 0) {
        float used = (float)info->mem_used / 1024.0f / 1024.0f;
        float total = (float)info->mem_total / 1024.0f / 1024.0f;
        float pct = (float)info->mem_used / (float)info->mem_total * 100.0f;
        printf(B C2 "Memory" R D ":" R " %.1f GB / %.1f GB (%.0f%%)\n", used, total, pct);
    }
    
    if (info->has_resolution && info->resolution[0]) {
        printf(B C4 "Resolution" R D ":" R " %s\n", info->resolution);
    }
    
    if (info->packages > 0) {
        printf(B C6 "Packages" R D ":" R " %s (%u)\n", info->pkg_manager, info->packages);
    }
    
    /* Color palette */
    printf("\n\x1b[30m███\x1b[31m███\x1b[32m███\x1b[33m███\x1b[34m███\x1b[35m███\x1b[36m███\x1b[37m███\n");
    printf("\x1b[90m███\x1b[91m███\x1b[92m███\x1b[93m███\x1b[94m███\x1b[95m███\x1b[96m███\x1b[97m███\n" R);
}

int main(int argc, char *argv[]) {
    int show_time = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--time") == 0 || strcmp(argv[i], "-t") == 0) show_time = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("zfetch v3.2 - Ultra-fast parallel system fetcher\n"
                   "Supports: Linux, macOS, Android/Termux\n"
                   "Usage: zfetch [--time] [--help]\n");
            return 0;
        }
    }
    
    uint64_t start = get_time_ns();
    
    /* Initialize system info */
    SystemInfo info = {0};
    
    /* Platform detection */
    info.is_android = is_android();
    info.is_termux = is_termux();
    
    /* Fast command availability check */
    has_lspci = cmd_exists("lspci");
    has_xrandr = cmd_exists("xrandr");
    has_getprop = cmd_exists("getprop") || access("/system/bin/getprop", X_OK) == 0;
    
    /* Create threads for parallel collection */
    pthread_t threads[7];
    
    /* Launch all threads simultaneously */
    pthread_create(&threads[0], NULL, thread_user, &info);
    pthread_create(&threads[1], NULL, thread_os, &info);
    pthread_create(&threads[2], NULL, thread_memory, &info);
    pthread_create(&threads[3], NULL, thread_cpu, &info);
    pthread_create(&threads[4], NULL, thread_uptime, &info);
    pthread_create(&threads[5], NULL, thread_env, &info);
    pthread_create(&threads[6], NULL, thread_packages, &info);
    
    /* Wait for core threads */
    for (int i = 0; i < 7; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Launch optional threads */
    pthread_t opt_threads[2];
    int opt_count = 0;
    
    pthread_create(&opt_threads[opt_count++], NULL, thread_gpu, &info);
    if (!info.is_android && has_xrandr) {
        pthread_create(&opt_threads[opt_count++], NULL, thread_resolution, &info);
    } else if (info.is_android) {
        pthread_create(&opt_threads[opt_count++], NULL, thread_resolution, &info);
    }
    
    for (int i = 0; i < opt_count; i++) {
        pthread_join(opt_threads[i], NULL);
    }
    
    uint64_t collect_end = get_time_ns();
    
    /* Print results */
    print_output(&info);
    
    uint64_t end = get_time_ns();
    
    if (show_time) {
        printf("\n" D "━━━ Benchmark ━━━\n" R);
        printf("Collection: %.3f ms\n", (collect_end - start) / 1000000.0);
        printf("Output:     %.3f ms\n", (end - collect_end) / 1000000.0);
        printf("Total:      %.3f ms\n", (end - start) / 1000000.0);
    }
    
    return 0;
}
