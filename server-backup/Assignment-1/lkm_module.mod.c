#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0x62ac93f9, "module_layout" },
	{ 0x8b1e3b69, "remove_proc_entry" },
	{ 0x31b0044a, "proc_create" },
	{ 0x37a0cba, "kfree" },
	{ 0x8270c609, "pv_ops" },
	{ 0xdbf17652, "_raw_spin_lock" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0xd6b678b8, "kmem_cache_alloc_trace" },
	{ 0x4ca96566, "kmalloc_caches" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0xf474fdcb, "kfree_const" },
	{ 0xdecd0b29, "__stack_chk_fail" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x269d5c9f, "current_task" },
	{ 0xc5850110, "printk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "54B22067C25172570C6F2D4");
