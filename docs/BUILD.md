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
            mingw-w64-ucrt-x86_64-glew \
            mingw-w64-ucrt-x86_64-python-scipy \
            mingw-w64-ucrt-x86_64-assimp
```

### linux ubuntu/debian


```sh
sudo apt install nlohmann-json3-dev \
                 libglew-dev \
                 libassimp-dev \
                 python3-scipy
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

## python



```sh
cd ~

# 创建名为 .venv 的虚拟环境
python3 -m venv .venv

# 激活环境（激活后终端提示符前面会出现 (.venv)）
source .venv/bin/activate

# 升级 pip 确保能拉到最新包
python -m pip install --upgrade pip

# 安装 Valve 文件解析库（推荐 valve-parsers，维护较新）[citation:4]
pip install valve-parsers

# 安装 VTF 转图片的库（底层依赖 Pillow）[citation:3][citation:11]
pip install vtf2img
```