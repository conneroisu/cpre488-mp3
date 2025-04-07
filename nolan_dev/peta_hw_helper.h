#ifndef PETA_HW_HELPER_H__
#define PETA_HW_HELPER_H__

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#define MEM_DEVICE "/dev/mem"

#define NUM_FRAME_BUFFERS 5

#define ERROR_U32_PTR ((uint32_t*) 0xFFFFFFFF)

// Xparams
#define VDMA_BASE 0x43000000
#define PARK_OFFSET 0x28
#define WRITE_START_ADDR_OFFSET 0xAC
#define ERROR_HW_MAP ((hw_map_t) {.reg = NULL, .size = 0})

// In bytes
#define REG_SIZE 4

typedef struct hw_map
{
    uint32_t* reg;
    size_t size;
} hw_map_t;

typedef struct hw_maps
{
    hw_map_t vdma_write_start_addrs;
    hw_map_t vdma_park_addr;
} hw_maps_t;

hw_maps_t get_maps();
void destroy_maps(hw_maps_t maps);
uint8_t get_current_frame_pointer(hw_maps_t maps);
void set_park_frame(uint8_t frame, hw_maps_t maps);

#endif