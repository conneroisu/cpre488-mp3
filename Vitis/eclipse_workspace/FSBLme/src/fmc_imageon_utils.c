#include "camera_app.h"
#include "xil_cache.h"
#include "xil_types.h"

XVprocSs proc_ss_RGB_YCrCb_444;
XVprocSs proc_ss_444_to_422;
XVprocSs_Config *Config_ptr;
XVprocSs_Config *Config_ptr_422;

#define VITA_ENABLE_ATTEMPT_LIMIT 3
#define INCR_DECR_VALUE 1
#define XPAR_XV_DEMOSAIC_0_DEVICE_ID XPAR_V_DEMOSAIC_0_DEVICE_ID
#define XPAR_XV_DEMOSAIC_0_S_AXI_CTRL_BASEADDR 0x43C40000
#define XPAR_XV_DEMOSAIC_0_S_AXI_CTRL_HIGHADDR 0x43C4FFFF
#define XPAR_XV_DEMOSAIC_0_SAMPLES_PER_CLOCK 1
#define XPAR_XV_DEMOSAIC_0_MAX_COLS 3840
#define XPAR_XV_DEMOSAIC_0_MAX_ROWS 2160
#define XPAR_XV_DEMOSAIC_0_MAX_DATA_WIDTH 8
#define XPAR_XV_DEMOSAIC_0_ALGORITHM 1
#define XV_DEMOSAIC_CTRL_ADDR_AP_CTRL 0x00
#define XV_DEMOSAIC_CTRL_ADDR_GIE 0x04
#define XV_DEMOSAIC_CTRL_ADDR_IER 0x08
#define XV_DEMOSAIC_CTRL_ADDR_ISR 0x0c
#define XV_DEMOSAIC_CTRL_ADDR_HWREG_WIDTH_DATA 0x10
#define XV_DEMOSAIC_CTRL_BITS_HWREG_WIDTH_DATA 16
#define XV_DEMOSAIC_CTRL_ADDR_HWREG_HEIGHT_DATA 0x18
#define XV_DEMOSAIC_CTRL_BITS_HWREG_HEIGHT_DATA 16
#define XV_DEMOSAIC_CTRL_ADDR_HWREG_BAYER_PHASE_DATA 0x28
#define XV_DEMOSAIC_CTRL_BITS_HWREG_BAYER_PHASE_DATA 16

