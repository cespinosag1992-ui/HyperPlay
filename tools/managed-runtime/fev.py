import struct, sys, collections
b = open(sys.argv[1], "rb").read()
counts = collections.Counter()
def walk(off, end, depth, path):
    while off + 8 <= end:
        cid = b[off:off + 4].decode("latin1"); size = struct.unpack_from("<I", b, off + 4)[0]
        body = off + 8
        if cid in ("RIFF", "LIST"):
            sub = b[body:body + 4].decode("latin1")
            counts[path + "/" + cid + ":" + sub] += 1
            if depth < 6: walk(body + 4, body + size, depth + 1, path + "/" + sub)
        else:
            counts[path + "/" + cid] += 1
        off = body + size + (size & 1)
walk(0, len(b), 0, "")
for k, v in sorted(counts.items()):
    print(f"{v:6d}  {k}")
