import mmap, os, sys, struct
PAGE = 4096
def peek(addr, n=1):
    base = addr & ~(PAGE-1); off = addr - base
    f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    m = mmap.mmap(f, PAGE*2, offset=base)
    out = [struct.unpack("<I", m[off+4*i:off+4*i+4])[0] for i in range(n)]
    m.close(); os.close(f); return out
def poke(addr, val):
    base = addr & ~(PAGE-1); off = addr - base
    f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    m = mmap.mmap(f, PAGE*2, offset=base)
    m[off:off+4] = struct.pack("<I", val)
    m.close(); os.close(f)
if sys.argv[1] == "r":
    a = int(sys.argv[2],16); n = int(sys.argv[3]) if len(sys.argv)>3 else 1
    for i,v in enumerate(peek(a,n)): print("0x%08x: 0x%08x" % (a+4*i, v))
else:
    a = int(sys.argv[2],16); v = int(sys.argv[3],16); poke(a,v); print("wrote 0x%08x -> 0x%08x" % (v,a))
