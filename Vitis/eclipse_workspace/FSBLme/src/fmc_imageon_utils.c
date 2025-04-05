#include "sleep.h"
#include "xenv.h"
#include "xvprocss_coreinit.h"
#include "xvprocss_router.h"
#include "xvprocss_vdma.h"
#include "camera_app.h"
#include "xil_cache.h"
#include "xil_types.h"
#include <stdio.h>
#include "xparameters.h"
#include "xstatus.h"
#include "fmc_iic.h"
#include "fmc_ipmi_fru.h"

/* HW Reset Network GPIO Channel */
#define GPIO_CH_RESET_SEL (1u)
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

#define FMC_ID_SLOT1 1 // defined to be 0xA0
#define FMC_ID_SLOT2 2 // defined to be 0xA2 or 0xA4
#define FMC_ID_ALL 0   // defined to be 0xA0, 0xA2, 0xA4, or 0xA6

#define XVPROCSS_RSTMASK_VIDEO_IN                                    \
  (0x01) /**< Reset line going out of vpss */
#define XVPROCSS_RSTMASK_IP_AXIS                                     \
  (0x02) /**< Reset line for vpss internal video IP blocks */
#define XVPROCSS_RSTMASK_IP_AXIMM                                    \
  (0x01) /**< Reset line for vpss internal AXI-MM blocks */
/*@}*/

#define XVPROCSS_RSTMASK_ALL_BLOCKS                                  \
  (XVPROCSS_RSTMASK_VIDEO_IN | XVPROCSS_RSTMASK_IP_AXIS)

/**************************** Type Definitions
 * *******************************/
/**
 * This typedef declares the driver instances of all the cores in the
 * subsystem
 */
typedef struct {
  XAxis_Switch Router;
  XGpio RstAxis;  // Reset for IP's running at AXIS Clk
  XGpio RstAximm; // Reset for IP's with AXI MM interface

  XV_Hcresampler_l2 Hcrsmplr;
  XV_Vcresampler_l2 VcrsmplrIn;
  XV_Vcresampler_l2 VcrsmplrOut;
  XV_Vscaler_l2 Vscaler;
  XV_Hscaler_l2 Hscaler;
  XAxiVdma Vdma;
  XV_Lbox_l2 Lbox;
  XV_Csc_l2 Csc;
  XV_Deint_l2 Deint;
} XVprocSs_SubCores;

// Define Driver instance of all sub-core included in the design */
XVprocSs_SubCores subcoreRepo[XPAR_XVPROCSS_NUM_INSTANCES];

/** @name VDMA Alignment required step size
 *
 * @{
 * The following constants define various Zoom/Pip window horizontal
 * step sizes that keeps the VDMA access aligned to aximm interface
 * width, based on Pixels/Clock and Color Depth of the configured
 * subsystem. This is required as current version of VDMA does not
 * support DRE if interface width is >64bits. Software has to align
 * the hsize and stride for all possible configurations supported by
 * the subsystem. Current subsystem version supports the following
 *  - Number of components = 3 (Fixed)
 *  - Pixels/Clock         = 1, 2, 4
 *  - Color Depth          = 8, 10, 12, 16   (4 variations)
 */
const u16 XVprocSs_PixelHStep[3][4] = {
    {16, 4, 32, 8},    // XVIDC_PPC_1
    {16, 4, 64, 16},   // XVIDC_PPC_2
    {32, 128, 128, 32} // XVIDC_PPC_4
};

/************************** Function Prototypes
 * ******************************/
static void SetPowerOnDefaultState(XVprocSs *XVprocSsPtr);
static void GetIncludedSubcores(XVprocSs *XVprocSsPtr);
static int ValidateSubsystemConfig(XVprocSs *InstancePtr);
static int ValidateScalerOnlyConfig(XVprocSs *XVprocSsPtr);
static int ValidateCscOnlyConfig(XVprocSs *XVprocSsPtr,
                                 u16 Allow422,
                                 u16 Allow420);
static int ValidateDeintOnlyConfig(XVprocSs *XVprocSsPtr);
static int ValidateVCResampleOnlyConfig(XVprocSs *XVprocSsPtr);
static int ValidateHCResampleOnlyConfig(XVprocSs *XVprocSsPtr);
static int SetupModeScalerOnly(XVprocSs *XVprocSsPtr);
static int SetupModeCscOnly(XVprocSs *XVprocSsPtr);
static int SetupModeDeintOnly(XVprocSs *XVprocSsPtr);
static int SetupModeVCResampleOnly(XVprocSs *XVprocSsPtr);
static int SetupModeHCResampleOnly(XVprocSs *XVprocSsPtr);
static int SetupModeMax(XVprocSs *XVprocSsPtr);




Xuint8 detect_ipmi_address(fmc_iic_t *pIIC, int fmcId) {
  Xuint8 ipmi_address = 0x00;
  Xuint8 min_address;
  Xuint8 max_address;

  Xuint8 iic_address;
  Xuint8 iic_data;

  switch (fmcId) {
  case FMC_ID_SLOT1:
    min_address = 0xA0;
    max_address = 0xA0;
    break;
  case FMC_ID_SLOT2:
    min_address = 0xA2;
    max_address = 0xA4;
    break;
  case FMC_ID_ALL:
    min_address = 0xA0;
    max_address = 0xA6;
    break;
  default:
    min_address = 0xA0;
    max_address = 0xA6;
    break;
  }

  for (ipmi_address = min_address; ipmi_address <= max_address;
       ipmi_address += 2) {
    // xil_printf( "Checking IPMI EEPROM at Address = 0x%02X\n\r",
    // ipmi_address );
    iic_address = ipmi_address >> 1;
    if (pIIC->fpIicRead(pIIC, iic_address, 0, &iic_data, 1)) {
      // xil_printf( "Detected IPMI EEPROM at Address = 0x%02X\n\r",
      // ipmi_address );
      break;
    }
  }
  // If none were detected in range, use the max value
  if (ipmi_address > max_address) {
    ipmi_address = max_address;
  }

  return ipmi_address;
}

int fmc_ipmi_detect(fmc_iic_t *pIIC, char *szExpected, int fmcId) {
  Xuint8 ipmi_eeprom_address;
  struct fru_area_board board_info;
  int retval;

  xil_printf("FMC Module Validation\n\r");

  // I2C Address of IPMI EEPROM
  ipmi_eeprom_address = detect_ipmi_address(pIIC, fmcId);
  // xil_printf( "IPMI EEPROM Address = 0x%02X\n\r",
  // ipmi_eeprom_address );

  // Read FRU Board Info from IPMI EEPROM
  retval =
      fmc_ipmi_get_board_info(pIIC, ipmi_eeprom_address, &board_info);
  if (retval == FRU_SUCCESS) {
    // Display Board Information
    xil_printf("Board Information:\n\r");
    xil_printf("\tManufacturer    = %s\n\r", board_info.mfg);
    xil_printf("\tProduct Name    = %s\n\r", board_info.prod);
    xil_printf("\tSerial Number   = %s\n\r", board_info.serial);
    xil_printf("\tPart Number     = %s\n\r", board_info.part);

    // Validate presence of FMC module
    if (!strcmp(board_info.prod, szExpected)) {
      xil_printf("SUCCESS : Detected %s module!\n\r",
                 board_info.prod);
      // fmc_ipmi_enable( pIIC, fmcId );
      return 1;
    } else {
      // Error due to unexpected FMC module
      xil_printf(
          "ERROR : Unexpected %s module, Expected %s module\n\r",
          board_info.prod, szExpected);
      // fmc_ipmi_disable( pIIC, fmcId );
      return 0;
    }
  } else if (retval == FRU_I2C_ERROR) {
    // Error due to unpopulated FMC slot
    xil_printf("ERROR : No FMC module detected\n\r");
    // fmc_ipmi_disable( pIIC, fmcId );
    return 0;
  } else {
    // Error due to invalid IPMI EEPROM
    xil_printf("ERROR : FMC module does not have valid FRU content "
               "in its IPMI EEPROM\n\r");
    // fmc_ipmi_disable( pIIC, fmcId );
    return 0;
  }
}

int fmc_ipmi_enable(fmc_iic_t *pIIC, int fmcId) {
  Xuint32 value;

  pIIC->fpGpoRead(pIIC, &value);
  if (fmcId == FMC_ID_SLOT1) {
    value = value | 0x00000001; // Force bit 0 to 1
  } else if (fmcId == FMC_ID_SLOT2) {
    value = value | 0x00000002; // Force bit 1 to 1
  } else {
    value = value | 0x00000003; // Force bits 1:0 to 1
  }
  pIIC->fpGpoWrite(pIIC, value);

  return 1;
}

int fmc_ipmi_disable(fmc_iic_t *pIIC, int fmcId) {
  Xuint32 value;

  pIIC->fpGpoRead(pIIC, &value);
  if (fmcId == FMC_ID_SLOT1) {
    value = value & 0xFFFFFFFE; // Force bit 0 to 0
  } else if (fmcId == FMC_ID_SLOT2) {
    value = value & 0xFFFFFFFD; // Force bit 1 to 0
  } else {
    value = value & 0xFFFFFFFC; // Force bits 1:0 to 0
  }
  pIIC->fpGpoWrite(pIIC, value);

  return 1;
}

