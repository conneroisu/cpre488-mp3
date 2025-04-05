#include "camera_app.h"
#include "platform.h"
#include "xaxis_switch.h"
#include "xil_types.h"
#include "xv_hcresampler.h"
#include "xv_letterbox.h"
#include "xv_vcresampler.h"
#include "xvidc.h"

camera_config_t camera_config;
/**
 *  This typedef enumerates the AXIS Switch Port for Sub-Core
 * connection
 */
typedef enum {
  XVPROCSS_SUBCORE_SCALER_V = 1,
  XVPROCSS_SUBCORE_SCALER_H,
  XVPROCSS_SUBCORE_VDMA,
  XVPROCSS_SUBCORE_LBOX,
  XVPROCSS_SUBCORE_CR_H,
  XVPROCSS_SUBCORE_CR_V_IN,
  XVPROCSS_SUBCORE_CR_V_OUT,
  XVPROCSS_SUBCORE_CSC,
  XVPROCSS_SUBCORE_DEINT,
  XVPROCSS_SUBCORE_MAX
} XVPROCSS_SUBCORE_ID;

/**
 * This typedef enumerates supported subsystem configuration topology
 */
typedef enum {
  XVPROCSS_TOPOLOGY_SCALER_ONLY = 0,
  XVPROCSS_TOPOLOGY_FULL_FLEDGED,
  XVPROCSS_TOPOLOGY_DEINTERLACE_ONLY,
  XVPROCSS_TOPOLOGY_CSC_ONLY,
  XVPROCSS_TOPOLOGY_VCRESAMPLE_ONLY,
  XVPROCSS_TOPOLOGY_HCRESAMPLE_ONLY,
  XVPROCSS_TOPOLOGY_NUM_SUPPORTED
} XVPROCSS_CONFIG_TOPOLOGY;

/**
 * This typedef enumerates types of Windows (Sub-frames) available in
 * the Subsystem
 */
typedef enum {
  XVPROCSS_ZOOM_WIN = 0,
  XVPROCSS_PIP_WIN,
  XVPROCSS_PIXEL_WIN,
  XVPROCSS_WIN_NUM_SUPPORTED
} XVprocSs_Win;

/**
 * This typedef enumerates supported scaling modes
 */
typedef enum {
  XVPROCSS_SCALE_1_1 = 0,
  XVPROCSS_SCALE_UP,
  XVPROCSS_SCALE_DN,
  XVPROCSS_SCALE_NOT_SUPPORTED
} XVprocSs_ScaleMode;

/** This typedef enumerates supported Color Channels
 *
 */
typedef enum {
  XVPROCSS_COLOR_CH_Y_RED = 0,
  XVPROCSS_COLOR_CH_CB_GREEN,
  XVPROCSS_COLOR_CH_CR_BLUE,
  XVPROCSS_COLOR_CH_NUM_SUPPORTED
} XVprocSs_ColorChannel;

/**
 * Video Processing Subsystem context scratch pad memory.
 * This contains internal flags, state variables, routing table
 * and other meta-data required by the subsystem. Each instance
 * of the subsystem will have its own context data memory
 */
typedef struct {
  XVidC_VideoWindow
      RdWindow; /**< window for Zoom/Pip feature support */
  XVidC_VideoWindow
      WrWindow; /**< window for Zoom/Pip feature support */

  UINTPTR DeintBufAddr; /**< Deinterlacer field buffer Addr. in DDR */
  u8 PixelWidthInBits;  /**< Number of bits required to store 1 pixel
                         */

  u8 RtngTable[XVPROCSS_SUBCORE_MAX]; /**< Storage for computed
                                         routing map */
  u8 StartCore[XVPROCSS_SUBCORE_MAX]; /**< Enable flag to start
                                         sub-core */
  u8 RtrNumCores;     /**< Number of sub-cores in routing map */
  u8 ScaleMode;       /**< Stored computed scaling mode - UP/DN/1:1 */
  u8 ZoomEn;          /**< Flag to store Zoom feature state */
  u8 PipEn;           /**< Flag to store PIP feature state */
  u16 VidInWidth;     /**< Input H Active */
  u16 VidInHeight;    /**< Input V Active */
  u16 PixelHStepSize; /**< Increment step size for Pip/Zoom window */
  XVidC_ColorFormat StrmCformat; /**< processing pipe color format */
  XVidC_ColorFormat CscIn;       /**< CSC core input color format */
  XVidC_ColorFormat CscOut;      /**< CSC core output color format */
  XVidC_ColorFormat
      HcrIn; /**< horiz. cresmplr core input color format */
  XVidC_ColorFormat
      HcrOut; /**< horiz. cresmplr core output color format */
  XLboxColorId LboxBkgndColor; /**< Lbox background color */
} XVprocSs_ContextData;

