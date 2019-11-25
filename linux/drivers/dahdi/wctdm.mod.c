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

MODULE_ALIAS("pci:v0000E159d00000001sv0000A159sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000E159sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000B100sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000B1D9sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000B118sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000B119sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000A9FDsd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000A8FDsd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000A800sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000A801sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000A908sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000E159d00000001sv0000A901sd*bc*sc*i*");

MODULE_INFO(srcversion, "6CB29641CE0C6EFF105A8ED");
