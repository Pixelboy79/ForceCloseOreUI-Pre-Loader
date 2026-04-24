# ForceCloseOreUI (Standalone / Snail Edition)

A native C++ library for Minecraft Bedrock Edition designed to force-close the new OreUI interface. This is highly useful for bypassing UI-related crashes and identifying conflicting mods within your load order.

This specific fork has been **completely rewritten** to strip out all Levi Launcher and `libpreloader.so` dependencies. It is now a fully standalone `.so` library optimized for raw memory injection via the **Snail Method**.

## 🚀 Key Features

* **Snail Method Compatible:** Injects perfectly as a standalone library without triggering `UnsatisfiedLinkError` crashes.
* **No Levi Environment Required:** Completely removes `libGlossHook.a` and the LiteLDev preloader dependencies.
* **Safe Background Polling:** Utilizes a background thread to safely wait for `libminecraftpe.so` to unpack, preventing race conditions and segmentation faults during early injection.
* **Dobby Hook Integration:** Replaced static macro hooks with standard inline hooking via [Dobby](https://github.com/jmpews/Dobby).
* **Hardcoded JNI Bypass:** Bypasses JavaVM lookups to ensure the config file saves correctly regardless of when the mod is injected.

## 📂 Configuration

Once injected, the mod will automatically generate a configuration file at the following path:
`/storage/emulated/0/Android/data/com.mojang.minecraftpe/files/mods/ForceCloseOreUI/config.json`

You can edit this JSON file to toggle specific UI elements on or off.

## 🛠️ Building the Project

This project uses `xmake` and has a fully automated GitHub Actions CI/CD pipeline. 

### Automated Build (Recommended)
You do not need to install the Android NDK locally. 
1. Fork or push your code to GitHub.
2. The GitHub Actions workflow will automatically download the Dobby dependencies, compile the code for `arm64-v8a`, and upload the standalone `libForceCloseOreUI.so` to the **Actions** tab.

### Manual Local Build
If you prefer to compile locally, ensure you have the Android NDK (r26b recommended) and `xmake` installed.

```bash
# Configure the project for Android (Accept the prompt to install Dobby)
xmake f -p android --ndk=/path/to/your/android-ndk -a arm64-v8a -c --yes

# Compile the project
xmake


## 📜 Credits & Copyright

**© 2026 Pixelboypro** — *Standalone Snail Method Conversion, Levi Launcher/Preloader Dependency Removal, and Dobby Hook Integration.*

* Original concept, core logic, and OreUI memory signatures created by **QYCottage / yinghuajimew / stivusik**. 

Permission is granted to use, modify, and distribute the standalone modifications provided that the above copyright notice and author credits are included in all copies or substantial portions of the software.