/*****************************************************************************/
/**
 * This macro reads the subsystem reset network state
 *
 * @param  pReset is a pointer to the Reset IP Block
 * @param  channel is number of reset channel to work upon
 *
 * @return Reset state
 *           -1: Normal
 *           -0: Reset
 *
 ******************************************************************************/
static __inline u32 XVprocSs_GetResetState(XGpio *pReset,
                                           u32 channel) {
  return (XGpio_DiscreteRead(pReset, channel));
}

/*****************************************************************************/
/**
 * This macro enables the IP's connected to subsystem reset network
 *
 * @param  pReset is a pointer to the Reset IP Block
 * @param  channel is number of reset channel to work upon
 * @param  ipBlock is the reset line(s) to be activated
 *
 * @return None
 *
 * @note If reset block is not included in the subsystem instance
 *function does not do anything
 ******************************************************************************/
static __inline void
XVprocSs_EnableBlock(XGpio *pReset, u32 channel, u32 ipBlock) {
  u32 val;

  if (pReset) {
    val = XVprocSs_GetResetState(pReset, channel);
    val |= ipBlock;
    XGpio_DiscreteWrite(pReset, channel, val);
  }
}

/*****************************************************************************/
/**
 * This macro resets the IP connected to subsystem reset network
 *
 * @param  pReset is a pointer to the Reset IP Block
 * @param  channel is number of reset channel to work upon
 * @param  ipBlock is the reset line(s) to be asserted
 *
 * @return None
 *
 * @note If reset block is not included in the subsystem instance
 *function does not do anything
 ******************************************************************************/
static __inline void
XVprocSs_ResetBlock(XGpio *pReset, u32 channel, u32 ipBlock) {
  u32 val;

  if (pReset) {
    val = XVprocSs_GetResetState(pReset, channel);
    val &= ~ipBlock;
    XGpio_DiscreteWrite(pReset, channel, val);
  }
}

/*****************************************************************************/
/**
 * This function routes the delay routine used in the subsystem.
 *Preference is given to the user registered timer based routine. If
 *no delay handler is registered then it uses the platform specific
 *delay handler
 *
 * @param  XVprocSsPtr is a pointer to the subsystem instance
 * @param  msec is delay required
 *
 * @return None
 *
 ******************************************************************************/
static __inline void WaitUs(XVprocSs *XVprocSsPtr, u32 MicroSeconds) {
  if (MicroSeconds == 0)
    return;

  if (XVprocSsPtr->UsrDelayUs) {
    /* Use the time handler specified by the user for
     * better accuracy
     */
    XVprocSsPtr->UsrDelayUs(XVprocSsPtr->UsrTmrPtr, MicroSeconds);
  } else {
    /* use default BSP sleep API */
    usleep(MicroSeconds);
  }
}

/************************** Function Definition
 * ******************************/

/*****************************************************************************/
/**
 * This function queries the subsystem instance configuration to
 *determine the included sub-cores. For each sub-core that is present
 *in the design the sub-core driver instance is binded with the
 *subsystem sub-core driver handle
 *
 * @param  XVprocSsPtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return None
 *
 ******************************************************************************/
static void GetIncludedSubcores(XVprocSs *XVprocSsPtr) {
  XVprocSsPtr->HcrsmplrPtr =
      ((XVprocSsPtr->Config.HCrsmplr.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].Hcrsmplr)
           : NULL);
  XVprocSsPtr->VcrsmplrInPtr =
      ((XVprocSsPtr->Config.VCrsmplrIn.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].VcrsmplrIn)
           : NULL);
  XVprocSsPtr->VcrsmplrOutPtr =
      ((XVprocSsPtr->Config.VCrsmplrOut.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].VcrsmplrOut)
           : NULL);
  XVprocSsPtr->VscalerPtr =
      ((XVprocSsPtr->Config.Vscale.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].Vscaler)
           : NULL);
  XVprocSsPtr->HscalerPtr =
      ((XVprocSsPtr->Config.Hscale.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].Hscaler)
           : NULL);
  XVprocSsPtr->VdmaPtr =
      ((XVprocSsPtr->Config.Vdma.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].Vdma)
           : NULL);
  XVprocSsPtr->LboxPtr =
      ((XVprocSsPtr->Config.Lbox.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].Lbox)
           : NULL);
  XVprocSsPtr->CscPtr =
      ((XVprocSsPtr->Config.Csc.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].Csc)
           : NULL);
  XVprocSsPtr->DeintPtr =
      ((XVprocSsPtr->Config.Deint.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].Deint)
           : NULL);
  XVprocSsPtr->RouterPtr =
      ((XVprocSsPtr->Config.Router.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].Router)
           : NULL);
  XVprocSsPtr->RstAxisPtr =
      ((XVprocSsPtr->Config.RstAxis.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].RstAxis)
           : NULL);
  XVprocSsPtr->RstAximmPtr =
      ((XVprocSsPtr->Config.RstAximm.IsPresent)
           ? (&subcoreRepo[XVprocSsPtr->Config.DeviceId].RstAximm)
           : NULL);
}

/*****************************************************************************/
/**
 * This function initializes the video subsystem and included
 *sub-cores. This function must be called prior to using the
 *subsystem. Initialization includes setting up the instance data for
 *top level as well as all included sub-core therein, and ensuring the
 *hardware is in a known stable state.
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 * @param  CfgPtr points to the configuration structure associated
 *with the subsystem instance.
 * @param  EffectiveAddr is the base address of the device. If address
 *         translation is being used, then this parameter must reflect
 *the virtual base address. Otherwise, the physical address should be
 *         used.
 *
 * @return XST_SUCCESS if initialization is successful else
 *XST_FAILURE
 *
 ******************************************************************************/
int XVprocSs_CfgInitialize(XVprocSs *InstancePtr,
                           XVprocSs_Config *CfgPtr,
                           UINTPTR EffectiveAddr) {
  /* Verify arguments */
  Xil_AssertNonvoid(InstancePtr != NULL);
  Xil_AssertNonvoid(CfgPtr != NULL);
  Xil_AssertNonvoid(EffectiveAddr != (UINTPTR)NULL);

  /* Log the start of initialization */
  XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_INIT,
                    XVPROCSS_EDAT_BEGIN);

  /* Setup the instance */
  InstancePtr->Config = *CfgPtr;
  InstancePtr->Config.BaseAddress = EffectiveAddr;

  if (XVprocSs_GetSubsystemTopology(InstancePtr) >=
      XVPROCSS_TOPOLOGY_NUM_SUPPORTED) {
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CHK_TOPO,
                      XVPROCSS_EDAT_FAILURE);
    return (XST_FAILURE);
  }
  XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CHK_TOPO,
                    XVprocSs_GetSubsystemTopology(InstancePtr));

  /* Determine sub-cores included in the provided instance of
   * subsystem */
  GetIncludedSubcores(InstancePtr);

  /* Initialize all included sub_cores */
  if (InstancePtr->RstAxisPtr) {
    if (XVprocSs_SubcoreInitResetAxis(InstancePtr) != XST_SUCCESS) {
      return (XST_FAILURE);
    }
  }

  if (InstancePtr->RstAximmPtr) {
    if (XVprocSs_SubcoreInitResetAximm(InstancePtr) != XST_SUCCESS) {
      return (XST_FAILURE);
    }
  }

  if (InstancePtr->RouterPtr) {
    if (XVprocSs_SubcoreInitRouter(InstancePtr) != XST_SUCCESS) {
      return (XST_FAILURE);
    }
  }

  if (InstancePtr->CscPtr) {
    if (XVprocSs_SubcoreInitCsc(InstancePtr) != XST_SUCCESS) {
      return (XST_FAILURE);
    }
  }

  if (InstancePtr->HscalerPtr) {
    if (XVprocSs_SubcoreInitHScaler(InstancePtr) != XST_SUCCESS) {
      return (XST_FAILURE);
    }
  }

  if (InstancePtr->VscalerPtr) {
    if (XVprocSs_SubcoreInitVScaler(InstancePtr) != XST_SUCCESS) {
      return (XST_FAILURE);
    }
  }

  if (InstancePtr->HcrsmplrPtr) {
    if (XVprocSs_SubcoreInitHCrsmplr(InstancePtr) != XST_SUCCESS) {
      return (XST_FAILURE);
    }
  }

  if (InstancePtr->VcrsmplrInPtr) {
    if (XVprocSs_SubcoreInitVCrsmpleIn(InstancePtr) != XST_SUCCESS) {
      return (XST_FAILURE);
    }
  }

  if (InstancePtr->VcrsmplrOutPtr) {
    if (XVprocSs_SubcoreInitVCrsmpleOut(InstancePtr) != XST_SUCCESS) {
      return (XST_FAILURE);
    }
  }

  if (InstancePtr->LboxPtr) {
    if (XVprocSs_SubcoreInitLetterbox(InstancePtr) != XST_SUCCESS) {
      return (XST_FAILURE);
    }
  }

  if (InstancePtr->VdmaPtr) {
    if (XVprocSs_SubcoreInitVdma(InstancePtr) != XST_SUCCESS) {
      return (XST_FAILURE);
    }
    /* If VDMA is included, Buffer address must be set */
    if (InstancePtr->FrameBufBaseaddr == 0) {
      XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CHK_BASEADDR,
                        XVPROCSS_EDAT_FAILURE);
      return (XST_FAILURE);
    }
  }

  if (InstancePtr->DeintPtr) {
    u32 vdmaBufReq, bufsize;

    if (XVprocSs_SubcoreInitDeinterlacer(InstancePtr) !=
        XST_SUCCESS) {
      return (XST_FAILURE);
    }

    /* Set Deinterlacer buffer offset in allocated DDR Frame Buffer
     * memory */
    if (InstancePtr->VdmaPtr) {
      u32 Bpc; // bytes per component

      Bpc = (InstancePtr->Config.ColorDepth + 7) / 8;

      // compute buffer size based on subsystem configuration
      // For 1 4K2K buffer (YUV444 16-bit) size is ~48MB
      bufsize = InstancePtr->Config.MaxWidth *
                InstancePtr->Config.MaxHeight *
                InstancePtr->Config.NumVidComponents * Bpc;

      // VDMA requires 4 buffers for total size of ~190MB
      vdmaBufReq = InstancePtr->VdmaPtr->MaxNumFrames * bufsize;
    } else {
      vdmaBufReq = 0;
      bufsize = 0;
    }

    /* If MADI is included, Buffer address must be set */
    if ((InstancePtr->Config.HasMADI) &&
        (InstancePtr->FrameBufBaseaddr == 0)) {
      XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CHK_BASEADDR,
                        XVPROCSS_EDAT_FAILURE);
      return (XST_FAILURE);
    }

    /* Set Deint Buffer Address Offset
     *   - Located after vdma buffers, if included
     *   - 1 4k2k buffer added as a pad between vdma and deint
     */
    InstancePtr->CtxtData.DeintBufAddr =
        InstancePtr->FrameBufBaseaddr + vdmaBufReq + bufsize;
  }

  /* Reset the hardware */
  XVprocSs_Reset(InstancePtr);

  /* Set subsystem to power on default state */
  SetPowerOnDefaultState(InstancePtr);

  /* Set the flag to indicate the subsystem is ready */
  InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

  /* Log the end of initialization */
  XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_INIT,
                    XVPROCSS_EDAT_END);

  return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * This function configures the Video Processing subsystem internal
 *blocks to power on default configuration
 *
 * @param  XVprocSsPtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return None
 *
 ******************************************************************************/
