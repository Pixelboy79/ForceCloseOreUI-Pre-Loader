#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>
#include <vector>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>
#include <dobby.h>

#define LOG_TAG "ForceCloseOreUI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace fs = std::filesystem;

// --- Minecraft Classes ---
class OreUIConfig {
public:
    void *mUnknown1;
    void *mUnknown2;
    std::function<bool()> mUnknown3;
    std::function<bool()> mUnknown4;
};

class OreUi {
public:
    std::unordered_map<std::string, OreUIConfig> mConfigs;
};

// --- Utilities ---
std::string getPackageName() {
    std::ifstream cmdline("/proc/self/cmdline");
    std::string pkgName;
    if (std::getline(cmdline, pkgName, '\0') && !pkgName.empty()) {
        return pkgName;
    }
    return "com.mojang.minecraftpe"; 
}

std::string getConfigPath() {
    // Android 11+ uses /Android/data/pkg/files as a safe bet for native mods
    std::string path = "/sdcard/Android/data/" + getPackageName() + "/files/mods/ForceCloseOreUI/";
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        fs::create_directories(path, ec);
    }
    return path + "config.json";
}

// --- Hook Logic ---
void (*orig_OreUi_init)(void*, void*, void*, void*, void*, void*, void*, void*, void*, OreUi&, void*);

void hook_OreUi_init(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9, OreUi &oreUi, void *a11) {
    // Call original first so the map is populated by the game
    orig_OreUi_init(a1, a2, a3, a4, a5, a6, a7, a8, a9, oreUi, a11);

    std::string filePath = getConfigPath();
    nlohmann::json config;
    bool needsSave = false;

    // Load existing config
    if (fs::exists(filePath)) {
        try {
            std::ifstream inFile(filePath);
            inFile >> config;
        } catch (...) {
            LOGE("Failed to parse config.json, resetting.");
        }
    }

    // Apply overrides
    for (auto &entry : oreUi.mConfigs) {
        const std::string& name = entry.first;
        bool value = false;

        if (config.contains(name) && config[name].is_boolean()) {
            value = config[name];
        } else {
            config[name] = false; // Default to false (Force Close)
            needsSave = true;
        }

        // Overwrite the lambdas
        entry.second.mUnknown3 = [value]() { return value; };
        entry.second.mUnknown4 = [value]() { return value; };
    }

    if (needsSave) {
        std::ofstream outFile(filePath);
        if (outFile.is_open()) {
            outFile << config.dump(4);
            LOGI("Config updated and saved to: %s", filePath.c_str());
        }
    }
}

// --- Pattern Scanning ---
struct MemoryRegion {
    uintptr_t start;
    uintptr_t end;
};

std::vector<MemoryRegion> getLibraryRegions(const char* libName) {
    std::vector<MemoryRegion> regions;
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return regions;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, libName) && strstr(line, "r-x")) {
            uintptr_t start, end;
            sscanf(line, "%lx-%lx", &start, &end);
            regions.push_back({start, end});
        }
    }
    fclose(fp);
    return regions;
}

uintptr_t FindPattern(uintptr_t start, uintptr_t end, const char* pattern) {
    std::vector<int> bytes;
    const char* p = pattern;
    while (*p) {
        if (*p == ' ') { p++; continue; }
        if (*p == '?') {
            bytes.push_back(-1);
            p += (*(p+1) == '?') ? 2 : 1;
            continue;
        }
        bytes.push_back((int)strtol(p, nullptr, 16));
        p += 2;
    }

    const uint8_t* scanStart = reinterpret_cast<const uint8_t*>(start);
    const size_t scanLen = end - start;
    const size_t patternLen = bytes.size();

    for (size_t i = 0; i <= scanLen - patternLen; ++i) {
        bool match = true;
        for (size_t j = 0; j < patternLen; ++j) {
            if (bytes[j] != -1 && scanStart[i + j] != bytes[j]) {
                match = false;
                break;
            }
        }
        if (match) return reinterpret_cast<uintptr_t>(&scanStart[i]);
    }
    return 0;
}

// --- Initialization ---
void* MainThread(void*) {
    LOGI("Thread started. Waiting for libminecraftpe.so...");

    std::vector<MemoryRegion> regions;
    while (regions.empty()) {
        regions = getLibraryRegions("libminecraftpe.so");
        if (regions.empty()) usleep(100000); // 100ms
    }

    LOGI("Library found. Scanning for OreUI...");

    const std::vector<const char*> patterns = {
        "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D5 FB 03 00 AA F5 03 07 AA",
        "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? F9 ? ? ? D5 FB 03 00 AA ? ? ? F9 F5 03 07 AA"
    };

    uintptr_t targetAddr = 0;
    for (int i = 0; i < 50; i++) { // Max 50 attempts
        for (const auto& region : regions) {
            for (const char* sig : patterns) {
                targetAddr = FindPattern(region.start, region.end, sig);
                if (targetAddr) break;
            }
            if (targetAddr) break;
        }
        if (targetAddr) break;
        usleep(200000); // Wait 200ms
    }

    if (targetAddr) {
        LOGI("OreUI found at %p. Applying Hook...", (void*)targetAddr);
        DobbyHook((void*)targetAddr, (void*)hook_OreUi_init, (void**)&orig_OreUi_init);
        LOGI("Hook applied successfully.");
    } else {
        LOGE("Failed to find OreUI pattern after multiple attempts.");
    }

    return nullptr;
}

__attribute__((constructor))
void Init() {
    pthread_t thread;
    pthread_create(&thread, nullptr, MainThread, nullptr);
    pthread_detach(thread);
}