/// @brief fmc_imageon_enable Enable the FMC Imageon camera
/// @param config the camera configuration to enable the camera with.
/// @return 0 if successful, -1 if not
int fmc_imageon_enable(camera_config_t *config) {
  int ret;

  config->bVerbose = 1;
  config->vita_aec = 0;       // off
  config->vita_again = 0;     // 1.0
  config->vita_dgain = 128;   // 1.0
  config->vita_exposure = 90; // 90% of frame period

  ret = fmc_iic_axi_init(&(config->fmc_ipmi_iic),
                         "FMC-IPMI I2C Controller",
                         config->uBaseAddr_IIC_FmcIpmi);
  if (!ret) {
    xil_printf("fail open impi\n\r");
    exit(1);
  }

  // FMC Module Validation
  if (fmc_ipmi_detect(&(config->fmc_ipmi_iic), "FMC-IMAGEON",
                      FMC_ID_ALL)) {
    fmc_ipmi_enable(&(config->fmc_ipmi_iic), FMC_ID_SLOT1);
  } else {
    xil_printf("fail validate FMC-IPMI I2C Controller.\n\r");
    exit(1);
  }

  ret = fmc_iic_axi_init(&(config->fmc_imageon_iic),
                         "FMC-IMAGEON I2C Controller",
                         config->uBaseAddr_IIC_FmcImageon);
  if (!ret) {
    xil_printf("fail open IIC\n\r");
    exit(1);
  }

  fmc_imageon_init(&(config->fmc_imageon), "FMC-IMAGEON",
                   &(config->fmc_imageon_iic));
  fmc_imageon_vclk_init(&(config->fmc_imageon));
  fmc_imageon_vclk_config(&(config->fmc_imageon),
                          FMC_IMAGEON_VCLK_FREQ_148_500_000);

  reset_dcms(config);

  config->hdmio_width = 1920;
  config->hdmio_height = 1080;
  config->hdmio_timing.IsHDMI = 0; // DVI Mode
  config->hdmio_timing.IsEncrypted = 0;
  config->hdmio_timing.IsInterlaced = 0;
  config->hdmio_timing.ColorDepth = 8;
  config->hdmio_timing.HActiveVideo = 1920;
  config->hdmio_timing.HFrontPorch = 88;
  config->hdmio_timing.HSyncWidth = 44;
  config->hdmio_timing.HSyncPolarity = 1;
  config->hdmio_timing.HBackPorch = 148;
  config->hdmio_timing.VActiveVideo = 1080;
  config->hdmio_timing.VFrontPorch = 4;
  config->hdmio_timing.VSyncWidth = 5;
  config->hdmio_timing.VSyncPolarity = 1;
  config->hdmio_timing.VBackPorch = 36;

  config->hdmio_resolution =
      vres_detect(config->hdmio_width, config->hdmio_height);
  // xil_printf("\tVideo Resolution = %s\n\r",
  //            vres_get_name(config->hdmio_resolution));

  vgen_init(&(config->vtc_tpg), config->uDeviceId_VTC_tpg);
  vgen_config(&(config->vtc_tpg), config->hdmio_resolution, 1);

  // FMC-IMAGEON HDMI Output Initialization
  ret = fmc_imageon_hdmio_init(&(config->fmc_imageon), 1,
                               &(config->hdmio_timing), 0);
  if (!ret) {
    xil_printf("fail init imageon HDMI\n\r");
    return 0;
  }

  // FMC-IMAGEON VITA Camera Receiver Initialization
  onsemi_vita_init(
      &(config->onsemi_vita), "VITA-2000", config->uBaseAddr_VITA_SPI,
      config->uBaseAddr_VITA_CAM // Base Address of VITA CAM
  );
  config->onsemi_vita.uManualTap = 25;
  // Assuming a 75 MHz AXI-Lite SPI bus
  onsemi_vita_spi_config(
      &(config->onsemi_vita),
      (75000000 / 10000000) // AXI-Lite SPI Speed (HZ) / 10,000,000 Hz
  );

  // Enable spread-spectrum clocking (SSC)
  enable_ssc(config);

  // Clear frame stores
  Xuint32 i;
  Xuint32 storage_size =
      config->uNumFrames_HdmiFrameBuffer * ((1920 * 1080) << 1);
  volatile Xuint32 *pStorageMem =
      (Xuint32 *)config->uBaseAddr_MEM_HdmiFrameBuffer;

  // Frame #1 - Red pixels
  for (i = 0; i < storage_size / config->uNumFrames_HdmiFrameBuffer;
       i += 4) {
    *pStorageMem++ = 0xF0525A52; // Red
  }
  // Frame #2 - Green pixels
  for (i = 0; i < storage_size / config->uNumFrames_HdmiFrameBuffer;
       i += 4) {
    *pStorageMem++ = 0x36912291; // Green
  }
  // Frame #3 - Blue pixels
  for (i = 0; i < storage_size / config->uNumFrames_HdmiFrameBuffer;
       i += 4) {
    *pStorageMem++ = 0x6E29F029; // Blue
  }

  Xil_DCacheFlush(); // Flush Cache

  // Initialize Output Side of AXI VDMA
  vfb_common_init(config->uDeviceId_VDMA_HdmiFrameBuffer, // uDeviceId
                  &(config->vdma_hdmi)                    // pAxiVdma
  );

  // Output static Frame buffer for 5 seconds
  sleep(5);

  // Initialize Input Side of AXI VDMA
  int vita_enabled_error = 0;
  int vita_enable_attempt = 1;
  do {
    vita_enabled_error = fmc_imageon_enable_vita(config);
    if (vita_enable_attempt > VITA_ENABLE_ATTEMPT_LIMIT) {
      xil_printf("VITA Camera failed init after %d attempts\r\n",
                 VITA_ENABLE_ATTEMPT_LIMIT);
      return -1;
    }
  } while (vita_enabled_error != 0);

  fmc_imageon_enable_ipipe(config);

  sleep(1);

  // Status of AXI VDMA
  vfb_dump_registers(&(config->vdma_hdmi));
  if (vfb_check_errors(&(config->vdma_hdmi),
                       1 /*clear errors, if any*/)) {
    vfb_dump_registers(&(config->vdma_hdmi));
  }

  return 0;
}