static void SetPowerOnDefaultState(XVprocSs *XVprocSsPtr) {
  XVidC_VideoStream vidStrmIn;
  XVidC_VideoStream vidStrmOut;
  XVidC_VideoWindow win;
  u16 PixPrecisionIndex;
  XVidC_VideoTiming const *TimingPtr;

  memset(&vidStrmIn, 0, sizeof(XVidC_VideoStream));
  memset(&vidStrmOut, 0, sizeof(XVidC_VideoStream));

  /* Setup Default Output Stream configuration */
  vidStrmOut.VmId = XVIDC_VM_1920x1080_60_P;
  vidStrmOut.ColorFormatId = XVIDC_CSF_RGB;
  vidStrmOut.FrameRate = XVIDC_FR_60HZ;
  vidStrmOut.IsInterlaced = FALSE;
  vidStrmOut.ColorDepth =
      (XVidC_ColorDepth)XVprocSsPtr->Config.ColorDepth;
  vidStrmOut.PixPerClk =
      (XVidC_PixelsPerClock)XVprocSsPtr->Config.PixPerClock;

  TimingPtr = XVidC_GetTimingInfo(vidStrmOut.VmId);
  vidStrmOut.Timing = *TimingPtr;

  /* Setup Default Input Stream configuration */
  vidStrmIn.VmId = XVIDC_VM_1920x1080_60_P;
  vidStrmIn.ColorFormatId = XVIDC_CSF_RGB;
  vidStrmIn.FrameRate = XVIDC_FR_60HZ;
  vidStrmIn.IsInterlaced = FALSE;
  vidStrmIn.ColorDepth =
      (XVidC_ColorDepth)XVprocSsPtr->Config.ColorDepth;
  vidStrmIn.PixPerClk =
      (XVidC_PixelsPerClock)XVprocSsPtr->Config.PixPerClock;

  TimingPtr = XVidC_GetTimingInfo(vidStrmIn.VmId);
  vidStrmIn.Timing = *TimingPtr;

  /* Setup Video Processing subsystem input/output  configuration */
  XVprocSs_SetVidStreamIn(XVprocSsPtr, &vidStrmIn);
  XVprocSs_SetVidStreamOut(XVprocSsPtr, &vidStrmOut);

  /* compute data width supported by Vdma */
  XVprocSsPtr->CtxtData.PixelWidthInBits =
      XVprocSsPtr->Config.NumVidComponents *
      XVprocSsPtr->VidIn.ColorDepth;
  switch (XVprocSsPtr->Config.PixPerClock) {
  case XVIDC_PPC_1:
  case XVIDC_PPC_2:
    if (XVprocSsPtr->Config.ColorDepth == XVIDC_BPC_10) {
      /* Align the bit width to next byte boundary for this particular
       * case Num_Channel	Color Depth		PixelWidth
       * Align
       * ----------------------------------------------------
       *    2				10
       * 20			 24 3				10
       * 30			 32
       *
       *    HW will do the bit padding for 20->24 and 30->32
       */
      XVprocSsPtr->CtxtData.PixelWidthInBits =
          ((XVprocSsPtr->CtxtData.PixelWidthInBits + 7) / 8) * 8;
    }
    break;

  default:
    break;
  }

  /* Set default Pip/Zoom window increment step size */
  switch (XVprocSsPtr->Config.ColorDepth) {
  case XVIDC_BPC_8:
    PixPrecisionIndex = 0;
    break;
  case XVIDC_BPC_10:
    PixPrecisionIndex = 1;
    break;
  case XVIDC_BPC_12:
    PixPrecisionIndex = 2;
    break;
  case XVIDC_BPC_16:
    PixPrecisionIndex = 3;
    break;
  default:
    PixPrecisionIndex = 0;
    break;
  }

  XVprocSsPtr->CtxtData.PixelHStepSize =
      XVprocSs_PixelHStep[XVprocSsPtr->Config.PixPerClock >> 1]
                         [PixPrecisionIndex];

  if (XVprocSs_IsConfigModeMax(XVprocSsPtr)) {
    /* Set default PIP Background color */
    XVprocSsPtr->CtxtData.LboxBkgndColor = XLBOX_BKGND_BLACK;

    /* Set default Zoom Window */
    win.Width = 400;
    win.Height = 400;
    win.StartX = win.StartY = 0;

    XVprocSs_SetZoomPipWindow(XVprocSsPtr, XVPROCSS_ZOOM_WIN, &win);

    /* Set default PIP Window */
    XVprocSs_SetZoomPipWindow(XVprocSsPtr, XVPROCSS_PIP_WIN, &win);
  }

  /* Release reset before programming any IP Block */
  XVprocSs_EnableBlock(XVprocSsPtr->RstAxisPtr, GPIO_CH_RESET_SEL,
                       XVPROCSS_RSTMASK_ALL_BLOCKS);
}

/****************************************************************************/
/**
 * This function starts the video subsystem including all sub-cores
 *that are included in the processing pipeline for a given use-case.
 *Video pipe is started from back to front
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return None
 *
 * @note Cores are started only if the corresponding start flag in the
 *scratch pad memory is set. This allows to selectively start only
 *those cores included in the processing chain
 ******************************************************************************/
void XVprocSs_Start(XVprocSs *InstancePtr) {
  u8 *StartCorePtr;

  /* Verify arguments */
  Xil_AssertVoid(InstancePtr != NULL);

  StartCorePtr = &InstancePtr->CtxtData.StartCore[0];

  if (StartCorePtr[XVPROCSS_SUBCORE_CR_V_OUT])
    XV_VCrsmplStart(InstancePtr->VcrsmplrOutPtr);

  if (StartCorePtr[XVPROCSS_SUBCORE_CR_H])
    XV_HCrsmplStart(InstancePtr->HcrsmplrPtr);

  if (StartCorePtr[XVPROCSS_SUBCORE_CSC])
    XV_CscStart(InstancePtr->CscPtr);

  if (StartCorePtr[XVPROCSS_SUBCORE_LBOX])
    XV_LBoxStart(InstancePtr->LboxPtr);

  if (StartCorePtr[XVPROCSS_SUBCORE_SCALER_H])
    XV_HScalerStart(InstancePtr->HscalerPtr);

  if (StartCorePtr[XVPROCSS_SUBCORE_SCALER_V])
    XV_VScalerStart(InstancePtr->VscalerPtr);

  if (StartCorePtr[XVPROCSS_SUBCORE_VDMA])
    XVprocSs_VdmaStartTransfer(InstancePtr);

  if (StartCorePtr[XVPROCSS_SUBCORE_DEINT])
    XV_DeintStart(InstancePtr->DeintPtr);

  if (StartCorePtr[XVPROCSS_SUBCORE_CR_V_IN])
    XV_VCrsmplStart(InstancePtr->VcrsmplrInPtr);

  /* Subsystem ready to accept axis - Enable Video Input */
  XVprocSs_EnableBlock(InstancePtr->RstAxisPtr, GPIO_CH_RESET_SEL,
                       XVPROCSS_RSTMASK_VIDEO_IN);

  XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_START_VPSS,
                    XVPROCSS_EDAT_SUCCESS);
}

/*****************************************************************************/
/**
 * This function stops the video subsystem including all sub-cores
 * Stop the video pipe starting from front to back
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return None
 *
 ******************************************************************************/
