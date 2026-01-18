<img src="shell/linux/flycast.png" alt="flycast logo" width="150"/>


# Flycast

**Flycast** is a multi-platform Sega Dreamcast, Naomi, Naomi 2, and Atomiswave emulator derived from [**reicast**](https://github.com/skmp/reicast-emulator).

Information about configuration and supported features can be found on [**TheArcadeStriker's flycast wiki**](https://github.com/TheArcadeStriker/flycast-wiki/wiki).


### iOS

# 1. Clone the repository
git clone --recursive https://github.com/chachillie/Flycast-iOS.git

cd Flycast-iOS

# 2. Create build directory
mkdir build-ios

cd build-ios

# 3. Run CMake            
cmake .. -G Xcode -DCMAKE_SYSTEM_NAME=iOS


# 4. Open in Xcode
open flycast.xcodeproj







