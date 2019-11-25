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

MODULE_ALIAS("pci:v0000E159d00000001sv00008084sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv00008085sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv00008086sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv00008087sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001057d00005608sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "9FBA06A4CF1FF6824F26CD2");