void XVprocSs_Stop(XVprocSs *InstancePtr) {
  /* Verify arguments */
  Xil_AssertVoid(InstancePtr != NULL);

  if (InstancePtr->VcrsmplrInPtr)
    XV_VCrsmplStop(InstancePtr->VcrsmplrInPtr);

  if (InstancePtr->DeintPtr)
    XV_DeintStop(InstancePtr->DeintPtr);

  if (InstancePtr->VdmaPtr)
    XVprocSs_VdmaStop(InstancePtr);

  if (InstancePtr->VscalerPtr)
    XV_VScalerStop(InstancePtr->VscalerPtr);

  if (InstancePtr->HscalerPtr)
    XV_HScalerStop(InstancePtr->HscalerPtr);

  if (InstancePtr->LboxPtr)
    XV_LBoxStop(InstancePtr->LboxPtr);

  if (InstancePtr->CscPtr)
    XV_CscStop(InstancePtr->CscPtr);

  if (InstancePtr->HcrsmplrPtr)
    XV_HCrsmplStop(InstancePtr->HcrsmplrPtr);

  if (InstancePtr->VcrsmplrOutPtr)
    XV_VCrsmplStop(InstancePtr->VcrsmplrOutPtr);

  XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_STOP_VPSS,
                    XVPROCSS_EDAT_SUCCESS);
}

/*****************************************************************************/
/**
 * This function resets the video subsystem sub-cores. There are 2
 *reset networks within the subsystem
 *  - For cores that are on AXIS interface
 *  - For cores that are on AXI-MM interface
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return None
 *
 *   XVprocSs_Reset,_Start controls vpss resets as shown
 *    axis_int
 *----_______________________-------------------------------- axis_ext
 *----________________________________________________------- aximm
 *-----------_______----------------------------------------- | 100us|
 *100us| 1000us | 1000us |               |
 *            _Reset...............................|               |
 *                                                  Program VSPP IP|
 *                                                            _Start
 *
 ******************************************************************************/
void XVprocSs_Reset(XVprocSs *InstancePtr) {
  /* Verify arguments */
  Xil_AssertVoid(InstancePtr != NULL);

  /* Soft Reset */
  XVprocSs_VdmaReset(InstancePtr);

  /* Reset All IP Blocks on AXIS interface and wait before doing the
   * aximm reset*/
  XVprocSs_ResetBlock(InstancePtr->RstAxisPtr, GPIO_CH_RESET_SEL,
                      XVPROCSS_RSTMASK_ALL_BLOCKS);
  WaitUs(InstancePtr,
         100); /* hold reset line for 100us before resetting Aximm */

  /* Reset All IP Blocks on AXI-MM interface*/
  XVprocSs_ResetBlock(InstancePtr->RstAximmPtr, GPIO_CH_RESET_SEL,
                      XVPROCSS_RSTMASK_IP_AXIMM);

  WaitUs(InstancePtr, 100); /* hold reset line for 100us */
  /*
   * Make sure the video IP's are out of reset - IP's cannot be
   * programmed when held in reset. Will cause Axi-Lite bus to lock.
   * Release IP reset - but hold vid_in in reset
   */
  XVprocSs_EnableBlock(InstancePtr->RstAximmPtr, GPIO_CH_RESET_SEL,
                       XVPROCSS_RSTMASK_IP_AXIMM);
  WaitUs(InstancePtr, 1000); /* wait 1ms for AXI-MM to stabilize */
  XVprocSs_EnableBlock(InstancePtr->RstAxisPtr, GPIO_CH_RESET_SEL,
                       XVPROCSS_RSTMASK_IP_AXIS);
  WaitUs(InstancePtr, 1000); /* wait 1ms for AXIS to stabilize */

  /* Reset start core flags */
  memset(InstancePtr->CtxtData.StartCore, 0,
         sizeof(InstancePtr->CtxtData.StartCore));

  XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_RESET_VPSS,
                    XVPROCSS_EDAT_SUCCESS);
}

/*****************************************************************************/
/**
 * This function configures the video subsystem input interface
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 * @param  StrmIn is the pointer to input stream configuration
 *
 * @return XST_SUCCESS
 *
 ******************************************************************************/
int XVprocSs_SetVidStreamIn(XVprocSs *InstancePtr,
                            const XVidC_VideoStream *StrmIn) {
  /* Verify arguments */
  Xil_AssertNonvoid(InstancePtr != NULL);
  Xil_AssertNonvoid(StrmIn != NULL);
  Xil_AssertNonvoid((StrmIn->Timing.HActive > 0) &&
                    (StrmIn->Timing.VActive > 0));

  /* set stream properties */
  InstancePtr->VidIn = *StrmIn;

  return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * This function configures the video subsystem output interface
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 * @param  StrmOut is the pointer to input stream configuration
 *
 * @return XST_SUCCESS
 *
 ******************************************************************************/
int XVprocSs_SetVidStreamOut(XVprocSs *InstancePtr,
                             const XVidC_VideoStream *StrmOut) {
  /* Verify arguments */
  Xil_AssertNonvoid(InstancePtr != NULL);
  Xil_AssertNonvoid(StrmOut != NULL);
  Xil_AssertNonvoid((StrmOut->Timing.HActive > 0) &&
                    (StrmOut->Timing.VActive > 0));

  /* set stream properties */
  InstancePtr->VidOut = *StrmOut;

  return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function sets the required subsystem video stream to the user
provided
* stream and timing parameters
*
* @param  StreamPtr is a pointer to the subsystem video stream to be
configured
* @param  VmId is the Video Mode ID of the new resolution to be set
* @param  Timing is the timing parameters of the new resolution to be
set

* @return XST_SUCCESS if successful else XST_FAILURE
******************************************************************************/
int XVprocSs_SetStreamResolution(XVidC_VideoStream *StreamPtr,
                                 const XVidC_VideoMode VmId,
                                 XVidC_VideoTiming const *Timing) {
  /* Verify arguments */
  Xil_AssertNonvoid(StreamPtr != NULL);
  Xil_AssertNonvoid(Timing != NULL);
  Xil_AssertNonvoid((Timing->HActive > 0) && (Timing->VActive > 0));

  /* Video Mode could be from the list of supported modes or custom */
  if (VmId != XVIDC_VM_NOT_SUPPORTED) {
    /* update stream timing properties */
    StreamPtr->VmId = VmId;
    StreamPtr->Timing = *Timing;
    return (XST_SUCCESS);
  } else {
    return (XST_FAILURE);
  }
}

/*****************************************************************************/
/**
 * This function updates the Pip/Zoom window currently on screen
 *in-place. This implies the video is not blanked and the new
 *coordinates will update instantly as the function executes
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return None
 *
 * @note This function must be called only after the respective mode
 *(PIP/Zoom) has been enabled and user wants to move window to a new
 *location This function is not applicable in Subsystem Stream Mode
 *Configuration
 *
 ******************************************************************************/
void XVprocSs_UpdateZoomPipWindow(XVprocSs *InstancePtr) {
  /* Verify arguments */
  Xil_AssertVoid(InstancePtr != NULL);

  if (XVprocSs_IsConfigModeMax(InstancePtr)) {
    /* send Vdma update window to IP */
    if (XVprocSs_IsPipModeOn(InstancePtr)) {
      XVprocSs_VdmaSetWinToDnScaleMode(InstancePtr,
                                       XVPROCSS_VDMA_UPDATE_WR_CH);
    } else {
      XVprocSs_VdmaSetWinToUpScaleMode(InstancePtr,
                                       XVPROCSS_VDMA_UPDATE_RD_CH);
    }

    XVprocSs_VdmaStartTransfer(InstancePtr);

    /*
     * Final output of Video Processing subsystem goes via LBox IP
     * Program the output resolution window
     */
    if (XVprocSs_IsPipModeOn(InstancePtr)) {
      XV_LBoxSetActiveWin(InstancePtr->LboxPtr,
                          &InstancePtr->CtxtData.WrWindow,
                          InstancePtr->VidOut.Timing.HActive,
                          InstancePtr->VidOut.Timing.VActive);
    }
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_UPDATE_ZPWIN,
                      XVPROCSS_EDAT_SUCCESS);
  } else {
    // streaming Config - no PIP or ZOOM window
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_UPDATE_ZPWIN,
                      XVPROCSS_EDAT_FAILURE);
  }
}

/*****************************************************************************/
/**
 * This function allows user to set the Zoom or PIP window. Scratch
 *pad memory is updated with the new window information
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 * @param  mode is feature to be updated PIP or ZOOM
 * @param  win is structure that contains window coordinates and size
 *
 * @return None
 *
 * @note For Zoom mode RD client window is written in scratch pad
 *memory For Pip mode WR client window is written in scratch pad
 *memory This function is not applicable in Subsystem Stream Mode
 *Configuration
 *
 ******************************************************************************/
