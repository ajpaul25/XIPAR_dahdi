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
"depends=dahdi";

MODULE_ALIAS("pci:v000010B5d00009030sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010B5d00003001sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010B5d0000D00Dsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010B5d00004000sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "4772C8C4F80C722746D7CDA");