int fmc_imageon_enable_vita(camera_config_t *config) {
  int ret;

  // VITA-2000 Initialization
  ret = onsemi_vita_sensor_initialize(
      &(config->onsemi_vita), SENSOR_INIT_ENABLE, config->bVerbose);
  if (ret == 0) {
    xil_printf("VITA sensor init failed\n\r");
    return -1;
  }

  onsemi_vita_sensor_initialize(
      &(config->onsemi_vita), SENSOR_INIT_STREAMON, config->bVerbose);
  sleep(1);

  ret = onsemi_vita_sensor_1080P60(&(config->onsemi_vita),
                                   config->bVerbose);
  if (ret == 0) {
    xil_printf("configure timing VITA failed\n\r");
    return -1;
  }
  sleep(1);

  onsemi_vita_get_status(&(config->onsemi_vita),
                         &(config->vita_status_t1),
                         0 /*config->bVerbose*/);
  sleep(1);
  onsemi_vita_get_status(&(config->onsemi_vita),
                         &(config->vita_status_t2),
                         0 /*config->bVerbose*/);

  int vita_width, vita_height, vita_rate;
  vita_width = config->vita_status_t1.cntImagePixels * 4;
  vita_height = config->vita_status_t1.cntImageLines;
  vita_rate = config->vita_status_t2.cntFrames -
              config->vita_status_t1.cntFrames;

  if (config->bVerbose) {
    onsemi_vita_get_status(&(config->onsemi_vita),
                           &(config->vita_status_t2), 1);
  }

  if ((vita_width != 1920) || (vita_height != 1080) ||
      (vita_rate == 0)) {
    return 1;
  }

  return 0;
}
int fmc_imageon_enable_ipipe(camera_config_t *config) {

  int result;

  // # Re-Sampling Subsystem IP Setup (PG231)
  // 444 => 422

  Config_ptr_422 = XVprocSs_LookupConfig(XPAR_XVPROCSS_1_DEVICE_ID);

  result = XVprocSs_CfgInitialize(&proc_ss_444_to_422, Config_ptr_422,
                                  XPAR_XVPROCSS_1_BASEADDR //
  );
  if (result != XST_SUCCESS) {
    xil_printf("Error init 4:4:4 to 4:2:2\n\r");
    return -1;
  }

  // Set Up HW REG Width for SS1
  Xil_Out16((XPAR_V_PROC_SS_1_BASEADDR) +
                (XV_HCRESAMPLER_CTRL_ADDR_HWREG_WIDTH_DATA),
            (u16)(1920) // Number of Active Pixels per Scanline
  );
  // Set Up HW REG Height for SS1
  Xil_Out16((XPAR_V_PROC_SS_1_BASEADDR) +
                (XV_HCRESAMPLER_CTRL_ADDR_HWREG_HEIGHT_DATA),
            (u16)(1080) // Number of Active Lines per Frame
  );
  // Set HW REG Input Video Format for SS1
  Xil_Out8(
      (XPAR_V_PROC_SS_1_BASEADDR) +
          (XV_HCRESAMPLER_CTRL_ADDR_HWREG_INPUT_VIDEO_FORMAT_DATA),
      (u8)(0x01));
  // Set HW REG Output Video Format for SS1
  Xil_Out8(
      (XPAR_V_PROC_SS_1_BASEADDR) +
          (XV_HCRESAMPLER_CTRL_ADDR_HWREG_OUTPUT_VIDEO_FORMAT_DATA),
      (u8)(0x02));
  // Set Mode for SS1
  Xil_Out32((XPAR_V_PROC_SS_1_BASEADDR) +
                (XV_HCRESAMPLER_CTRL_ADDR_AP_CTRL),
            (u32)(0x81) // Control 0x10000001 means start and freerun
                        // mode (page 16 in PG231)
  );

  XVprocSs_Start(&proc_ss_444_to_422);

  Config_ptr = XVprocSs_LookupConfig(XPAR_XVPROCSS_0_DEVICE_ID);

  result = XVprocSs_CfgInitialize(&proc_ss_RGB_YCrCb_444, Config_ptr,
                                  XPAR_XVPROCSS_0_BASEADDR //
  );
  if (result != XST_SUCCESS) {
    xil_printf("failed init RGB to 4:4:4\n\r");
    return -1;
  }

  result = XV_CscSetColorspace(proc_ss_RGB_YCrCb_444.CscPtr,
                               XVIDC_CSF_RGB,       //
                               XVIDC_CSF_YCRCB_444, //
                               XVIDC_BT_709,        //
                               XVIDC_BT_709,        //
                               XVIDC_CR_0_255       //
  );
  if (result != XST_SUCCESS) {
    xil_printf("failed colorspace RGB to YCrCb 4:4:4\n\r");
    return -1;
  }

  result = XVprocSs_SetSubsystemConfig(&proc_ss_RGB_YCrCb_444);
  if (result != XST_SUCCESS) {
    xil_printf("failed ss configuration for RGB to 4:4:4\n\r");
    return -1;
  }
  result = XV_CscSetColorspace(proc_ss_RGB_YCrCb_444.CscPtr,
                               XVIDC_CSF_RGB,       //
                               XVIDC_CSF_YCRCB_444, //
                               XVIDC_BT_709,        //
                               XVIDC_BT_709,        //
                               XVIDC_CR_0_255       //
  );
  if (result != XST_SUCCESS) {
    xil_printf("failed colorspace RGB to YCrCb 4:4:4\n\r");
    return -1;
  }
  XVprocSs_Start(&proc_ss_RGB_YCrCb_444);

  // # Demosaic Bayer Pattern to 24b RGB IP Setup (PG286)

  // Additional Register 1 (Demosaic)
  // Active Width Configuration (Number of Active Pixels per Scanline)
  Xil_Out32((XPAR_XV_DEMOSAIC_0_S_AXI_CTRL_BASEADDR) +
                (XV_DEMOSAIC_CTRL_ADDR_HWREG_WIDTH_DATA),
            (u32)(1920) // Number of Active Pixels per Scanline
  );

  // Additional Register 2 (Demosaic)
  // Active Height Configuration (Number of Active Scanlines per
  // Frame)
  Xil_Out32((XPAR_XV_DEMOSAIC_0_S_AXI_CTRL_BASEADDR) +
                (XV_DEMOSAIC_CTRL_ADDR_HWREG_HEIGHT_DATA),
            (u32)(1080) // Number of Active Lines per Frame
  );

  // Additional Register 3 (Demosaic)
  // Bayer Phase Configuration (Bayer Pattern)
  Xil_Out32((XPAR_XV_DEMOSAIC_0_S_AXI_CTRL_BASEADDR) +
                (XV_DEMOSAIC_CTRL_ADDR_HWREG_BAYER_PHASE_DATA),
            (u32)(0) // Bayer sampling grid starting postition
  );

  // 0b10000001 means start and freerun mode (page 16 in PG286)
  Xil_Out32((XPAR_XV_DEMOSAIC_0_S_AXI_CTRL_BASEADDR) +
                (XV_DEMOSAIC_CTRL_ADDR_AP_CTRL),
            (u32)(0x81) // start and freerun mode (page 16 in PG286)
  );

  // Demosaic IP Configuring and Enable done

  return 0;
}