void XVprocSs_SetZoomPipWindow(XVprocSs *InstancePtr,
                               XVprocSs_Win mode,
                               XVidC_VideoWindow *win) {
  /* Verify arguments */
  Xil_AssertVoid(InstancePtr != NULL);
  Xil_AssertVoid(win != NULL);

  if (XVprocSs_IsConfigModeMax(InstancePtr)) {
    if (mode == XVPROCSS_ZOOM_WIN) {
      /* If DMA engine does not support unaligned transfers then
       * - align window StartX to next PixelHStepSize boundary
       * - align window size to 2*Pixels/Clock
       */
      if (!InstancePtr->VdmaPtr->ReadChannel.HasDRE) {
        u32 AlignStartX, AlignWidth;

        AlignStartX = InstancePtr->CtxtData.PixelHStepSize;
        AlignWidth = 2 * InstancePtr->Config.PixPerClock;

        win->StartX =
            ((win->StartX + AlignStartX - 1) / AlignStartX) *
            AlignStartX;
        win->Width =
            ((win->Width + AlignWidth - 1) / AlignWidth) * AlignWidth;
      }
      // VDMA RD Client
      InstancePtr->CtxtData.RdWindow.StartX = win->StartX;
      InstancePtr->CtxtData.RdWindow.StartY = win->StartY;
      InstancePtr->CtxtData.RdWindow.Width = win->Width;
      InstancePtr->CtxtData.RdWindow.Height = win->Height;

      XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_SET_ZOOMWIN,
                        XVPROCSS_EDAT_SUCCESS);
    } else { // PIP
      /* If DMA engine does not support unaligned transfers then
       * - align window StartX to next PixelHStepSize boundary
       * - align window size to 2*Pixels/Clock
       */
      if (!InstancePtr->VdmaPtr->WriteChannel.HasDRE) {
        u32 AlignStartX, AlignWidth;

        AlignStartX = InstancePtr->CtxtData.PixelHStepSize;
        AlignWidth = 2 * InstancePtr->Config.PixPerClock;

        win->StartX =
            ((win->StartX + AlignStartX - 1) / AlignStartX) *
            AlignStartX;
        win->Width =
            ((win->Width + AlignWidth - 1) / AlignWidth) * AlignWidth;
      }
      // VDMA WR Client
      InstancePtr->CtxtData.WrWindow.StartX = win->StartX;
      InstancePtr->CtxtData.WrWindow.StartY = win->StartY;
      InstancePtr->CtxtData.WrWindow.Width = win->Width;
      InstancePtr->CtxtData.WrWindow.Height = win->Height;

      XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_SET_PIPWIN,
                        XVPROCSS_EDAT_SUCCESS);
    }
  } else { // streaming Config - no PIP or ZOOM window
    if (mode == XVPROCSS_ZOOM_WIN)
      XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_SET_ZOOMWIN,
                        XVPROCSS_EDAT_FAILURE);
    else
      XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_SET_PIPWIN,
                        XVPROCSS_EDAT_FAILURE);
  }
}

/*****************************************************************************/
/**
 * This function reads the user defined Zoom/Pip window from scratch
 *pad memory
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 * @param  mode is feature (PIP or ZOOM) whose window coordinates are
 *to be retrieved
 * @param  win is structure that will contain read window coordinates
 *and size
 *
 ** @note For Zoom mode RD client window is read from scratch pad
 *memory For Pip mode WR client window is read from scratch pad memory
 *        This function is not applicable in Subsystem Stream Mode
 *Configuration
 *
 ******************************************************************************/
void XVprocSs_GetZoomPipWindow(XVprocSs *InstancePtr,
                               XVprocSs_Win mode,
                               XVidC_VideoWindow *win) {
  /* Verify arguments */
  Xil_AssertVoid(InstancePtr != NULL);
  Xil_AssertVoid(win != NULL);

  if (XVprocSs_IsConfigModeMax(InstancePtr)) {
    if (mode == XVPROCSS_ZOOM_WIN) {
      win->StartX = InstancePtr->CtxtData.RdWindow.StartX;
      win->StartY = InstancePtr->CtxtData.RdWindow.StartY;
      win->Width = InstancePtr->CtxtData.RdWindow.Width;
      win->Height = InstancePtr->CtxtData.RdWindow.Height;
    } else { // PIP
      win->StartX = InstancePtr->CtxtData.WrWindow.StartX;
      win->StartY = InstancePtr->CtxtData.WrWindow.StartY;
      win->Width = InstancePtr->CtxtData.WrWindow.Width;
      win->Height = InstancePtr->CtxtData.WrWindow.Height;
    }
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_GET_ZPWIN,
                      XVPROCSS_EDAT_SUCCESS);
  } else { // streaming Config - no PIP or ZOOM window
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_GET_ZPWIN,
                      XVPROCSS_EDAT_FAILURE);
  }
}

/*****************************************************************************/
/**
 * This function configures the video subsystem to enable/disable ZOOM
 *feature If ZOOM mode is set to ON but user has not set window
 *coordinates then quarter of input stream resolution at coordinates
 *0,0 is set as the default zoom window
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 * @param  OnOff is the action required
 *
 * @return None
 *
 * @note User must call XVprocSs_ConfigureSubsystem() for change to
 *take effect This call has not been added here such that it provides
 *an opportunity to make the change during vertical blanking at system
 *level. This behavior will change once shadow register support is
 *available in sub-core IP's This function is not applicable in
 *Subsystem Stream Mode Configuration
 *
 ******************************************************************************/
void XVprocSs_SetZoomMode(XVprocSs *InstancePtr, u8 OnOff) {
  /* Verify arguments */
  Xil_AssertVoid(InstancePtr != NULL);

  if (XVprocSs_IsConfigModeMax(InstancePtr)) {
    InstancePtr->CtxtData.ZoomEn = OnOff;
    InstancePtr->CtxtData.PipEn = FALSE;

    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_SET_ZOOMMODE,
                      InstancePtr->CtxtData.ZoomEn);
  } else { // streaming Config - no ZOOM window
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_SET_ZOOMMODE,
                      XVPROCSS_EDAT_FAILURE);
  }
}

/*****************************************************************************/
/**
 * This function configures the video subsystem to enable/disable PIP
 *feature If PIP mode is set to ON but user has not set window
 *coordinates then half of input stream resolution at coordinates 0,0
 *is set as the default zoom window
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 * @param  OnOff is the action required
 *
 * @return None
 *
 * @note User must call XVprocSs_ConfigureSubsystem() for change to
 *take effect This call has not been added here such that it provides
 *an opportunity to make the change during vertical blanking at system
 *level. This behavior will change once shadow register support is
 *available in sub-core IP's This function is not applicable in
 *Subsystem Stream Mode Configuration
 *
 ******************************************************************************/
void XVprocSs_SetPipMode(XVprocSs *InstancePtr, u8 OnOff) {
  /* Verify arguments */
  Xil_AssertVoid(InstancePtr != NULL);

  if (XVprocSs_IsConfigModeMax(InstancePtr)) {
    InstancePtr->CtxtData.PipEn = OnOff;
    InstancePtr->CtxtData.ZoomEn = FALSE;

    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_SET_PIPMODE,
                      InstancePtr->CtxtData.PipEn);
  } else { // streaming Config - no PIP window
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_SET_PIPMODE,
                      XVPROCSS_EDAT_FAILURE);
  }
}

/*****************************************************************************/
/**
 * This function validates the input and output stream configuration
 *for scaler only configuration
 *
 * @param  XVprocSsPtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 * @note This function is applicable only for Stream mode
 *configuration of the subsystem. In this mode only picture resizing
 *is available
 ******************************************************************************/
static int ValidateScalerOnlyConfig(XVprocSs *XVprocSsPtr) {
  XVidC_VideoStream *pStrmIn = &XVprocSsPtr->VidIn;
  XVidC_VideoStream *pStrmOut = &XVprocSsPtr->VidOut;

  if ((pStrmIn->ColorFormatId == XVIDC_CSF_YCRCB_420) &&
      !XV_VscalerIs420Enabled(XVprocSsPtr->VscalerPtr)) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_VSCALER,
                      XVPROCSS_EDAT_NO420);
    return (XST_FAILURE);
  }

  if ((pStrmIn->ColorFormatId == XVIDC_CSF_YCRCB_422) &&
      !XV_HscalerIs422Enabled(XVprocSsPtr->HscalerPtr)) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HSCALER,
                      XVPROCSS_EDAT_NO422);
    return (XST_FAILURE);
  }

  if (!XV_HScalerValidateConfig(XVprocSsPtr->HscalerPtr,
                                pStrmIn->ColorFormatId,
                                pStrmOut->ColorFormatId)) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HSCALER,
                      XVPROCSS_EDAT_FAILURE);
    return (XST_FAILURE);
  }

  XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HSCALER,
                    XVPROCSS_EDAT_VALID);
  return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * This function validates the input and output stream configuration
 *for csc only configuration
 *
 * @param  pStrmIn is a pointer to the input stream
 * @param  pStrmOut is a pointer to the output stream
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 * @note This function is applicable only for Stream mode
 *configuration of the subsystem. In this mode very limited
 *functionality is available
 ******************************************************************************/
