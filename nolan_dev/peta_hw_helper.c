#include "peta_hw_helper.h"

hw_map_t create_hw_mapping(uint32_t addr, size_t size)
{
    hw_map_t result;

    // Check that addr is page-aligned
    long page_size = sysconf(_SC_PAGE_SIZE);

    if(!(((uint32_t) addr) % page_size))
    {
        pritnf("ERROR: %x is not a multiple of the page size, which is %lx\n", (uint32_t) addr, page_size);
        return ERROR_HW_MAP;
    }

    // Open memory device
	int mem = open(MEM_DEVICE, O_RDWR);

	if(mem < 0)
	{
		printf("ERROR: Could not open %s\n", MEM_DEVICE);
		perror("Error Description: ");
		return ERROR_HW_MAP;
	}

    result.reg = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, mem, (__off_t) addr);
    result.size = size;

	if(result.reg == ERROR_U32_PTR)
	{
		printf("ERROR: Could not get mapping to %x!\n", addr);
		perror("Error Description: ");
        close(mem);
		return ERROR_HW_MAP;
	}

    close(mem);
    return result;
}

void destroy_hw_mapping(hw_map_t map)
{
	int retval = munmap(map.reg, map.size);
	if(retval < 0)
	{
		printf("ERROR: Could not unmap %x!\n", (uint32_t) map.reg);
		perror("Error Description: ");
	}
}

hw_maps_t get_maps()
{
    hw_maps_t result;

    // Park
    result.vdma_park_addr = create_hw_mapping(VDMA_BASE + PARK_OFFSET, REG_SIZE);

    if(result.vdma_park_addr.reg == NULL)
    {
        printf("ERROR: Could not map VDMA Park!\n");
        perror("Error Description: ");
    }

    // Write start addresses
    result.vdma_write_start_addrs = create_hw_mapping(VDMA_BASE + WRITE_START_ADDR_OFFSET, REG_SIZE * NUM_FRAME_BUFFERS);
    
    if(result.vdma_write_start_addrs.reg == NULL)
    {
        printf("ERROR: Could not map VDMA Write Start Addrs!\n");
        perror("Error Description: ");
    }

    return result;
}

void destroy_maps(hw_maps_t maps)
{
    destroy_hw_mapping(maps.vdma_park_addr);
    destroy_hw_mapping(maps.vdma_write_start_addrs);
}

uint8_t get_current_frame_pointer(hw_maps_t maps)
{
	uint8_t result = 0;

	uint32_t mask = 0;
	uint32_t shift_amt = 0;

    mask = 0x1F00000;
    shift_amt = 24;

	result = (maps.vdma_park_addr.reg[0] & mask) >> shift_amt;

	return result;
}

void set_park_frame(uint8_t frame, hw_maps_t maps)
{
	uint32_t mask = 0;
	uint32_t shift_amt = 0;
    mask = ~0x1F0;
    shift_amt = 8;

	maps.vdma_park_addr.reg[0] = (maps.vdma_park_addr.reg[0] & mask) | ((uint32_t)(frame & 0x1F) << shift_amt);
}