/// @brief enable_ssc Enable the spread-spectrum clocking (SSC) on the
/// camera
/// @param config
void enable_ssc(camera_config_t *config) {
  int i;

  Xuint8 iic_cdce913_ssc_on[3][2] = {
      {0x10, 0x6D}, // SSC = 011 (0.75%)
      {0x11, 0xB6}, //
      {0x12, 0xDB}  //
  };

  fmc_imageon_iic_mux(&(config->fmc_imageon),
                      FMC_IMAGEON_I2C_SELECT_VID_CLK);

  for (i = 0; i < 3; i++) {
    config->fmc_imageon.pIIC->fpIicWrite(
        config->fmc_imageon.pIIC, FMC_IMAGEON_VID_CLK_ADDR,
        (0x80 | iic_cdce913_ssc_on[i][0]),
        &(iic_cdce913_ssc_on[i][1]), 1 //
    );
  }

  return;
}

// Toggles the reset on the DCM core (clock generator)
void reset_dcms(camera_config_t *config) {

  Xuint32 value;

  // Force reset high
  config->fmc_ipmi_iic.fpGpoRead(&(config->fmc_ipmi_iic), &value);
  value = value | 0x00000004; // Force bit 2 to 1
  config->fmc_ipmi_iic.fpGpoWrite(&(config->fmc_ipmi_iic), value);
  usleep(200000);

  // Force reset low
  config->fmc_ipmi_iic.fpGpoRead(&(config->fmc_ipmi_iic), &value);
  value = value & ~0x00000004; // Force bit 2 to 0
  config->fmc_ipmi_iic.fpGpoWrite(&(config->fmc_ipmi_iic), value);
  usleep(500000);
}