static int ValidateCscOnlyConfig(XVprocSs *XVprocSsPtr,
                                 u16 Allow422,
                                 u16 Allow420) {
  XVidC_VideoStream *pStrmIn = &XVprocSsPtr->VidIn;
  XVidC_VideoStream *pStrmOut = &XVprocSsPtr->VidOut;

  // Valid color formats for the csc only case:
  //   1) if Vin or Vout are 422, and 422 or 420 are enabled, the case
  //   is allowed 2) if Vin or Vout are 420, and 420 is enabled, the
  //   case is allowed

  if (((pStrmIn->ColorFormatId == XVIDC_CSF_YCRCB_422) ||
       (pStrmOut->ColorFormatId == XVIDC_CSF_YCRCB_422)) &&
      (!Allow422)) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_CSC,
                      XVPROCSS_EDAT_NO422);
    return (XST_FAILURE);
  }

  if (((pStrmIn->ColorFormatId == XVIDC_CSF_YCRCB_420) ||
       (pStrmOut->ColorFormatId == XVIDC_CSF_YCRCB_420)) &&
      (!Allow420)) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_CSC,
                      XVPROCSS_EDAT_NO420);
    return (XST_FAILURE);
  }

  if (pStrmIn->VmId != pStrmOut->VmId) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_CSC,
                      XVPROCSS_EDAT_VMDIFF);
    return (XST_FAILURE);
  }

  if (pStrmIn->Timing.HActive != pStrmOut->Timing.HActive) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_CSC,
                      XVPROCSS_EDAT_HDIFF);
    return (XST_FAILURE);
  }

  if (pStrmIn->Timing.VActive != pStrmOut->Timing.VActive) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_CSC,
                      XVPROCSS_EDAT_VDIFF);
    return (XST_FAILURE);
  }

  XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_CSC,
                    XVPROCSS_EDAT_VALID);
  return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * This function validates the input and output stream configuration
 *for deint only configuration
 *
 * @param  pStrmIn is a pointer to the input stream
 * @param  pStrmOut is a pointer to the output stream
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 * @note This function is applicable only for Stream mode
 *configuration of the subsystem. In this mode very limited
 *functionality is available
 ******************************************************************************/
static int ValidateDeintOnlyConfig(XVprocSs *XVprocSsPtr) {
  XVidC_VideoStream *pStrmIn = &XVprocSsPtr->VidIn;
  XVidC_VideoStream *pStrmOut = &XVprocSsPtr->VidOut;

  if (pStrmOut->IsInterlaced) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_DEINT,
                      XVPROCSS_EDAT_INTPRG);
    return (XST_FAILURE);
  }

  if (pStrmIn->ColorFormatId != pStrmOut->ColorFormatId) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_DEINT,
                      XVPROCSS_EDAT_CDIFF);
    return (XST_FAILURE);
  }

  if (pStrmIn->Timing.HActive != pStrmOut->Timing.HActive) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_DEINT,
                      XVPROCSS_EDAT_HDIFF);
    return (XST_FAILURE);
  }

  XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_DEINT,
                    XVPROCSS_EDAT_VALID);
  return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * This function validates the in and out stream configuration for
 *VCResample only configuration. Converts 420->422 or 422->420.
 *
 * @param  pStrmIn is a pointer to the input stream
 * @param  pStrmOut is a pointer to the output stream
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 * @note This function is applicable only for Stream mode
 *configuration of the subsystem. In this mode very limited
 *functionality is available
 ******************************************************************************/
static int ValidateVCResampleOnlyConfig(XVprocSs *XVprocSsPtr) {
  XVidC_VideoStream *pStrmIn = &XVprocSsPtr->VidIn;
  XVidC_VideoStream *pStrmOut = &XVprocSsPtr->VidOut;

  if ((pStrmIn->ColorFormatId != XVIDC_CSF_YCRCB_420) &&
      (pStrmIn->ColorFormatId != XVIDC_CSF_YCRCB_422)) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_VCRI,
                      XVPROCSS_EDAT_CFIN);
    return (XST_FAILURE);
  }

  if ((pStrmOut->ColorFormatId != XVIDC_CSF_YCRCB_420) &&
      (pStrmOut->ColorFormatId != XVIDC_CSF_YCRCB_422)) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_VCRI,
                      XVPROCSS_EDAT_CFOUT);
    return (XST_FAILURE);
  }

  if (pStrmIn->VmId != pStrmOut->VmId) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_VCRI,
                      XVPROCSS_EDAT_VMDIFF);
    return (XST_FAILURE);
  }

  if (pStrmIn->Timing.HActive != pStrmOut->Timing.HActive) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_VCRI,
                      XVPROCSS_EDAT_HDIFF);
    return (XST_FAILURE);
  }

  if (pStrmIn->Timing.VActive != pStrmOut->Timing.VActive) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_VCRI,
                      XVPROCSS_EDAT_VDIFF);
    return (XST_FAILURE);
  }

  XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_VCRI,
                    XVPROCSS_EDAT_VALID);
  return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * This function validates the input and output stream configuration
 *for the HCResample only configuration
 *
 * @param  pStrmIn is a pointer to the input stream
 * @param  pStrmOut is a pointer to the output stream
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 * @note This function is applicable only for Stream mode
 *configuration of the subsystem. In this mode very limited
 *functionality is available
 ******************************************************************************/
static int ValidateHCResampleOnlyConfig(XVprocSs *XVprocSsPtr) {
  XVidC_VideoStream *pStrmIn = &XVprocSsPtr->VidIn;
  XVidC_VideoStream *pStrmOut = &XVprocSsPtr->VidOut;

  if ((pStrmIn->ColorFormatId != XVIDC_CSF_YCRCB_422) &&
      (pStrmIn->ColorFormatId != XVIDC_CSF_YCRCB_444)) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HCR,
                      XVPROCSS_EDAT_CFIN);
    return (XST_FAILURE);
  }

  if ((pStrmOut->ColorFormatId != XVIDC_CSF_YCRCB_422) &&
      (pStrmOut->ColorFormatId != XVIDC_CSF_YCRCB_444)) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HCR,
                      XVPROCSS_EDAT_CFOUT);
    return (XST_FAILURE);
  }

  if (pStrmIn->VmId != pStrmOut->VmId) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HCR,
                      XVPROCSS_EDAT_VMDIFF);
    return (XST_FAILURE);
  }

  if (pStrmIn->Timing.HActive != pStrmOut->Timing.HActive) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HCR,
                      XVPROCSS_EDAT_HDIFF);
    return (XST_FAILURE);
  }

  if (pStrmIn->Timing.VActive != pStrmOut->Timing.VActive) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HCR,
                      XVPROCSS_EDAT_VDIFF);
    return (XST_FAILURE);
  }

  XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HCR,
                    XVPROCSS_EDAT_VALID);
  return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * This function configures the video subsystem pipeline for
 *ScalerOnly topology of the subsystem
 *
 * @param  XVprocSsPtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 * @note If use case is possible the subsystem will configure the
 *sub-cores accordingly else will ignore the request
 *
 ******************************************************************************/
static int SetupModeScalerOnly(XVprocSs *XVprocSsPtr) {
  u32 vsc_WidthIn, vsc_HeightIn, vsc_HeightOut;
  u32 hsc_HeightIn, hsc_WidthIn, hsc_WidthOut, hsc_ColorFormatIn;
  int status = XST_SUCCESS;

  vsc_WidthIn = vsc_HeightIn = vsc_HeightOut = 0;
  hsc_HeightIn = hsc_WidthIn = hsc_WidthOut = 0;

  if (!XVprocSsPtr->VscalerPtr) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_VSCALER,
                      XVPROCSS_EDAT_IPABSENT);
    return (XST_FAILURE);
  }

  if (!XVprocSsPtr->HscalerPtr) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HSCALER,
                      XVPROCSS_EDAT_IPABSENT);
    return (XST_FAILURE);
  }

  /* check if input/output stream configuration is supported */
  status = ValidateScalerOnlyConfig(XVprocSsPtr);

  if (status == XST_SUCCESS) {
    /* Reset the IP Blocks inside the VPSS */
    XVprocSs_Reset(XVprocSsPtr);

    /* UpScale mode V Scaler is before H Scaler */
    vsc_WidthIn = XVprocSsPtr->VidIn.Timing.HActive;
    vsc_HeightIn = XVprocSsPtr->VidIn.Timing.VActive;
    vsc_HeightOut = XVprocSsPtr->VidOut.Timing.VActive;

    hsc_WidthIn = vsc_WidthIn;
    hsc_HeightIn = vsc_HeightOut;
    hsc_WidthOut = XVprocSsPtr->VidOut.Timing.HActive;
    if (XVprocSsPtr->VidIn.ColorFormatId == XVIDC_CSF_YCRCB_420) {
      hsc_ColorFormatIn = XVIDC_CSF_YCRCB_422;
    } else {
      hsc_ColorFormatIn = XVprocSsPtr->VidIn.ColorFormatId;
    }

    /* Configure scaler to scale input to output resolution */
    XV_VScalerSetup(XVprocSsPtr->VscalerPtr, vsc_WidthIn,
                    vsc_HeightIn, vsc_HeightOut,
                    XVprocSsPtr->VidIn.ColorFormatId);

    XV_HScalerSetup(XVprocSsPtr->HscalerPtr, hsc_HeightIn,
                    hsc_WidthIn, hsc_WidthOut, hsc_ColorFormatIn,
                    XVprocSsPtr->VidOut.ColorFormatId);

    /* Start Scaler sub-cores */
    XV_HScalerStart(XVprocSsPtr->HscalerPtr);
    XV_VScalerStart(XVprocSsPtr->VscalerPtr);

    /* Subsystem Ready to accept input stream - Enable Video Input */
    XVprocSs_EnableBlock(XVprocSsPtr->RstAxisPtr, GPIO_CH_RESET_SEL,
                         XVPROCSS_RSTMASK_VIDEO_IN);
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HSCALER,
                      XVPROCSS_EDAT_SETUPOK);
  } else {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HSCALER,
                      XVPROCSS_EDAT_IGNORE);
  }
  return (status);
}

