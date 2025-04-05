#include "camera_app.h"
#include "platform.h"
#include "xil_types.h"

camera_config_t camera_config;

void set_park_frame(XAxiVdma *vdma, u8 frame, u16 dir) {
#define PARK                                                         \
  *((volatile u32 *)(vdma->BaseAddr + XAXIVDMA_PARKPTR_OFFSET))

	u32 mask = 0;
	u32 shift_amt = 0;

	if (dir == XAXIVDMA_READ) {
		mask = ~0x1F;
	} else if (dir == XAXIVDMA_WRITE) {
		mask = ~0x1F0;
		shift_amt = 8;
	}

	PARK = (PARK & mask) | ((u32) (frame & 0x1F) << shift_amt);

#undef PARK
}

// Initialize the camera configuration data structure
void camera_config_init(camera_config_t *config) {
	config->uBaseAddr_IIC_FmcIpmi =
	XPAR_FMC_IPMI_ID_EEPROM_0_BASEADDR; // Device for reading HDMI
										// board IPMI EEPROM
										// information
	config->uBaseAddr_IIC_FmcImageon =
	XPAR_FMC_IMAGEON_IIC_0_BASEADDR; // Device for configuring the
									 // HDMI board

	config->uBaseAddr_VITA_SPI =
	XPAR_ONSEMI_VITA_SPI_0_S00_AXI_BASEADDR; // Config Device

	config->uBaseAddr_VITA_CAM =
	XPAR_ONSEMI_VITA_CAM_0_S00_AXI_BASEADDR; // Device for receiving

	config->uDeviceId_VTC_tpg =
	XPAR_V_TC_0_DEVICE_ID; // Video Timer Controller (VTC) ID
	config->uDeviceId_VDMA_HdmiFrameBuffer =
	XPAR_AXI_VDMA_0_DEVICE_ID; // VDMA ID
	config->uBaseAddr_MEM_HdmiFrameBuffer =
	XPAR_DDR_MEM_BASEADDR + 0x10000000; // VDMA base address for Frame buffers
	config->uNumFrames_HdmiFrameBuffer =
	XPAR_AXIVDMA_0_NUM_FSTORES; // NUmber of VDMA Frame buffers

	return;
}

// Picture data
// Main function. Initializes the devices and configures VDMA
int camera_main() {
	init_platform();

	camera_config_init(&camera_config);
//	fmc_imageon_enable(&camera_config);

	// Park both READ and WRITE channels on frame 1.
	set_park_frame(&(camera_config.vdma_hdmi), 1, XAXIVDMA_WRITE);
	set_park_frame(&(camera_config.vdma_hdmi), 1, XAXIVDMA_READ);

	// Enable park.G
#define READ_CR                                                      \
  *((volatile u32 *)(camera_config.vdma_hdmi.BaseAddr +              \
                     XAXIVDMA_RX_OFFSET + XAXIVDMA_CR_OFFSET))
#define WRITE_CR                                                     \
  *((volatile u32 *)(camera_config.vdma_hdmi.BaseAddr +              \
                     XAXIVDMA_TX_OFFSET + XAXIVDMA_CR_OFFSET))

	READ_CR &= ~0x2;
	WRITE_CR &= ~0x2;

#undef READ_CR
#undef WRITE_CR
	return 0;
}
