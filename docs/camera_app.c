#include "camera_app.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unistd.h"

camera_config_t camera_config;

// Function prototypes
void camera_config_init(camera_config_t *config);
void camera_loop(camera_config_t *config);
void camera_interface(camera_config_t *config, Xuint16 *rawBayer, int *StoreimageIndex, int *imageIndex, int *playMode);

// Main function. Initializes the devices and configures VDMA
int camera_main() {
    camera_config_init(&camera_config);

    if (fmc_imageon_enable(&camera_config) != 0) {
        return -1;
    }

    return 0;
}

// Initialize the camera configuration data structure
void camera_config_init(camera_config_t *config) {
    config->uBaseAddr_IIC_FmcIpmi =  0x41610000;   // Device for reading HDMI board IPMI EEPROM information
    config->uBaseAddr_IIC_FmcImageon = 0x41600000;    // Device for configuring the HDMI board

    // Uncomment when using VITA Camera for Video input
    config->uBaseAddr_VITA_SPI = 0x43C20000;  // Device for configuring the Camera sensor
    config->uBaseAddr_VITA_CAM = 0x43C30000;  // Device for receiving Camera sensor data

    // Uncomment when using the TPG for Video input
//    config->uBaseAddr_TPG_PatternGenerator = XPAR_V_TPG_0_S_AXI_CTRL_BASEADDR; // TPG Device

    config->uDeviceId_VTC_tpg   = 0;                        // Video Timer Controller (VTC) ID
    config->uDeviceId_VDMA_HdmiFrameBuffer = 0U;         // VDMA ID
    config->uBaseAddr_MEM_HdmiFrameBuffer = 0x00000000U + 0x10000000; // VDMA base address for Frame buffers
    config->uNumFrames_HdmiFrameBuffer = 3U;            // Number of VDMA Frame buffers
    return;
}
