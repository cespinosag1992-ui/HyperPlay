import struct, sys
def exports(path):
    b = open(path, "rb").read()
    pe = struct.unpack_from("<I", b, 0x3c)[0]
    magic = struct.unpack_from("<H", b, pe + 24)[0]
    dd = pe + 24 + (96 if magic == 0x10b else 112)
    erva, esz = struct.unpack_from("<II", b, dd)
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    opt = struct.unpack_from("<H", b, pe + 20)[0]
    st = pe + 24 + opt
    secs = [struct.unpack_from("<IIII", b, st + i * 40 + 8) for i in range(nsec)]
    def off(rva):
        for vsz, va, rsz, raw in secs:
            if va <= rva < va + max(vsz, rsz): return rva - va + raw
    o = off(erva)
    nnames, faddr, naddr = struct.unpack_from("<III", b, o + 24)[0], 0, struct.unpack_from("<I", b, o + 32)[0]
    names = []
    for i in range(nnames):
        r = struct.unpack_from("<I", b, off(naddr) + 4 * i)[0]
        s = off(r); e = b.index(b"\0", s)
        names.append(b[s:e].decode())
    return names
n = exports(sys.argv[1])
print(len(n), "exports")
open(sys.argv[2], "w").write("\n".join(n))
