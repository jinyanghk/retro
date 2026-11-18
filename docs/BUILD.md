# Build

> must check and setup PATH before each build

## msys2 ucrt64

> need to set msys2/ucrt64/bin to path

### deps

```sh
pacman -S   mingw-w64-ucrt-x86_64-toolchain \
            mingw-w64-ucrt-x86_64-cmake \
            mingw-w64-ucrt-x86_64-git \
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
# might need to do this to re-config
# rm CMakeCache.txt 
cmake .. -DCMAKE_BUILD_TYPE=Debug -G Ninja
ninja
```

## Ubuntu

> must remove msys2 from PATH

### deps
```sh
sudo apt install nlohmann-json3-dev \
                 libglew-dev \
                 libglm-dev \
                 libsdl2-dev \
                 libsdl2-mixer-dev \
                 libassimp-dev \
                 python3-scipy \
                 cmake
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
      .
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

> need to remove msys2/ucrt64/bin from path, add windows version cmake in path

### deps

download win x64 vc pre-built

* https://github.com/libsdl-org/SDL_mixer/releases
* https://github.com/libsdl-org/SDL/releases
* https://github.com/nigels-com/glew/releases

download source

* https://github.com/g-truc/glm/releases
* https://github.com/nlohmann/json/releases (only need json.h)

### [portable msvc](/misc/portable-msvc.py)

Step-by-Step Setup

Step 1: Download the Script

1. Navigate to the official [mmozeiko / portable-msvc](https://gist.github.com/mmozeiko/7f3162ec2988e81e56d5c4e22cde9977) GitHub Gist.
2. Download the portable-msvc.py file.
3. Move this script into a clean directory where you want your compiler to live (e.g., C:\Users\YourUsername\Developer\msvc_build).

Step 2: Download the MSVC & Windows SDK Files

1. Open a standard, unprivileged Command Prompt (cmd.exe) or PowerShell window.

2. Navigate to your folder

```cmd
cd C:\Users\YourUsername\Developer\msvc_build
```
3. Run the script to fetch the toolchain. By default, it will automatically pull the bare minimum components required for native desktop application development from the latest stable Visual Studio release:cmd
python portable-msvc.py

(Note: If you run into a certificate error, you may need to run pip install certifi first as noted by the script creator).

4. Wait for the download to finish. It will create a folder named msvc inside your working directory.

Step 3: Initialize the Compiler Environment

The download process generates a few initialization batch files (setup_x86.bat, setup_x64.bat, etc.) inside the newly created folder. Whenever you want to compile code, you must initialize the environment variables (PATH, INCLUDE, LIB) for that specific terminal window: 
1. In your command prompt, target the architecture you want to build for

```cmd
cd msvc
setup_x64.bat
```
2. Your current command prompt session is now ready to use the compiler. 

Step 4: Verify the Installation

Test that the environment variables are active by querying the compiler version:

```cmd
cl.exe
```
You should see the standard Microsoft C/C++ Optimizing Compiler banner print to the console instead of a "'cl.exe' is not recognized" error.

Everyday Use Tips

• Session Lifespan: Running `setup_TARGET.bat` only updates the environment variables for your active shell session. If you close the window, you will need to re-run the batch file the next time you open a terminal. 
• VS Code Integration: If you use Visual Studio Code, you can open it directly from that initialized terminal window by typing `code .`. VS Code will inherit the environment variables, allowing the C/C++ extensions to locate cl.exe and system headers flawlessly.
• Python Packaging: If you are trying to use this setup to build local Python extension modules or C-dependencies via pip, remember to pass the flags `SET DISTUTILS_USE_SDK=1` and `SET MSSDK=1` to force Python to utilize your active environment shell tools.

### setup env vars for portable msvc
```bat
set MSVC_ROOT=D:\jin\portable-msvc\msvc
set SDK_VER=10.0.28000.0
set MSVC_VER=14.51.36231

set LIB=%MSVC_ROOT%\VC\Tools\MSVC\14.51.36231\lib\x64;%MSVC_ROOT%\Windows Kits\10\Lib\%SDK_VER%\um\x64;%MSVC_ROOT%\Windows Kits\10\Lib\%SDK_VER%\ucrt\x64;%LIB%
set INCLUDE=%MSVC_ROOT%\VC\Tools\MSVC\14.51.36231\include;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\ucrt;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\um;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\shared;%INCLUDE%
set PATH=%MSVC_ROOT%\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64;%MSVC_ROOT%\Windows Kits\10\bin\%SDK_VER%\x64;%PATH%

set PATH=%MSVC_ROOT%\VC\Tools\MSVC\%MSVC_VER%\bin\Hostx64\x64;%MSVC_ROOT%\Windows Kits\10\bin\%SDK_VER%\x64;%PATH%
set LIB=%MSVC_ROOT%\VC\Tools\MSVC\%MSVC_VER%\lib\x64;%MSVC_ROOT%\Windows Kits\10\Lib\%SDK_VER%\um\x64;%MSVC_ROOT%\Windows Kits\10\Lib\%SDK_VER%\ucrt\x64;%LIB%
set INCLUDE=%MSVC_ROOT%\VC\Tools\MSVC\%MSVC_VER%\include;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\ucrt;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\um;%MSVC_ROOT%\Windows Kits\10\Include\%SDK_VER%\shared;%INCLUDE%

```

### portable python (optional, when only python is now allow to install)

```bat
set PATH=C:\Users\%USERNAME%\AppData\Local\Programs\python-3.14.10-embed-amd64;%PATH%
```

### Jolt Physics

```bat
git clone https://github.com/jrouwe/JoltPhysics.git
cd JoltPhysics
cd Build
rmdir /s /q Build-MSVC
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
rmdir /s /q build-msvc
cmake -B build-msvc -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"
cmake --build build-msvc
```
