ifneq ($(KERNELRELEASE),)
	obj-m := z800.o

else

	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

install: 
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	rm -f *o
	rm -f .z*
	rm -f *.mod.c
	rm -rf .tmp*
	rm -f Modul*
	rm -f z800Test

load:
	modprobe z800

reload:
	rmmod z800
	modprobe z800

test:
	$(CC) z800Test.c -o z800Test
	
endif

