/*
 * CPRE 488 MP3 - Digital Camera Pipeline
 * Authors: Conner Ohnesorge, Nolan Eastburn, Owen Parker, Jason Xie
 * Copyright (c) 2025
 */

#include "control-interface.h"

gpio_addr_maps_t init_interface() {
  long page_size = sysconf(_SC_PAGE_SIZE);

  // Setup GPIO mapping using mmap()
  gpio_addr_maps_t map;

  // Open memory device
  int mem = open(MEM_DEVICE, O_RDWR);

  if (mem < 0) {
    printf("ERROR: Could not open %s\n", MEM_DEVICE);
    perror("Error Description: ");
    return ERROR_GPIO_ADDR_MAPS;
  }

  // Make sure that button address is a multiple of page_size
  if (BUTTON_BASE_ADDR % page_size) {
    printf("ERROR: Button address is not a multiple of page_size! "
           "Addr: %x, Page Size: %lx\n",
           BUTTON_BASE_ADDR, page_size);
    return ERROR_GPIO_ADDR_MAPS;
  } else {
    map.button_addr = mmap(NULL, REG_SIZE_BYTES * BUTTON_REG_COUNT,
                           PROT_READ | PROT_WRITE, MAP_SHARED, mem,
                           BUTTON_BASE_ADDR);
    printf("Button Addr: %x\n", map.button_addr);
  }

  if (map.button_addr == ERROR_U32_PTR) {
    printf("ERROR: Could not get mapping to buttons!\n");
    perror("Error Description: ");
    return ERROR_GPIO_ADDR_MAPS;
  }

  // Make sure that switch address is a multiple of page_size
  if (SWITCH_BASE_ADDR % page_size) {
    printf("ERROR: Switch address is not a multiple of page_size! "
           "Addr: %x, Page Size: %lx\n",
           SWITCH_BASE_ADDR, page_size);
    return ERROR_GPIO_ADDR_MAPS;
  } else {
    map.sw_addr = mmap(NULL, REG_SIZE_BYTES * SWITCH_REG_COUNT,
                       PROT_READ | PROT_WRITE, MAP_SHARED, mem,
                       SWITCH_BASE_ADDR);
    printf("Switch Addr: %x\n", map.sw_addr);
  }

  if (map.sw_addr == ERROR_U32_PTR) {
    printf("ERROR: Could not get mapping to switches!\n");
    perror("Error Description: ");
    return ERROR_GPIO_ADDR_MAPS;
  }

  close(mem);

  // Since buttons and switches are inputs, set TRI reg to 0x1.
  map.button_addr[1] = 0x1;
  map.sw_addr[1] = 0x1;

  return map;
}

void cleanup_interface(gpio_addr_maps_t maps) {
  // Undo the GPIO tri state sets.
  maps.button_addr[1] = 0x0;
  maps.sw_addr[1] = 0x0;

  // Unmap physical memory
  int retval =
      munmap(maps.button_addr, REG_SIZE_BYTES * BUTTON_REG_COUNT);
  if (retval < 0) {
    printf("ERROR: Could not unmap buttons!\n");
    perror("Error Description: ");
  }

  retval = munmap(maps.sw_addr, REG_SIZE_BYTES * SWITCH_REG_COUNT);
  if (retval < 0) {
    printf("ERROR: Could not unmap switches!\n");
    perror("Error Description: ");
  }
}

uint32_t get_button_states(gpio_addr_maps_t map) {
  return map.button_addr[0];
}

uint32_t get_switch_states(gpio_addr_maps_t map) {
  return map.sw_addr[0];
}

int button_pressed(t_buttons button, uint32_t state) {
  return (state & (uint32_t)button) > 0;
}
