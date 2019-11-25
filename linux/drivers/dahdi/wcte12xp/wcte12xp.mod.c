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

MODULE_ALIAS("pci:v0000D161d00000120sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00008000sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00008001sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "037E050D1B5878AD06F9178");
