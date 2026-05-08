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
#include <dobby.h> // Standalone hooking framework

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ForceCloseOreUI", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ForceCloseOreUI", __VA_ARGS__)

namespace fs = std::filesystem;

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

// --- Dynamically detect package name without JNI ---
std::string getPackageName() {
    std::ifstream cmdline("/proc/self/cmdline");
    std::string pkgName;
    
    // In Android, the first null-terminated string in cmdline is the package name
    if (std::getline(cmdline, pkgName, '\0') && !pkgName.empty()) {
        return pkgName;
    }
    
    // Fallback to official just in case
    return "com.mojang.minecraftpe"; 
}

std::string getConfigDir() {
    std::string pkgName = getPackageName();
    
    // Dynamically inject the detected package name into the path
    std::string primary = "/storage/emulated/0/Android/data/" + pkgName + "/files/mods/ForceCloseOreUI/";
    
    std::error_code ec;
    fs::create_directories(primary, ec); // The 'ec' prevents fatal filesystem crashes
    return primary;
}

nlohmann::json outputJson;
std::string dirPath = "";
std::string filePath = "";
bool updated = false;

void saveJson(const std::string &path, const nlohmann::json &j) {
    std::error_code ec;
    // Safely attempt to create directories without throwing exceptions
    fs::create_directories(fs::path(path).parent_path(), ec);
    
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f) {
        LOGE("Failed to open config file for writing. Check Android storage permissions.");
        return;
    }
    std::string jsonStr = j.dump(4);
    std::fwrite(jsonStr.data(), 1, jsonStr.size(), f);
    std::fclose(f);
}

// Original function pointer for the hook
void (*orig_OreUi_init)(void*, void*, void*, void*, void*, void*, void*, void*, void*, OreUi&, void*);

// Our Dobby Hook replacement
void hook_OreUi_init(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9, OreUi &a10, void *a11) {
    dirPath = getConfigDir();
    filePath = dirPath + "config.json";

    if (fs::exists(filePath)) {
        std::ifstream inFile(filePath);
        if (inFile.is_open()) {
            inFile >> outputJson;
            inFile.close();
        }
    }

    for (auto &data : a10.mConfigs) {
        bool value = false;
        if (outputJson.contains(data.first) && outputJson[data.first].is_boolean()) {
            value = outputJson[data.first];
        } else {
            outputJson[data.first] = false;
            updated = true;
        }
        data.second.mUnknown3 = [value]() { return value; };
        data.second.mUnknown4 = [value]() { return value; };
    }

    if (updated || !fs::exists(filePath)) {
        saveJson(filePath, outputJson);
    }

    orig_OreUi_init(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

// --- STANDALONE SCANNER & POLLING LOOP 26.20 Support ARM64---
const std::vector<const char*> OREUI_PATTERNS = {
    "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? D1 ? ? ? D5 FA 03 00 AA F5 03 07 AA",
    "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? D1 ? ? ? D5 FB 03 00 AA F5 03 07 AA",
    "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? F9 ? ? ? D5 FB 03 00 AA ? ? ? F9 F5 03 07 AA",
    "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? D1 ? ? ? D5 FA 03 00 AA F6 03 07 AA",
    "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? D1 ? ? ? D5 FA 03 00 AA F5 03 07 AA"
};

static uintptr_t ResolveSignature(const char* sig) {
    std::vector<int> pattern;
    const char* p = sig;
    while (*p) {
        if (*p == ' ') { p++; continue; }
        if (*p == '?') { pattern.push_back(-1); p++; if(*p=='?') p++; continue; }
        pattern.push_back(strtol(p, nullptr, 16));
        p += 2;
    }

    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return 0;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (!strstr(line, "libminecraftpe.so") || !strstr(line, "r-x")) continue; 
        
        uintptr_t start, end;
        if (sscanf(line, "%lx-%lx", &start, &end) != 2) continue;

        uint8_t* scan_base = (uint8_t*)start;
        size_t size = end - start;
        if (size < pattern.size()) continue;

        for (size_t i = 0; i < size - pattern.size(); i++) {
            bool found = true;
            for (size_t j = 0; j < pattern.size(); j++) {
                if (pattern[j] != -1 && scan_base[i + j] != pattern[j]) {
                    found = false;
                    break;
                }
            }
            if (found) {
                fclose(fp);
                return (uintptr_t)(scan_base + i);
            }
        }
    }
    fclose(fp);
    return 0;
}

void* InjectionThread(void* arg) {
    LOGI("ForceCloseOreUI thread started. Waiting for libminecraftpe.so...");

    bool isLoaded = false;
    while (!isLoaded) {
        FILE* fp = fopen("/proc/self/maps", "r");
        if (fp) {
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "libminecraftpe.so") && strstr(line, "r-x")) {
                    isLoaded = true;
                    break;
                }
            }
            fclose(fp);
        }
        if (!isLoaded) usleep(500000); 
    }

    LOGI("libminecraftpe.so is mapped! Polling for OreUI signatures...");

    bool hookApplied = false;
    for (int attempts = 1; attempts <= 40; attempts++) {
        for (const char* sig : OREUI_PATTERNS) {
            uintptr_t addr = ResolveSignature(sig);
            if (addr != 0) {
                LOGI("Found OreUI signature! Applying DobbyHook...");
                DobbyHook((void*)addr, (void*)hook_OreUi_init, (void**)&orig_OreUi_init);
                hookApplied = true;
                break;
            }
        }
        if (hookApplied) break;
        sleep(1); 
    }

    if (!hookApplied) {
        LOGE("Failed to find any OreUI signatures after 40 seconds. You may need to update them for your Minecraft version.");
    }

    return nullptr;
}

__attribute__((constructor))
void ForceCloseOreUI_Init() {
    pthread_t thread;
    pthread_create(&thread, nullptr, InjectionThread, nullptr);
    pthread_detach(thread);
}
