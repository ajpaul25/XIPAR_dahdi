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

MODULE_ALIAS("pci:v000010EEd00000314sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000420sv00000004sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000410sv00000004sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000405sv00000004sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000410sv00000003sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000405sv00000003sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000410sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000405sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000220sv00000004sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000205sv00000004sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000210sv00000004sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000205sv00000003sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000210sv00000003sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000205sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000D161d00000210sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "26CDF1ABD0A63F6405A387D");