/*****************************************************************************/
/**
 * This function configures the video subsystem pipeline for CscOnly
 * topology of the subsystem
 *
 * @param  XVprocSsPtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 * @note If use case is possible the subsystem will configure the
 *sub-cores accordingly else will ignore the request
 *
 ******************************************************************************/
static int SetupModeCscOnly(XVprocSs *XVprocSsPtr) {
  XVidC_ColorFormat CscIn, CscOut;
  XVidC_ColorStd StdIn, StdOut;
  XVidC_ColorRange RangeOut;
  XVidC_ColorDepth ColorDepth;
  u32 HeightOut = 0;
  u32 WidthOut = 0;
  u16 Allow422;
  u16 Allow420;
  int status = XST_SUCCESS;

  if (!XVprocSsPtr->CscPtr) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_CSC,
                      XVPROCSS_EDAT_IPABSENT);
    return (XST_FAILURE);
  }

  Allow422 = XV_CscIs422Enabled(XVprocSsPtr->CscPtr);
  Allow420 = XV_CscIs420Enabled(XVprocSsPtr->CscPtr);

  /* check if input/output stream configuration is supported */
  status = ValidateCscOnlyConfig(XVprocSsPtr, Allow422, Allow420);

  if (status == XST_SUCCESS) {
    /* In the single-IP cases the reset has been done outside this
     * routine */

    // when setting up a new resolution, start with default picture
    // settings
    XV_CscSetPowerOnDefaultState(XVprocSsPtr->CscPtr);

    // set the proper color depth: get it from the vprocss config
    ColorDepth =
        (XVidC_ColorDepth)XVprocSs_GetColorDepth(XVprocSsPtr);
    XV_CscSetColorDepth(XVprocSsPtr->CscPtr, ColorDepth);

    // all other picture settings are filled in by XV_CscSetColorspace
    CscIn = XVprocSsPtr->VidIn.ColorFormatId;
    CscOut = XVprocSsPtr->VidOut.ColorFormatId;
    StdIn = XVprocSsPtr->CscPtr->StandardIn;
    StdOut = XVprocSsPtr->CscPtr->StandardOut;
    RangeOut = XVprocSsPtr->CscPtr->OutputRange;
    XV_CscSetColorspace(XVprocSsPtr->CscPtr, CscIn, CscOut, StdIn,
                        StdOut, RangeOut);

    // set the Global Window size
    HeightOut = XVprocSsPtr->VidOut.Timing.VActive;
    WidthOut = XVprocSsPtr->VidOut.Timing.HActive;
    XV_CscSetActiveSize(XVprocSsPtr->CscPtr, WidthOut, HeightOut);

    /* Start Csc sub-core */
    XV_CscStart(XVprocSsPtr->CscPtr);
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_CSC,
                      XVPROCSS_EDAT_SETUPOK);
  } else {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_CSC,
                      XVPROCSS_EDAT_IGNORE);
  }
  return (status);
}

/*****************************************************************************/
/**
 * This function configures the video subsystem pipeline for DeintOnly
 * topology of the subsystem
 *
 * @param  XVprocSsPtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 * @note If use case is possible the subsystem will configure the
 *sub-cores accordingly else will ignore the request
 *
 ******************************************************************************/
static int SetupModeDeintOnly(XVprocSs *XVprocSsPtr) {
  XVprocSs_ContextData *CtxtPtr = &XVprocSsPtr->CtxtData;
  XVidC_VideoStream *pStrmIn = &XVprocSsPtr->VidIn;
  int status = XST_SUCCESS;

  if (!XVprocSsPtr->DeintPtr) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_DEINT,
                      XVPROCSS_EDAT_IPABSENT);
    return (XST_FAILURE);
  }

  /* check if input/output stream configuration is supported */
  status = ValidateDeintOnlyConfig(XVprocSsPtr);

  if (status == XST_SUCCESS) {
    /* In the single-IP cases the reset has been done outside this
     * routine */

    /* Save input resolution in the _ContextData structure */
    CtxtPtr->StrmCformat = XVprocSsPtr->VidIn.ColorFormatId;
    CtxtPtr->VidInWidth = XVprocSsPtr->VidIn.Timing.HActive;
    // we know after validating this config that VidIn is interlaced
    // and VidOut is progressive: multiply height by 2 for downstream
    CtxtPtr->VidInHeight = XVprocSsPtr->VidIn.Timing.VActive * 2;

    XV_DeintSetFieldBuffers(XVprocSsPtr->DeintPtr,
                            CtxtPtr->DeintBufAddr,
                            XVprocSsPtr->VidIn.ColorFormatId);

    XV_deinterlacer_Set_width(&XVprocSsPtr->DeintPtr->Deint,
                              CtxtPtr->VidInWidth);

    // VidIn.Timing.VActive is the field height
    XV_deinterlacer_Set_height(&XVprocSsPtr->DeintPtr->Deint,
                               XVprocSsPtr->VidIn.Timing.VActive);

    // TBD (the deint field ID bit is fixed to zero)
    XV_deinterlacer_Set_invert_field_id(&XVprocSsPtr->DeintPtr->Deint,
                                        0);

    if (!pStrmIn->IsInterlaced)
      XV_deinterlacer_Set_algo(&XVprocSsPtr->DeintPtr->Deint,
                               XV_DEINTERLACER_MEMORY_PASSTHROUGH);

    /* Start Deint sub-core */
    XV_DeintStart(XVprocSsPtr->DeintPtr);
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_DEINT,
                      XVPROCSS_EDAT_SETUPOK);
  } else {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_DEINT,
                      XVPROCSS_EDAT_IGNORE);
  }
  return (status);
}

/*****************************************************************************/
/**
 * This function configures the video subsystem pipeline for
 *VCResample only topology of the subsystem
 *
 * @param  XVprocSsPtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 * @note If use case is possible the subsystem will configure the
 *sub-cores accordingly else will ignore the request
 *
 ******************************************************************************/
static int SetupModeVCResampleOnly(XVprocSs *XVprocSsPtr) {
  XVprocSs_ContextData *CtxtPtr = &XVprocSsPtr->CtxtData;
  int status = XST_SUCCESS;

  if (!XVprocSsPtr->VcrsmplrInPtr) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_VCRI,
                      XVPROCSS_EDAT_IPABSENT);
    return (XST_FAILURE);
  }

  /* check if input/output stream configuration is supported */
  status = ValidateVCResampleOnlyConfig(XVprocSsPtr);

  if (status == XST_SUCCESS) {
    /* In the single-IP cases the reset has been done outside this
     * routine */

    CtxtPtr->VidInWidth = XVprocSsPtr->VidIn.Timing.HActive;
    CtxtPtr->VidInHeight = XVprocSsPtr->VidIn.Timing.VActive;

    /* Configure V chroma resampler in and out color space */
    XV_VCrsmplSetActiveSize(XVprocSsPtr->VcrsmplrInPtr,
                            CtxtPtr->VidInWidth,
                            CtxtPtr->VidInHeight);

    XV_VCrsmplSetFormat(XVprocSsPtr->VcrsmplrInPtr,
                        XVprocSsPtr->VidIn.ColorFormatId,
                        XVprocSsPtr->VidOut.ColorFormatId);

    /* Start V Chroma Resampler-In sub-core */
    XV_VCrsmplStart(XVprocSsPtr->VcrsmplrInPtr);
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_VCRI,
                      XVPROCSS_EDAT_SETUPOK);
  } else {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_VCRI,
                      XVPROCSS_EDAT_IGNORE);
  }

  return (status);
}

/*****************************************************************************/
/**
 * This function configures the video subsystem pipeline for
 *HCResample only topology of the subsystem
 *
 * @param  XVprocSsPtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 * @note If use case is possible the subsystem will configure the
 *sub-cores accordingly else will ignore the request
 *
 ******************************************************************************/
static int SetupModeHCResampleOnly(XVprocSs *XVprocSsPtr) {
  XVidC_ColorFormat HcrIn, HcrOut;
  u32 HeightOut = 0;
  u32 WidthOut = 0;
  int status = XST_SUCCESS;

  if (!XVprocSsPtr->HcrsmplrPtr) {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HCR,
                      XVPROCSS_EDAT_IPABSENT);
    return (XST_FAILURE);
  }

  /* check if input/output stream configuration is supported */
  status = ValidateHCResampleOnlyConfig(XVprocSsPtr);

  if (status == XST_SUCCESS) {
    /* In the single-IP cases the reset has been done outside this
     * routine */

    HcrIn = XVprocSsPtr->VidIn.ColorFormatId;
    HcrOut = XVprocSsPtr->VidOut.ColorFormatId;
    HeightOut = XVprocSsPtr->VidOut.Timing.VActive;
    WidthOut = XVprocSsPtr->VidOut.Timing.HActive;

    /* Configure H chroma resampler in and out color space */
    XV_HCrsmplSetFormat(XVprocSsPtr->HcrsmplrPtr, HcrIn, HcrOut);

    XV_HCrsmplSetActiveSize(XVprocSsPtr->HcrsmplrPtr, WidthOut,
                            HeightOut);

    /* Start chroma resampler sub-core */
    XV_HCrsmplStart(XVprocSsPtr->HcrsmplrPtr);
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HCR,
                      XVPROCSS_EDAT_SETUPOK);
  } else {
    XVprocSs_LogWrite(XVprocSsPtr, XVPROCSS_EVT_CFG_HCR,
                      XVPROCSS_EDAT_IGNORE);
  }

  return (status);
}