/**
 * Sub-Core Configuration Table
 */
typedef struct {
  u16 IsPresent;  /**< Flag to indicate if sub-core is present in the
                     design*/
  u16 DeviceId;   /**< Device ID of the sub-core */
  u32 AddrOffset; /**< sub-core offset from subsystem base address */
} XSubCore;

/**
 * Video Processing Subsystem configuration structure.
 * Each subsystem device should have a configuration structure
 * associated that defines the MAX supported sub-cores within
 * subsystem
 */

typedef struct {
  u16 DeviceId; /**< DeviceId is the unique ID  of the device */
  UINTPTR BaseAddress; /**< BaseAddress is the physical base address
                          of the subsystem address range */
  UINTPTR HighAddress; /**< HighAddress is the physical MAX address of
                          the subsystem address range */
  u8 Topology;         /**< Subsystem configuration mode */
  u8 PixPerClock;      /**< Number of Pixels Per Clock processed by
                          Subsystem */
  u16 ColorDepth;      /**< Processing precision of the data pipe */
  u16 NumVidComponents; /**< Number of Video Components */
  u16 MaxWidth;  /**< Maximum cols supported by subsystem instance */
  u16 MaxHeight; /**< Maximum rows supported by subsystem instance */
  u16 HasMADI;   /**< Motion Adaptive Deinterlacer available flag */
  XSubCore
      RstAximm; /**< Axi MM reset network instance configuration */
  XSubCore
      RstAxis; /**< Axi stream reset network instance configuration */
  XSubCore Vdma;        /**< Sub-core instance configuration */
  XSubCore Router;      /**< Sub-core instance configuration */
  XSubCore Csc;         /**< Sub-core instance configuration */
  XSubCore Deint;       /**< Sub-core instance configuration */
  XSubCore HCrsmplr;    /**< Sub-core instance configuration */
  XSubCore Hscale;      /**< Sub-core instance configuration */
  XSubCore Lbox;        /**< Sub-core instance configuration */
  XSubCore VCrsmplrIn;  /**< Sub-core instance configuration */
  XSubCore VCrsmplrOut; /**< Sub-core instance configuration */
  XSubCore Vscale;      /**< Sub-core instance configuration */
} XVprocSs_Config;

/**
 * The XVprocSs driver instance data. The user is required to allocate
 * a variable of this type for every XVprocSs device in the system. A
 * pointer to a variable of this type is then passed to the driver API
 * functions.
 */
typedef struct {
  XVprocSs_Config Config; /**< Hardware configuration */
  u32 IsReady;            /**< Device and the driver instance are
                        initialized */

  XAxis_Switch *RouterPtr; /**< handle to sub-core driver instance */
  XGpio *RstAxisPtr;       /**< handle to sub-core driver instance */
  XGpio *RstAximmPtr;      /**< handle to sub-core driver instance */

  XV_Hcresampler_l2
      *HcrsmplrPtr; /**< handle to sub-core driver instance */
  XV_Vcresampler_l2
      *VcrsmplrInPtr; /**< handle to sub-core driver instance */
  XV_Vcresampler_l2
      *VcrsmplrOutPtr; /**< handle to sub-core driver instance */
  XV_Vscaler_l2
      *VscalerPtr; /**< handle to sub-core driver instance */
  XV_Hscaler_l2
      *HscalerPtr;       /**< handle to sub-core driver instance */
  XAxiVdma *VdmaPtr;     /**< handle to sub-core driver instance */
  XV_Lbox_l2 *LboxPtr;   /**< handle to sub-core driver instance */
  XV_Csc_l2 *CscPtr;     /**< handle to sub-core driver instance */
  XV_Deint_l2 *DeintPtr; /**< handle to sub-core driver instance */

  // I/O Streams
  XVidC_VideoStream VidIn;  /**< Input  AXIS configuration */
  XVidC_VideoStream VidOut; /**< Output AXIS configuration */

  XVprocSs_ContextData CtxtData; /**< Internal Scratch pad memory for
                                    subsystem instance */
  UINTPTR
  FrameBufBaseaddr; /**< Base address for frame buffer storage */

  XVidC_DelayHandler
      UsrDelayUs;  /**< custom user function for delay/sleep */
  void *UsrTmrPtr; /**< handle to timer instance used by user
                       delay function */

#ifdef XV_VPROCSS_LOG_ENABLE
  XVprocSs_Log Log; /**< A log of events. */
#endif
} XVprocSs;

