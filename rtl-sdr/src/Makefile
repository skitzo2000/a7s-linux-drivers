# make            — build against the running kernel's headers
# make install    — copy into /lib/modules/$(uname -r)/extra and run depmod (root)
KV   ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KV)/build
DEST ?= /lib/modules/$(KV)/extra
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
install: all
	install -d $(DEST)
	install -m 644 dvb_usb_v2.ko dvb-usb-rtl28xxu.ko $(DEST)/
	depmod -a $(KV)
.PHONY: all clean install
