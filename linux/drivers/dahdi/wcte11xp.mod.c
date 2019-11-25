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

MODULE_ALIAS("pci:v0000E159d00000001sv000071FEsd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv000079FEsd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000795Esd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv000079DEsd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000797Esd*bc*sc*i*");

MODULE_INFO(srcversion, "1F81928AC9684D9DD9BB9FD");