void set_park_frame(XAxiVdma *vdma, u8 frame, u16 dir) {
#define PARK *((volatile u32 *)(vdma->BaseAddr + 0x00000028))

  u32 mask = 0;
  u32 shift_amt = 0;

  if (dir == XAXIVDMA_READ) {
    mask = ~0x1F;
  } else if (dir == XAXIVDMA_WRITE) {
    mask = ~0x1F0;
    shift_amt = 8;
  }

  PARK = (PARK & mask) | ((u32)(frame & 0x1F) << shift_amt);

#undef PARK
}

// Initialize the camera configuration data structure
void camera_config_init(camera_config_t *config) {
  config->uBaseAddr_IIC_FmcIpmi = 0x41610000; // Device for reading
                                              // HDMI board IPMI
                                              // EEPROM information

  config->uBaseAddr_IIC_FmcImageon = 0x41600000; // Device Config

  config->uBaseAddr_VITA_SPI = 0x43C30000; // Config Device

  config->uBaseAddr_VITA_CAM = 0x43C20000; // Device for receiving

  config->uDeviceId_VTC_tpg = 0; // Video Timer Controller (VTC) ID
  config->uDeviceId_VDMA_HdmiFrameBuffer = 0U; // VDMA ID
  config->uBaseAddr_MEM_HdmiFrameBuffer =
      0x00000000U + 0x10000000; // VDMA base address for Frame buffers
  config->uNumFrames_HdmiFrameBuffer =
      XPAR_AXIVDMA_0_NUM_FSTORES; // NUmber of VDMA Frame buffers

  return;
}

XVprocSs proc_ss_RGB_YCrCb_444;
XVprocSs proc_ss_444_to_422;
XVprocSs_Config *Config_ptr;
XVprocSs_Config *Config_ptr_422;

