import re

with open('d1_town_05.vmf', 'r', errors='ignore') as f:
    text = f.read()

print("文件大小:", len(text))
print("前 500 个字符:")
print(text[:500])

# 检查是否有 world 和 solid 关键字
print("\n包含 'world':", 'world' in text.lower())
print("包含 'solid':", 'solid' in text.lower())
print("包含 'entity':", 'entity' in text.lower())