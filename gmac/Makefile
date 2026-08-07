# Out-of-tree build of the sun60iw2 (A733) GMAC-210 stmmac glue for 6.18.
# Needs the private stmmac driver headers in ./stmmac-hdrs (copied from the
# matching kernel source tree, e.g. NickAlilovic/allwinner-linux-6.18
# drivers/net/ethernet/stmicro/stmmac/*.h).
#
# Build on the board:
#   make KDIR=/usr/src/linux-headers-6.18.19-edge-sun60iw2

obj-m := dwmac-sun60iw2.o

ccflags-y += -I$(src)/stmmac-hdrs

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
