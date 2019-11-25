#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=xpp";

MODULE_ALIAS("usb:vE4E4p1132d*dc*dsc*dp*ic*isc*ip*");
MODULE_ALIAS("usb:vE4E4p1142d*dc*dsc*dp*ic*isc*ip*");
MODULE_ALIAS("usb:vE4E4p1152d*dc*dsc*dp*ic*isc*ip*");
MODULE_ALIAS("usb:vE4E4p1162d*dc*dsc*dp*ic*isc*ip*");

MODULE_INFO(srcversion, "3418F4310A483FC0E97779D");
