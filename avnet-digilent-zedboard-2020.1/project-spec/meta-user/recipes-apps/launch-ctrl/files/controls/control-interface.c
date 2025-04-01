#include "control-interface.h"
#define DREF_REG(reg_addr, offset) (volatile uint32_t*) ((uint32_t))

gpio_addr_maps_t init_interface()
{
	long page_size = sysconf(_SC_PAGE_SIZE);
	printf("Page Size: %lf\n", page_size);

	// Setup GPIO mapping using mmap()
	gpio_addr_maps_t map;

	// Open memory device
	int mem = open(MEM_DEVICE, O_RDWR);

	if(mem < 0)
	{
		printf("ERROR: Could not open %s\n", MEM_DEVICE);
		return ERROR_GPIO_ADDR_MAPS;
	}

	printf("Opened /dev/mem\n");

	map.button_addr = (uint32_t*) mmap(NULL, REG_SIZE_BYTES * BUTTON_REG_COUNT, PROT_READ | PROT_WRITE, MAP_PRIVATE, mem, BUTTON_BASE_ADDR / page_size);

	if(map.button_addr < 0)
	{
		printf("ERROR: Could not get mapping to buttons!\n");
		return ERROR_GPIO_ADDR_MAPS;
	}

	printf("Mapped buttons\n");

	map.sw_addr = (uint32_t*) mmap(NULL, REG_SIZE_BYTES * SWITCH_REG_COUNT, PROT_READ | PROT_WRITE, MAP_PRIVATE, mem, SWITCH_BASE_ADDR / page_size);

	if(map.sw_addr < 0)
	{
		printf("ERROR: Could not get mapping to switches!\n");
		return ERROR_GPIO_ADDR_MAPS;
	}

	printf("Mapped switches\n");

	close(mem);

	// Since buttons and switches are inputs, set TRI reg to 0x1.
	map.button_addr[1] = 0x1;
	map.sw_addr[1] = 0x1;

	printf("Configured gpio\n");

	return map;
}

uint32_t get_button_states(gpio_addr_maps_t map)
{
	return map.button_addr[0];
}

uint32_t get_switch_states(gpio_addr_maps_t map)
{
	return map.sw_addr[0];
}

int button_pressed(t_buttons button, uint32_t state)
{
	return (state & (uint32_t) button) > 0;
}
