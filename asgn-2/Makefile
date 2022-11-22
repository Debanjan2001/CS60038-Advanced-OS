obj-m+=lkm_module_2.o
all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules MODULE_FORCE_UNLOAD=yes
clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
