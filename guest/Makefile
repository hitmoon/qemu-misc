KDIR ?= /usr/src/linux-source-3.2


T := flip_pci.ko flip_test

all:
	@echo "Build flip_pci kernel module ..."
	make -C $(KDIR) M=$(shell pwd) modules
	@echo "Build flip test ..."
	gcc flip_user.c -o flip_test
obj-m += flip_pci.o

.PHONY: clean
clean:
	rm -fv *.o 
	rm -fv $(T)

