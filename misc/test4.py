from valve_parsers import VPKFile

VPK_PATH = "/media/pi/backup/SteamLibrary/steamapps/common/Half-Life 2/hl2/hl2_textures_dir.vpk"
vpk = VPKFile(VPK_PATH).parse_directory()
vpk.extract_file("materials/concrete/concretewall071f.vtf", "/tmp/check.vtf")

with open("/tmp/check.vtf", "rb") as f:
    raw = f.read(64)

print("Raw first 16 bytes:", raw[:16].hex())
print("Magic:", raw[:4])
print("Version major:", int.from_bytes(raw[4:6], 'little'))
print("Version minor:", int.from_bytes(raw[6:8], 'little'))
print("Header size:", int.from_bytes(raw[8:12], 'little'))
print("Width:", int.from_bytes(raw[12:14], 'little'))
print("Height:", int.from_bytes(raw[14:16], 'little'))