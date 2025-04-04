#ifndef __GPIO_INTERFACE_H
#define __GPIO_INTERFACE_H

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#define REG_SIZE_BYTES 4
#define ERROR_GPIO_ADDR_MAPS ((gpio_addr_maps_t) {.button_addr = 0x0, .sw_addr = 0x0})
#define ERROR_U32_PTR ((uint32_t*) 0xFFFFFFFF)

#define BUTTON_BASE_ADDR 0x41210000
#define BUTTON_REG_COUNT 2

#define SWITCH_BASE_ADDR 0x41220000
#define SWITCH_REG_COUNT 2

#define MEM_DEVICE "/dev/mem"

typedef struct gpio_addr_maps
{
	uint32_t* button_addr;
	uint32_t* sw_addr;
} gpio_addr_maps_t;

typedef enum buttons
{
	CENTER = 0x1, DOWN = 0x2, LEFT = 0x4, RIGHT = 0x8, UP = 0x10, NONE = 0x0
} t_buttons;


gpio_addr_maps_t init_interface();
void cleanup_interface(gpio_addr_maps_t maps);

uint32_t get_button_states(gpio_addr_maps_t map);

uint32_t get_switch_states(gpio_addr_maps_t map);

int button_pressed(t_buttons button, uint32_t state);

#endif
