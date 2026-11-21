import struct, sys, zlib
dll = open(sys.argv[1], "rb").read()
pe = struct.unpack_from("<I", dll, 0x3c)[0]
base = struct.unpack_from("<I", dll, pe + 24 + 28)[0]
nsec = struct.unpack_from("<H", dll, pe + 6)[0]; opt = struct.unpack_from("<H", dll, pe + 20)[0]
secs = [struct.unpack_from("<IIII", dll, pe + 24 + opt + i * 40 + 8) for i in range(nsec)]
def va2off(va):
    rva = va - base
    for vsz, v, rsz, raw in secs:
        if v <= rva < v + max(vsz, rsz): return rva - v + raw
for crc in (2767611644, 3311335958):
    p = dll.find(struct.pack("<I", crc))
    ptr, size = struct.unpack_from("<II", dll, p - 8)
    o = va2off(ptr); h = dll[o:o + size]
    print(crc, hex(ptr), size, h[:7], "crc32:", zlib.crc32(h), "match" if zlib.crc32(h) == crc else "NO")
