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
"depends=dahdi_voicebus,dahdi";

MODULE_ALIAS("pci:v0000D161d00002400sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000800sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00008002sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00008003sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00008005sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00008006sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "077634528375DC115E707A8");
