#!/usr/bin/env python3
# Standalone DPCD-over-AUX probe for the A733 trilinear DP14 controller.
import mmap, os, struct, sys, time
EDP = 0x5740000          # controller register base (edp0 reg[0])
AUX_PHY = 0x6c01e00      # aux-hpd phy top_reg
PAGE = 4096
class Win:
    def __init__(self, base, size=PAGE):
        self.pb = base & ~(PAGE-1); self.off = base - self.pb
        self.f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
        self.m = mmap.mmap(self.f, size + PAGE, offset=self.pb)
    def rd(self, r):  return struct.unpack("<I", self.m[self.off+r:self.off+r+4])[0]
    def wr(self, r, v): self.m[self.off+r:self.off+r+4] = struct.pack("<I", v)
    def close(self): self.m.close(); os.close(self.f)

AUX_CMD, AUX_ADDR, AUX_CLKDIV = 0x100, 0x108, 0x10C
# INTERRUPT_STATE (0x130) is read-only and does NOT clear the latch.
# INTERRUPT_CAUSE (0x140) is the write-1-to-clear / read-to-clear register.
INT_CAUSE = 0x140
RPLY_DATA, RPLY_CODE, AUX_STATUS, PHY_STATUS = 0x134, 0x138, 0x14C, 0x280
AUX_PHY_CTRL = 0x04   # offset into aux-hpd phy top_reg for direction control
CMD_READ = 0x09 << 8

# AUX frame transmit duration in µs at 1 Mbaud (9-bit: sync+data+stop per byte
# per the DP spec, i.e. 9 µs/byte). The request is 5 bytes (cmd + 4-byte addr)
# plus payload, plus a turnaround guard-band.
def aux_tx_us(length):
    return (5 + length) * 9 + 20

def aux_read(w, phy, addr, length=1):
    # Clear any latched interrupt cause (use CAUSE, not STATE).
    w.rd(INT_CAUSE)
    w.wr(AUX_ADDR, addr)

    # Drive the AUX pad to TX before writing the command, because that write
    # starts the frame going out. Bit 9 = CFG_AUX_TX_DIRECTION.
    phy.wr(AUX_PHY_CTRL, phy.rd(AUX_PHY_CTRL) | (1 << 9))
    w.wr(AUX_CMD, CMD_READ | (length - 1))

    # Hold TX until the request has left the wire, then turn around to RX
    # so the reply can be received.
    time.sleep(aux_tx_us(length) * 1e-6)
    phy.wr(AUX_PHY_CTRL, phy.rd(AUX_PHY_CTRL) & ~(1 << 9))

    t0 = time.time()
    while time.time() - t0 < 0.5:
        st = w.rd(AUX_STATUS); ints = w.rd(INT_CAUSE)
        if ints & (1 << 3):   return ("TIMEOUT", st, ints, w.rd(RPLY_CODE), None)
        if st & (1 << 3):     return ("REPLY_ERR", st, ints, w.rd(RPLY_CODE), None)
        if st & (1 << 0):
            code = w.rd(RPLY_CODE)
            data = [w.rd(RPLY_DATA) & 0xff for _ in range(length)]
            return ("REPLY", st, ints, code, data)
    return ("HANG", w.rd(AUX_STATUS), w.rd(INT_CAUSE), w.rd(RPLY_CODE), None)

if __name__ == "__main__":
    w = Win(EDP, 0x1000)
    if len(sys.argv) > 1 and sys.argv[1] == "setaux":
        p = Win(AUX_PHY); val = int(sys.argv[2], 16)
        print("aux phy 0x04: 0x%08x -> 0x%08x" % (p.rd(0x04), val)); p.wr(0x04, val); p.close()
    phy = Win(AUX_PHY)
    print("phy_status=0x%x clkdiv=0x%x aux_phy_ctrl=0x%x" % (w.rd(PHY_STATUS), w.rd(AUX_CLKDIV), phy.rd(AUX_PHY_CTRL)))
    for attempt in range(3):
        r, st, ints, code, data = aux_read(w, phy, 0x0, 1)
        print("  try%d: %-9s aux_status=0x%-4x int_state=0x%-3x reply=0x%x data=%s"
              % (attempt, r, st, ints, code, data))
    w.close()
    phy.close()