/*****************************************************************************/
/**
 * This function configures the video subsystem pipeline for Maximum
 * (Full_Fledged) topology
 *
 * @param  XVprocSsPtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 ******************************************************************************/
static int SetupModeMax(XVprocSs *XVprocSsPtr) {
  int status;

  /* Build Routing table for the Video Data Flow */
  status = XVprocSs_BuildRoutingTable(XVprocSsPtr);

  if (status == XST_SUCCESS) {
    /* Reset the IP Blocks inside the VPSS */
    XVprocSs_Reset(XVprocSsPtr);

    /* Set the Video Data Router registers */
    XVprocSs_ProgRouterMux(XVprocSsPtr);

    /* program the Video IP subcores according to the use case */
    XVprocSs_SetupRouterDataFlow(XVprocSsPtr);
  }
  return (status);
}

/*****************************************************************************/
/**
 * This function validates the input and output stream configuration
 *against the Subsystem hardware capabilities
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 ******************************************************************************/
static int ValidateSubsystemConfig(XVprocSs *InstancePtr) {
  XVidC_VideoStream *StrmIn = &InstancePtr->VidIn;
  XVidC_VideoStream *StrmOut = &InstancePtr->VidOut;

  /* Runtime Color Depth conversion not supported
   * Always overwrite input/output stream color depth with subsystem
   * setting
   */
  StrmIn->ColorDepth =
      (XVidC_ColorDepth)InstancePtr->Config.ColorDepth;
  StrmOut->ColorDepth =
      (XVidC_ColorDepth)InstancePtr->Config.ColorDepth;

  /* Runtime Pixel/Clock conversion not supported
   * Always overwrite input/output stream pixel/clk with subsystem
   * setting
   */
  StrmIn->PixPerClk =
      (XVidC_PixelsPerClock)InstancePtr->Config.PixPerClock;
  StrmOut->PixPerClk =
      (XVidC_PixelsPerClock)InstancePtr->Config.PixPerClock;

  /* Frame rate conversion is possible only in FULL topology */
  if ((XVprocSs_GetSubsystemTopology(InstancePtr) !=
       XVPROCSS_TOPOLOGY_FULL_FLEDGED) &&
      (StrmIn->FrameRate != StrmOut->FrameRate)) {
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                      XVPROCSS_EDAT_VPSS_FRDIFF);
    return (XST_FAILURE);
  }

  /* Check input resolution is supported by HW */
  if ((StrmIn->Timing.HActive > InstancePtr->Config.MaxWidth) ||
      (StrmIn->Timing.HActive == 0) ||
      (StrmIn->Timing.VActive > InstancePtr->Config.MaxHeight) ||
      (StrmIn->Timing.VActive == 0)) {
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                      XVPROCSS_EDAT_VPSS_IVRANGE);
    return (XST_FAILURE);
  }

  /* Check output resolution is supported by HW */
  if ((StrmOut->Timing.HActive > InstancePtr->Config.MaxWidth) ||
      (StrmOut->Timing.HActive == 0) ||
      (StrmOut->Timing.VActive > InstancePtr->Config.MaxHeight) ||
      (StrmOut->Timing.VActive == 0)) {
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                      XVPROCSS_EDAT_VPSS_OVRANGE);
    return (XST_FAILURE);
  }

  /* Check Stream Width is aligned at Samples/Clock boundary */
  if (((StrmIn->Timing.HActive % InstancePtr->Config.PixPerClock) !=
       0) ||
      ((StrmOut->Timing.HActive % InstancePtr->Config.PixPerClock) !=
       0)) {
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                      XVPROCSS_EDAT_VPSS_WIDBAD);
    return (XST_FAILURE);
  }

  /* Check for HCResamp required, but not present */
  if (XVprocSs_IsConfigModeMax(InstancePtr) &&
      ((StrmIn->ColorFormatId == XVIDC_CSF_YCRCB_420) ||
       (StrmIn->ColorFormatId == XVIDC_CSF_YCRCB_422)) &&
      ((StrmOut->ColorFormatId == XVIDC_CSF_YCRCB_444) ||
       (StrmOut->ColorFormatId == XVIDC_CSF_RGB)) &&
      (InstancePtr->HcrsmplrPtr == NULL)) {
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                      XVPROCSS_EDAT_VPSS_NOHCR);
    return (XST_FAILURE);
  }

  /* Check for YUV422 In/Out stream width is even */
  if (((StrmIn->ColorFormatId == XVIDC_CSF_YCRCB_422) &&
       ((StrmIn->Timing.HActive % 2) != 0)) ||
      ((StrmOut->ColorFormatId == XVIDC_CSF_YCRCB_422) &&
       ((StrmOut->Timing.HActive % 2) != 0))) {
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                      XVPROCSS_EDAT_VPSS_WIDODD);
    return (XST_FAILURE);
  }

  /* Check for YUV420 In stream width and height is even */
  if ((StrmIn->ColorFormatId == XVIDC_CSF_YCRCB_420) &&
      (((StrmIn->Timing.HActive % 2) != 0) &&
       ((StrmIn->Timing.VActive % 2) != 0))) {
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                      XVPROCSS_EDAT_VPSS_SIZODD);
    return (XST_FAILURE);
  }

  /* Check for VCResamp required, but not present */
  /* In the Full-fledged case, the Output V C Resampler is
   * "VcrsmplrOut" */
  /* In the VCResample-only case, the Output V C Resampler is
   * "VcrsmplrIn" */
  if (((StrmOut->ColorFormatId == XVIDC_CSF_YCRCB_420) &&
       XVprocSs_IsConfigModeMax(InstancePtr) &&
       !InstancePtr->VcrsmplrOutPtr)) {
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                      XVPROCSS_EDAT_VPSS_NOVCRO);
    return (XST_FAILURE);
  }
  if (((StrmIn->ColorFormatId == XVIDC_CSF_YCRCB_420) &&
       XVprocSs_IsConfigModeMax(InstancePtr) &&
       !InstancePtr->VcrsmplrInPtr)) {
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                      XVPROCSS_EDAT_VPSS_NOVCRI);
    return (XST_FAILURE);
  }

  /* Check for Interlaced input limitation */
  if (StrmIn->IsInterlaced) {
    if (StrmIn->ColorFormatId == XVIDC_CSF_YCRCB_420) {
      XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                        XVPROCSS_EDAT_NO420);
      return (XST_FAILURE);
    }
    if (!InstancePtr->DeintPtr) {
      XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                        XVPROCSS_EDAT_VPSS_NODEIN);
      return (XST_FAILURE);
    }
  }
  XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CFG_VPSS,
                    XVPROCSS_EDAT_VALID);
  return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * This function is the entry point into the video processing
 *subsystem driver processing path. It will examine the instantiated
 *subsystem configuration mode and the input and output stream
 *configuration. Based on the available information control flow is
 *determined and requisite sub-cores are configured to implement the
 *supported use case
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *worked on.
 *
 * @return XST_SUCCESS if successful else XST_FAILURE
 *
 ******************************************************************************/
int XVprocSs_SetSubsystemConfig(XVprocSs *InstancePtr) {
  int status = XST_SUCCESS;

  /* Verify arguments */
  Xil_AssertNonvoid(InstancePtr != NULL);

  /* validate subsystem configuration */
  if (ValidateSubsystemConfig(InstancePtr) != XST_SUCCESS) {
    return (XST_FAILURE);
  }

  switch (XVprocSs_GetSubsystemTopology(InstancePtr)) {
  case XVPROCSS_TOPOLOGY_FULL_FLEDGED:
    status = SetupModeMax(InstancePtr);
    break;

  case XVPROCSS_TOPOLOGY_SCALER_ONLY:
    // Only configuration supported is Scaler-only
    status = SetupModeScalerOnly(InstancePtr);
    break;

  case XVPROCSS_TOPOLOGY_CSC_ONLY:
    // Only configuration supported is CSC-only
    status = SetupModeCscOnly(InstancePtr);
    break;

  case XVPROCSS_TOPOLOGY_DEINTERLACE_ONLY:
    // Only configuration supported is Deint-only
    status = SetupModeDeintOnly(InstancePtr);
    break;

  case XVPROCSS_TOPOLOGY_VCRESAMPLE_ONLY:
    // Only configurations supported are 420 <-> 422
    status = SetupModeVCResampleOnly(InstancePtr);
    break;

  case XVPROCSS_TOPOLOGY_HCRESAMPLE_ONLY:
    // Only configurations supported are 422 <-> 444
    status = SetupModeHCResampleOnly(InstancePtr);
    break;

  default:
    XVprocSs_LogWrite(InstancePtr, XVPROCSS_EVT_CHK_TOPO,
                      XVPROCSS_EDAT_FAILURE);
    status = XST_FAILURE;
    break;
  }
  return (status);
}

XVprocSs proc_ss_RGB_YCrCb_444;
XVprocSs proc_ss_444_to_422;
XVprocSs_Config *Config_ptr;
XVprocSs_Config *Config_ptr_422;

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
  if (fmc_ipmi_detect(&(config->fmc_ipmi_iic), "FMC-IMAGEON",
                      FMC_ID_ALL)) {
    fmc_ipmi_enable(&(config->fmc_ipmi_iic), FMC_ID_SLOT1);
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
    xil_printf("fail init 4:4:4 to 4:2:2\n\r");
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
