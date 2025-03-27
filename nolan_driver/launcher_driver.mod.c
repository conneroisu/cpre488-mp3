#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
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
__used __section("__versions") = {
	{ 0x2c635209, "module_layout" },
	{ 0x45288b79, "noop_llseek" },
	{ 0xe180ce57, "usb_deregister" },
	{ 0x942410fb, "usb_register_driver" },
	{ 0x56baf789, "usb_unanchor_urb" },
	{ 0x6bd0e573, "down_interruptible" },
	{ 0x2cf05418, "usb_anchor_urb" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0xb5571d08, "usb_alloc_coherent" },
	{ 0xe9ffc063, "down_trylock" },
	{ 0xcf2a6966, "up" },
	{ 0x54af84da, "usb_free_coherent" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x2e3bcce2, "wait_for_completion_interruptible" },
	{ 0x25974000, "wait_for_completion" },
	{ 0x89940875, "mutex_lock_interruptible" },
	{ 0xb20c3797, "usb_submit_urb" },
	{ 0x8427cc7b, "_raw_spin_lock_irq" },
	{ 0xa6257a2f, "complete" },
	{ 0x71038ac7, "pv_ops" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0x2a753652, "usb_deregister_dev" },
	{ 0x2a8ca7d2, "usb_autopm_put_interface" },
	{ 0xfdff3e35, "_dev_info" },
	{ 0x37a0cba, "kfree" },
	{ 0xdcc00bff, "usb_put_dev" },
	{ 0x2f886acb, "usb_free_urb" },
	{ 0x1ab02dc2, "_dev_err" },
	{ 0x437b7175, "usb_alloc_urb" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x632ff929, "usb_register_dev" },
	{ 0x5e23b2f7, "usb_get_dev" },
	{ 0x608741b5, "__init_swait_queue_head" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0xaf88e69b, "kmem_cache_alloc_trace" },
	{ 0x30a93ed, "kmalloc_caches" },
	{ 0x962c8ae1, "usb_kill_anchored_urbs" },
	{ 0x510508ea, "usb_kill_urb" },
	{ 0x407af304, "usb_wait_anchor_empty_timeout" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x92997ed8, "_printk" },
	{ 0x296695f, "refcount_warn_saturate" },
	{ 0xe9de6a72, "usb_autopm_get_interface" },
	{ 0xf9a6f7e4, "usb_find_interface" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("usb:v2123p1010d*dc*dsc*dp*ic*isc*ip*in*");

MODULE_INFO(srcversion, "FE22D5342712CE0BD334B2C");
