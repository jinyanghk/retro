

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