int fmc_imageon_enable_vita(camera_config_t *config) {
  int ret;

  // VITA-2000 Initialization
  ret = onsemi_vita_sensor_initialize(&(config->onsemi_vita), 101,
                                      config->bVerbose);
  if (ret == 0) {
    xil_printf("VITA sensor init failed\n\r");
    return -1;
  }

  onsemi_vita_sensor_initialize(&(config->onsemi_vita), 103,
                                config->bVerbose);
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
                                  0x43C10000 //
  );
  if (result != 0) {
    xil_printf("fail init 4:4:4 to 4:2:2\n\r");
    return -1;
  }

  // Set Up HW REG Width for SS1
  Xil_Out16((0x43C10000) + (0x10),
            (u16)(1920) // Number of Active Pixels per Scanline
  );
  // Set Up HW REG Height for SS1
  Xil_Out16((0x43C10000) + (0x18),
            (u16)(1080) // Number of Active Lines per Frame
  );
  // Set HW REG Input Video Format for SS1
  Xil_Out8((0x43C10000) + (0x20), (u8)(0x01));
  // Set HW REG Output Video Format for SS1
  Xil_Out8((0x43C10000) + (0x28), (u8)(0x02));
  // Set Mode for SS1
  Xil_Out32((0x43C10000) + (0x00),
            (u32)(0x81) // Control 0x10000001 means start and freerun
                        // mode (page 16 in PG231)
  );

  XVprocSs_Start(&proc_ss_444_to_422);

  Config_ptr = XVprocSs_LookupConfig(0);

  result = XVprocSs_CfgInitialize(&proc_ss_RGB_YCrCb_444, Config_ptr,
                                  0x43C00000 //
  );
  if (result != 0) {
    xil_printf("failed init RGB to 4:4:4\n\r");
    return -1;
  }

  result = XV_CscSetColorspace(proc_ss_RGB_YCrCb_444.CscPtr,
                               0, //
                               1, //
                               1, //
                               1, //
                               2  //
  );
  if (result != 0) {
    xil_printf("failed colorspace RGB to YCrCb 4:4:4\n\r");
    return -1;
  }

  result = XVprocSs_SetSubsystemConfig(&proc_ss_RGB_YCrCb_444);
  if (result != 0) {
    xil_printf("failed ss configuration for RGB to 4:4:4\n\r");
    return -1;
  }
  result = XV_CscSetColorspace(proc_ss_RGB_YCrCb_444.CscPtr,
                               0, //
                               1, //
                               1, //
                               1, //
                               2  //
  );
  if (result != 0) {
    xil_printf("failed colorspace RGB to YCrCb 4:4:4\n\r");
    return -1;
  }
  XVprocSs_Start(&proc_ss_RGB_YCrCb_444);

  // # Demosaic Bayer Pattern to 24b RGB IP Setup (PG286)

  // Additional Register 1 (Demosaic)
  // Active Width Configuration (Number of Active Pixels per Scanline)
  Xil_Out32((0x43C40000) + (0x10),
            (u32)(1920) // Number of Active Pixels per Scanline
  );

  // Additional Register 2 (Demosaic)
  // Active Height Configuration (Number of Active Scanlines per
  // Frame)
  Xil_Out32((0x43C40000) + (0x18),
            (u32)(1080) // Number of Active Lines per Frame
  );

  // Additional Register 3 (Demosaic)
  // Bayer Phase Configuration (Bayer Pattern)
  Xil_Out32((0x43C40000) + (0x28),
            (u32)(0) // Bayer sampling grid starting postition
  );

  // 0b10000001 means start and freerun mode (page 16 in PG286)
  Xil_Out32((0x43C40000) + (0x00),
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

  fmc_imageon_iic_mux(&(config->fmc_imageon), 3);

  for (i = 0; i < 3; i++) {
    config->fmc_imageon.pIIC->fpIicWrite(
        config->fmc_imageon.pIIC, 0x65,
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
    return 1;
  }

  // FMC Module Validation
  if (fmc_ipmi_detect(&(config->fmc_ipmi_iic), "FMC-IMAGEON", 0)) {
    fmc_ipmi_enable(&(config->fmc_ipmi_iic), 1);
  } else {
    xil_printf("fail validate FMC-IPMI I2C Controller.\n\r");
    return 1;
  }

  ret = fmc_iic_axi_init(&(config->fmc_imageon_iic),
                         "FMC-IMAGEON I2C Controller",
                         config->uBaseAddr_IIC_FmcImageon);
  if (!ret) {
    xil_printf("fail open IIC\n\r");
    return 1;
  }

  fmc_imageon_init(&(config->fmc_imageon), "FMC-IMAGEON",
                   &(config->fmc_imageon_iic));
  fmc_imageon_vclk_init(&(config->fmc_imageon));
  fmc_imageon_vclk_config(&(config->fmc_imageon), 6);

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
    if (vita_enable_attempt > 3) {
      xil_printf("VITA Camera failed init after %d attempts\r\n", 3);
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

// Picture data
// Main function. Initializes the devices and configures VDMA
int camera_main() {
  init_platform();

  camera_config_init(&camera_config);
  //	fmc_imageon_enable(&camera_config);

  // Park both READ and WRITE channels on frame 1.
  set_park_frame(&(camera_config.vdma_hdmi), 1, 1);
  set_park_frame(&(camera_config.vdma_hdmi), 1, 2);

  // Enable park.G
#define READ_CR                                                      \
  *((volatile u32 *)(camera_config.vdma_hdmi.BaseAddr + 0x00000030));
#define WRITE_CR *((volatile u32 *)(camera_config.vdma_hdmi.BaseAddr))

  READ_CR &= ~0x2;
  WRITE_CR &= ~0x2;

#undef READ_CR
#undef WRITE_CR
  return 0;
}
