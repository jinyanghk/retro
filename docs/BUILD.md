# Build

## msys2 ucrt64

### deps

```sh
pacman -S   mingw-w64-ucrt-x86_64-toolchain \
            mingw-w64-ucrt-x86_64-cmake \
            mingw-w64-ucrt-x86_64-SDL2 \
            mingw-w64-ucrt-x86_64-SDL2_mixer \
            mingw-w64-ucrt-x86_64-glm \
            mingw-w64-ucrt-x86_64-jolt-physics \
            mingw-w64-ucrt-x86_64-nlohmann-json \
            mingw-w64-ucrt-x86_64-glew \
            mingw-w64-ucrt-x86_64-python-scipy \
            mingw-w64-ucrt-x86_64-assimp
```

### use cmake + ninja

```sh
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -G Ninja
ninja
```

## Ubuntu

### deps
```sh
sudo apt install nlohmann-json3-dev \
                 libglew-dev \
                 libsdl2-dev \
                 libsdl2-mixer-dev \
                 libassimp-dev \
                 python3-scipy
```

### Jolt Physics

on linux, jolt physics must be build from source

```sh
git clone https://github.com/jrouwe/JoltPhysics.git
cd JoltPhysics
cd Build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DINTERPROCEDURAL_OPTIMIZATION=OFF \
      ..
make
sudo make install
```

### use cmake + make
DO NOT USE ninja on ubuntu, only build Release, it needs to match jolt

```sh
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

## windows

### deps

download win x64 vc pre-built

* https://github.com/libsdl-org/SDL_mixer/releases
* https://github.com/libsdl-org/SDL/releases
* https://github.com/nigels-com/glew/releases

download source

* https://github.com/g-truc/glm/releases
* https://github.com/nlohmann/json/releases (only need json.h)

### [portable msvc](/misc/portable-msvc.py)

```bat
set MSVC_ROOT=C:\Users\%USERNAME%\AppData\Local\Programs\portable-msvc\msvc
set SDK_VER=10.0.28000.0
set MSVC_VER=14.51.36231

set LIB=%MSVC_ROOT%\VC\Tools\MSVC\14.51.36231\lib\x64;%MSVC_ROOT%\Windows Kits\10\Lib\%SDK_VER%\um\x64;%MSVC_ROOT%\Windows Kits\10\Lib\%SDK_VER%\ucrt\x64;%LIB%
set INCLUDE=%MSVC_ROOT%\VC\Tools\MSVC\14.51.36231\include;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\ucrt;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\um;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\shared;%INCLUDE%
set PATH=%MSVC_ROOT%\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64;%MSVC_ROOT%\Windows Kits\10\bin\%SDK_VER%\x64;%PATH%

set PATH=%MSVC_ROOT%\VC\Tools\MSVC\%MSVC_VER%\bin\Hostx64\x64;%MSVC_ROOT%\Windows Kits\10\bin\%SDK_VER%\x64;%PATH%
set LIB=%MSVC_ROOT%\VC\Tools\MSVC\%MSVC_VER%\lib\x64;%MSVC_ROOT%\Windows Kits\10\Lib\%SDK_VER%\um\x64;%MSVC_ROOT%\Windows Kits\10\Lib\%SDK_VER%\ucrt\x64;%LIB%
set INCLUDE=%MSVC_ROOT%\VC\Tools\MSVC\%MSVC_VER%\include;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\ucrt;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\um;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\shared;%INCLUDE%

```

### portable python

```bat
set PATH=C:\Users\%USERNAME%\AppData\Local\Programs\python-3.14.10-embed-amd64;%PATH%
```

### Jolt Physics

```bat
git clone https://github.com/jrouwe/JoltPhysics.git
cd JoltPhysics
cd Build
cmake -B Build-MSVC ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL ^
  -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF ^
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF ^
  -DJPH_USE_DX12=OFF ^
  -DJPH_USE_VK=OFF ^
  -DJPH_USE_MTL=OFF ^
  -DJPH_USE_CPU_COMPUTE=ON
cmake --build Build-MSVC --config Release
```

### assimp

```bat
cd assimp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Releas -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
cmake --build build
```

### use cmake + nmake

```bat
cmake -B build-msvc -G "NMake Makefiles"
cmake --build build-msvc
```