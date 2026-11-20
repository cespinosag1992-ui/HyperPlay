import struct, sys, collections
def imports(path):
    b = open(path, "rb").read()
    pe = struct.unpack_from("<I", b, 0x3c)[0]
    magic = struct.unpack_from("<H", b, pe + 24)[0]
    is64 = magic == 0x20b
    dd = pe + 24 + (112 if is64 else 96)
    irva = struct.unpack_from("<I", b, dd + 8)[0]
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    opt = struct.unpack_from("<H", b, pe + 20)[0]
    st = pe + 24 + opt
    secs = [struct.unpack_from("<IIII", b, st + i * 40 + 8) for i in range(nsec)]
    def off(rva):
        for vsz, va, rsz, raw in secs:
            if va <= rva < va + max(vsz, rsz): return rva - va + raw
    res = collections.OrderedDict()
    o = off(irva)
    while True:
        ilt, _, _, name, iat = struct.unpack_from("<IIIII", b, o)
        if name == 0: break
        s = off(name); dll = b[s:b.index(b"\0", s)].decode()
        fns = []
        p = off(ilt or iat)
        while True:
            v = struct.unpack_from("<Q" if is64 else "<I", b, p)[0]
            if v == 0: break
            if v >> (63 if is64 else 31): fns.append(f"#{v & 0xffff}")
            else:
                q = off(v & 0x7fffffff) + 2
                fns.append(b[q:b.index(b"\0", q)].decode())
            p += 8 if is64 else 4
        res[dll] = fns
        o += 20
    return res
for path in sys.argv[1:]:
    print("==", path.split("\\")[-1])
    for dll, fns in imports(path).items():
        print(f"  {dll}: {len(fns)}" + (": " + ", ".join(fns) if dll.lower().startswith(("kernel32","advapi","user32","ws2","shell","ole","winmm","psapi","iphlp","bcrypt","version","ntdll","shlwapi","msvcrt","gdi32","imm32","setupapi")) else ""))
