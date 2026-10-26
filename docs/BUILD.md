# How to Build

## Install Dependencies

### msys2 ucrt64

```sh
pacman -S   mingw-w64-ucrt-x86_64-toolchain \
            mingw-w64-ucrt-x86_64-cmake \
            mingw-w64-ucrt-x86_64-SDL2 \
            mingw-w64-ucrt-x86_64-SDL2_mixer \
            mingw-w64-ucrt-x86_64-glm \
            mingw-w64-ucrt-x86_64-jolt-physics \
            mingw-w64-ucrt-x86_64-nlohmann-json \
            mingw-w64-ucrt-x86_64-glew
```

### linux ubuntu/debian


```sh
sudo apt install nlohmann-json3-dev \
                 libglew-dev
```

#### Jolt Physics

on linux, jolt physics must be build from source

https://jrouwe.github.io/JoltPhysics/md__build_2_r_e_a_d_m_e.html

```sh
git clone https://github.com/jrouwe/JoltPhysics.git
cd JoltPhysics
cd Build
sudo chmod +x cmake_ninja.sh
./cmake_ninja.sh
cd Ninja_MultiConfig
cmake –build . –config Release
sudo cmake –install . –config Release
```

### windows

install vcpkg

```bat
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg; .\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
$env:PATH = "$env:VCPKG_ROOT;$env:PATH"
```

## Posix Config & Build

```sh
mkdir build
cd build
cmake .. -G Ninja
ninja
```

## Windows Config & Build

```bat
cmake -B build -S . -G "Ninja" -DCMAKE_TOOLCHAIN_FILE="[path_to_vcpkg]/scripts/buildsystems/vcpkg.cmake"

cmake --build build
```