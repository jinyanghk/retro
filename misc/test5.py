import vpk

VPK_PATH = "/media/pi/backup/SteamLibrary/steamapps/common/Half-Life 2/hl2/hl2_textures_dir.vpk"
pak = vpk.open(VPK_PATH)

data = pak["materials/concrete/concretewall071f.vtf"].read()
with open("/tmp/real.vtf", "wb") as f:
    f.write(data)

print(f"Size: {len(data)} bytes")

# 验证头部
with open("/tmp/real.vtf", "rb") as f:
    raw = f.read(16)
print("Magic:", raw[:4])
print("Version:", int.from_bytes(raw[4:6], 'little'), ".", int.from_bytes(raw[6:8], 'little'))
print("Header size:", int.from_bytes(raw[8:12], 'little'))
print("Width:", int.from_bytes(raw[12:14], 'little'))
print("Height:", int.from_bytes(raw[14:16], 'little'))