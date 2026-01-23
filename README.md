# Flycast

<img src="shell/linux/flycast.png" alt="flycast logo" width="150"/>

**Flycast** is a multi-platform Sega Dreamcast, Naomi, Naomi 2, and Atomiswave emulator derived from [**reicast**](https://github.com/skmp/reicast-emulator).

Information about configuration and supported features can be found on [**TheArcadeStriker's flycast wiki**](https://github.com/TheArcadeStriker/flycast-wiki/wiki).

### Building for iOS

### Prerequisites
- Xcode (latest version recommended)
- CMake
- **[StikDebug 2.3.7](https://github.com/StephenDev0/StikDebug/releases/tag/2.3.7)** (required for debugging)
  - Flycast-iOS automatically routes to the correct .js

### Build Instructions

1. **Clone the repository**
```bash
   git clone --recursive https://github.com/chachillie/Flycast-iOS.git
   cd Flycast-iOS
```

2. **Generate Xcode project**
```bash
   mkdir build-ios
   cd build-ios
   cmake .. -G Xcode -DCMAKE_SYSTEM_NAME=iOS
```

3. **Open in Xcode**
```bash
   open flycast.xcodeproj
```

4. **Configure code signing**
   - Select the project in the navigator
   - Under "Signing & Capabilities", enable Automatic signing
   - Select your Development Team
   - Add "Increased Memory Limit" capability 

5. **Enable code signing in build settings**
   - Select the project/target
   - Go to Build Settings
   - Search for "signing"
   - Set "Code Signing Allowed" from `NO` to `YES`

6. **Build and run**
   - Select your target device/simulator
   - Build

---

**Questions or issues?** Open an issue on GitHub.