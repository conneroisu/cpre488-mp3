#include "camera_app.h"
#include "fmc_iic.h"
#include "fmc_ipmi.h"
#include "platform.h"
#include "xaxis_switch.h"
#include "xil_cache.h"
#include "xil_types.h"
#include "xv_csc_l2.h"
#include "xv_hcresampler.h"
#include "xv_letterbox.h"
#include "xv_letterbox_l2.h"
#include "xv_vcresampler.h"
#include "xvidc.h"
#include "xvprocss_coreinit.h"
#include "xvprocss_router.h"
#include "xvprocss_vdma.h"

camera_config_t camera_config;

vres_timing_t vres_resolutions[8] = { { "VGA", 480, 10, 2, 33, 0, 640, 16, 96,
		48, 0 }, // VIDEO_RESOLUTION_VGA
		{ "NTSC", 480, 9, 6, 30, 1, 720, 16, 62, 60, 1 }, // VIDEO_RESOLUTION_NTSC
		{ "SVGA", 600, 1, 4, 23, 1, 800, 40, 128, 88, 1 }, // VIDEO_RESOLUTION_SVGA
		{ "XGA", 768, 3, 6, 29, 0, 1024, 24, 136, 160, 0 }, // VIDEO_RESOLUTION_XGA
		{ "720P", 720, 5, 5, 20, 1, 1280, 110, 40, 220, 1 }, // VIDEO_RESOLUTION_720P
		{ "SXGA", 1024, 1, 3, 26, 0, 1280, 48, 184, 200, 0 }, // VIDEO_RESOLUTION_SXGA
		{ "1080P", 1080, 4, 5, 36, 1, 1920, 88, 44, 148, 1 }, // VIDEO_RESOLUTION_1080P
		{ "UXGA", 1200, 1, 3, 46, 0, 1600, 64, 192, 304, 0 } // VIDEO_RESOLUTION_UXGA
};
static void SignalSetup(XVtc *pVtc, Xuint32 ResolutionId,
		XVtc_Signal *SignalCfgPtr) {
	vres_timing_t VideoTiming;
	int HFrontPorch;
	int HSyncWidth;
	int HBackPorch;
	int VFrontPorch;
	int VSyncWidth;
	int VBackPorch;
	int LineWidth;
	int FrameHeight;
	vres_get_timing(ResolutionId, &VideoTiming);
	HFrontPorch = VideoTiming.HFrontPorch;
	HSyncWidth = VideoTiming.HSyncWidth;
	HBackPorch = VideoTiming.HBackPorch;
	VFrontPorch = VideoTiming.VFrontPorch;
	VSyncWidth = VideoTiming.VSyncWidth;
	VBackPorch = VideoTiming.VBackPorch;
	LineWidth = VideoTiming.HActiveVideo;
	FrameHeight = VideoTiming.VActiveVideo;
	memset((void *) SignalCfgPtr, 0, sizeof(XVtc_Signal));
	SignalCfgPtr->HFrontPorchStart = LineWidth;
	SignalCfgPtr->HTotal = HFrontPorch + HSyncWidth + HBackPorch + LineWidth;
	SignalCfgPtr->HBackPorchStart = LineWidth + HFrontPorch + HSyncWidth;
	SignalCfgPtr->HSyncStart = LineWidth + HFrontPorch;
	SignalCfgPtr->HActiveStart = 0;

	SignalCfgPtr->V0FrontPorchStart = FrameHeight;
	SignalCfgPtr->V0Total = VFrontPorch + VSyncWidth + VBackPorch + FrameHeight;
	SignalCfgPtr->V0BackPorchStart = FrameHeight + VFrontPorch + VSyncWidth;
	SignalCfgPtr->V0SyncStart = FrameHeight + VFrontPorch;
	SignalCfgPtr->V0ChromaStart = 0;
	SignalCfgPtr->V0ActiveStart = 0;
	return;
}
int vgen_init(XVtc *pVtc, u16 VtcDeviceID) {
	int Status;
	XVtc_Config *VtcCfgPtr;
	VtcCfgPtr = XVtc_LookupConfig(VtcDeviceID);
	if (VtcCfgPtr == NULL) {
		return 1;
	}
	Status = XVtc_CfgInitialize(pVtc, VtcCfgPtr, VtcCfgPtr->BaseAddress);
	if (Status != XST_SUCCESS) {
		return 1;
	}
	XVtc_DisableSync(pVtc);
	sleep(1);
	XVtc_EnableGenerator(pVtc);
	return 0;
}

/*****************************************************************************/
/**
 *
 * vgen_config
 * - configures the generator to generate missing syncs
 *
 * @param	pVtc is a pointer to an initialized VTC instance
 *           ResolutionId identified a video resolution
 *           vVerbose = 0 no verbose, 1 minimal verbose, 2 most verbose
 *
 * @return	0 if all tests pass, 1 otherwise.
 *
 * @note		None.
 *
 ******************************************************************************/
int vgen_config(XVtc *pVtc, int ResolutionId, int bVerbose) {

	XVtc_Signal Signal; /* VTC Signal configuration */
	XVtc_Polarity Polarity; /* Polarity configuration */
	XVtc_HoriOffsets HoriOffsets; /* Horizontal offsets configuration */
	XVtc_SourceSelect SourceSelect; /* Source Selection configuration */
	sleep(5);
	memset((void *) &Polarity, 0, sizeof(Polarity));
	Polarity.ActiveChromaPol = 1;
	Polarity.ActiveVideoPol = 1;
	Polarity.FieldIdPol = 0;
	Polarity.VBlankPol = 1;
	Polarity.VSyncPol = 1;
	Polarity.HBlankPol = 1;
	Polarity.HSyncPol = 1;
	XVtc_SetPolarity(pVtc, &Polarity);
	memset((void *) &HoriOffsets, 0, sizeof(HoriOffsets));
	HoriOffsets.V0BlankHoriEnd = 1920;
	HoriOffsets.V0BlankHoriStart = 1920;
	HoriOffsets.V0SyncHoriEnd = 1920;
	HoriOffsets.V0SyncHoriStart = 1920;
	XVtc_SetGeneratorHoriOffset(pVtc, &HoriOffsets);
	SignalSetup(pVtc, ResolutionId, &Signal);
	XVtc_SetGenerator(pVtc, &Signal);
	memset((void *) &SourceSelect, 0, sizeof(SourceSelect));
	SourceSelect.VChromaSrc = 0;
	SourceSelect.VActiveSrc = 1;
	SourceSelect.VBackPorchSrc = 1;
	SourceSelect.VSyncSrc = 1;
	SourceSelect.VFrontPorchSrc = 1;
	SourceSelect.VTotalSrc = 1;
	SourceSelect.HActiveSrc = 1;
	SourceSelect.HBackPorchSrc = 1;
	SourceSelect.HSyncSrc = 1;
	SourceSelect.HFrontPorchSrc = 1;
	SourceSelect.HTotalSrc = 1;
	XVtc_SetSource(pVtc, &SourceSelect);
	return 0;
}

char *vres_get_name(Xuint32 resolutionId) {
	if (resolutionId < 8) {
		return vres_resolutions[resolutionId].pName;
	} else {
		return "{UNKNOWN}";
	}
}

Xuint32 vres_get_width(Xuint32 resolutionId) {
	return vres_resolutions[resolutionId].HActiveVideo; // horizontal active
}

Xuint32 vres_get_height(Xuint32 resolutionId) {
	return vres_resolutions[resolutionId].VActiveVideo; // vertical active
}

Xuint32 vres_get_timing(Xuint32 ResolutionId, vres_timing_t *pTiming) {
	pTiming->pName = vres_resolutions[ResolutionId].pName;
	pTiming->HActiveVideo = vres_resolutions[ResolutionId].HActiveVideo;
	pTiming->HFrontPorch = vres_resolutions[ResolutionId].HFrontPorch;
	pTiming->HSyncWidth = vres_resolutions[ResolutionId].HSyncWidth;
	pTiming->HBackPorch = vres_resolutions[ResolutionId].HBackPorch;
	pTiming->HSyncPolarity = vres_resolutions[ResolutionId].HSyncPolarity;
	pTiming->VActiveVideo = vres_resolutions[ResolutionId].VActiveVideo;
	pTiming->VFrontPorch = vres_resolutions[ResolutionId].VFrontPorch;
	pTiming->VSyncWidth = vres_resolutions[ResolutionId].VSyncWidth;
	pTiming->VBackPorch = vres_resolutions[ResolutionId].VBackPorch;
	pTiming->VSyncPolarity = vres_resolutions[ResolutionId].VSyncPolarity;
	return 0;
}

Xint32 vres_detect(Xuint32 width, Xuint32 height) {
	Xint32 i;
	Xint32 resolution = -1;
	for (i = 0; i < 8; i++) {
		if (width == vres_get_width(i) && height == vres_get_height(i)) {
			//xil_printf( "Detected Video Resolution = %s\r\n", vres_get_name(i) );
			resolution = i;
			break;
		}
	}
	return resolution;
}

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
	PARK = (PARK & mask) | ((u32) (frame & 0x1F) << shift_amt);

#undef PARK
}

// Initialize the camera configuration data structure
void camera_config_init(camera_config_t *config) {
	config->uBaseAddr_IIC_FmcIpmi = 0x41610000; // Device for reading
	config->uBaseAddr_IIC_FmcImageon = 0x41600000; // Device Config
	config->uBaseAddr_VITA_SPI = 0x43C30000; // Config Device
	config->uBaseAddr_VITA_CAM = 0x43C20000; // Device for receiving
	config->uDeviceId_VTC_tpg = 0; // Video Timer Controller (VTC) ID
	config->uDeviceId_VDMA_HdmiFrameBuffer = 0U; // VDMA ID
	config->uBaseAddr_MEM_HdmiFrameBuffer = 0x00000000U + 0x10000000; // VDMA base address for Frame buffers
	config->uNumFrames_HdmiFrameBuffer =
	XPAR_AXIVDMA_0_NUM_FSTORES; // NUmber of VDMA Frame buffers
	return;
}
void enable_ssc(camera_config_t *config) {

	int i;

	Xuint8 iic_cdce913_ssc_on[3][2] = { { 0x10, 0x6D }, // SSC = 011 (0.75%)
			{ 0x11, 0xB6 }, //
			{ 0x12, 0xDB }  //
	};

	xil_printf("Enabling spread-spectrum clocking (SSC)\n\r");
	xil_printf("\ttype=down-spread, amount=-0.75%%\n\r");
	fmc_imageon_iic_mux(&(config->fmc_imageon), 3);

	for (i = 0; i < 3; i++) {
		config->fmc_imageon.pIIC->fpIicWrite(config->fmc_imageon.pIIC, 0x65,
				(0x80 | iic_cdce913_ssc_on[i][0]), &(iic_cdce913_ssc_on[i][1]),
				1);
	}

	return;
}
int vfb_common_init(u16 uDeviceId, XAxiVdma *pAxiVdma) {
	int Status;
	XAxiVdma_Config *Config;

	Config = XAxiVdma_LookupConfig(uDeviceId);
	if (!Config) {
		xil_printf("No video DMA found for ID %d\n\r", uDeviceId);
		return 1;
	}

	/* Initialize DMA engine */
	Status = XAxiVdma_CfgInitialize(pAxiVdma, Config, Config->BaseAddress);
	if (Status != 0L) {
		xil_printf("Initialization failed %d\n\r", Status);
		return 1;
	}

	return 0;
}

int vfb_tx_setup(XAxiVdma *pAxiVdma, XAxiVdma_DmaSetup *pReadCfg,
		Xuint32 uVideoResolution, Xuint32 uStorageResolution, Xuint32 uMemAddr,
		Xuint32 uNumFrames) {
	int i;
	u32 Addr;
	int Status;

	Xuint32 video_width, video_height;
	Xuint32 storage_width, storage_height, storage_stride, storage_size,
			storage_offset;

	// Get Video dimensions
	video_height = vres_get_height(uVideoResolution);      // in lines
	video_width = vres_get_width(uVideoResolution) << 1; // in bytes

	// Get Storage dimensions
	storage_height = vres_get_height(uStorageResolution);      // in lines
	storage_width = vres_get_width(uStorageResolution) << 1; // in bytes
	storage_stride = storage_width;
	storage_size = storage_width * storage_height;
	storage_offset = ((storage_height - video_height) >> 1) * storage_width
			+ ((storage_width - video_width) >> 1);

	pReadCfg->VertSizeInput = video_height;
	pReadCfg->HoriSizeInput = video_width;
	pReadCfg->Stride = storage_stride;

	pReadCfg->FrameDelay = 0; /* This example does not test frame delay */

	pReadCfg->EnableCircularBuf = 1;
	pReadCfg->EnableSync = 1;

	pReadCfg->PointNum = 1;
	pReadCfg->EnableFrameCounter = 0; /* Endless transfers */

	pReadCfg->FixedFrameStoreAddr = 0; /* We are not doing parking */

	Status = XAxiVdma_DmaConfig(pAxiVdma, XAXIVDMA_READ, pReadCfg);
	if (Status != XST_SUCCESS) {
		xdbg_printf(XDBG_DEBUG_ERROR,
				"Read channel config failed %d\n\r", Status);

		return XST_FAILURE;
	}

	/* Initialize buffer addresses
	 *
	 * These addresses are physical addresses
	 */
	Addr = uMemAddr + storage_offset;
	for (i = 0; i < uNumFrames; i++) {
		pReadCfg->FrameStoreStartAddr[i] = Addr;

		Addr += storage_size;
	}

	/* Set the buffer addresses for transfer in the DMA engine
	 * The buffer addresses are physical addresses
	 */
	Status = XAxiVdma_DmaSetBufferAddr(pAxiVdma, XAXIVDMA_READ,
			pReadCfg->FrameStoreStartAddr);
	if (Status != XST_SUCCESS) {
		xdbg_printf(XDBG_DEBUG_ERROR,
				"Read channel set buffer address failed %d\n\r", Status);

		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

int vfb_tx_start(XAxiVdma *pAxiVdma) {
	int Status;

	// MM2S Startup
	Status = XAxiVdma_DmaStart(pAxiVdma, XAXIVDMA_READ);
	if (Status != XST_SUCCESS) {
		xil_printf("Start read transfer failed %d\n\r", Status);
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}
int vfb_tx_init(XAxiVdma *pAxiVdma, XAxiVdma_DmaSetup *pReadCfg,
		Xuint32 uVideoResolution, Xuint32 uStorageResolution, Xuint32 uMemAddr,
		Xuint32 uNumFrames) {
	int Status;
	u32 uBaseAddr;
	u32 uDMACR;

	/* Setup the read channel */
	Status = vfb_tx_setup(pAxiVdma, pReadCfg, uVideoResolution,
			uStorageResolution, uMemAddr, uNumFrames);
	if (Status != XST_SUCCESS) {
		xdbg_printf(XDBG_DEBUG_ERROR,
				"Read channel setup failed %d\n\r", Status);
		return 1;
	}
	/* Start the DMA engine to transfer */
	Status = vfb_tx_start(pAxiVdma);
	if (Status != 0) {
		return 1;
	}
#if 0
	// This function returns prematurely due to (!Channel->GenLock) evaluating to false
	XAxiVdma_GenLockSourceSelect(pAxiVdma, XAXIVDMA_INTERNAL_GENLOCK, XAXIVDMA_READ);
#else
	uBaseAddr = pAxiVdma->BaseAddr;
	//xil_printf( "\t MM2S_DMACR       = 0x%08X\n\r", *((volatile int *)(uBaseAddr+XAXIVDMA_TX_OFFSET+XAXIVDMA_CR_OFFSET )) );
	uDMACR = *((volatile int *) (uBaseAddr));
	uDMACR |= 0x00000080;
	*((volatile int *) (uBaseAddr)) = uDMACR;
	//xil_printf( "\t MM2S_DMACR       = 0x%08X\n\r", *((volatile int *)(uBaseAddr+XAXIVDMA_TX_OFFSET+XAXIVDMA_CR_OFFSET )) );
#endif
	return 0;
}

int fmc_imageon_enable(camera_config_t *config) {

	config->bVerbose = 1;
	config->vita_aec = 0;       // off
	config->vita_again = 0;     // 1.0
	config->vita_dgain = 128;   // 1.0
	config->vita_exposure = 90; // 90% of frame period
	int re;
	re = fmc_iic_axi_init(&(config->fmc_ipmi_iic), "FMC-IPMI I2C Controller",
			config->uBaseAddr_IIC_FmcIpmi);
	if (!re) {
		xil_printf("ERROR: Failed to open FMC-IIC driver,\n\r");
		return 1;
	}
	// FMC Module Validation
	if (fmc_ipmi_detect(&(config->fmc_ipmi_iic), "FMC-IMAGEON", FMC_ID_ALL)) {
		fmc_ipmi_enable(&(config->fmc_ipmi_iic), FMC_ID_SLOT1);
	} else {
		xil_printf("ERROR: Failed to validate FMC-IPMI I2C Controller.\n\r");
		return 1;
	}
	re = fmc_iic_axi_init(&(config->fmc_imageon_iic),
			"FMC-IMAGEON I2C Controller", config->uBaseAddr_IIC_FmcImageon);
	if (!re) {
		xil_printf("ERROR: Failed to open FMC-IIC driver\n\r");
		return 1;
	}
	fmc_imageon_init(&(config->fmc_imageon), "FMC-IMAGEON",
			&(config->fmc_imageon_iic));
	fmc_imageon_vclk_init(&(config->fmc_imageon));
	fmc_imageon_vclk_config(&(config->fmc_imageon), 6);
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
	config->hdmio_resolution = vres_detect(config->hdmio_width,
			config->hdmio_height);
	vgen_init(&(config->vtc_tpg), config->uDeviceId_VTC_tpg);
	vgen_config(&(config->vtc_tpg), config->hdmio_resolution, 1);

	re = fmc_imageon_hdmio_init(&(config->fmc_imageon), 1,
			&(config->hdmio_timing), 0);
	if (!re) {
		xil_printf("Failed to init FMC-IMAGEON HDMI Output Interface\n\r");
		return 1;
	}
	onsemi_vita_init( //
			&(config->onsemi_vita), //
			"VITA-2000", //
			config->uBaseAddr_VITA_SPI, //
			config->uBaseAddr_VITA_CAM //
			);
	config->onsemi_vita.uManualTap = 25;
	onsemi_vita_spi_config(&(config->onsemi_vita), (75000000 / 10000000) // AXI-Lite SPI Speed (HZ) / 10,000,000 Hz
			);
	enable_ssc(config);
	Xuint32 i;
	Xuint32 storage_size = config->uNumFrames_HdmiFrameBuffer
			* ((1920 * 1080) << 1);
	volatile Xuint32 *pStorageMem =
			(Xuint32 *) config->uBaseAddr_MEM_HdmiFrameBuffer;

	for (i = 0; i < storage_size / config->uNumFrames_HdmiFrameBuffer; i += 4) { // Frame #1 - Red pixels
		*pStorageMem++ = 0xF0525A52; // Red
	}
	for (i = 0; i < storage_size / config->uNumFrames_HdmiFrameBuffer; i += 4) { // Frame #2 - Green pixels
		*pStorageMem++ = 0x36912291; // Green
	}
	for (i = 0; i < storage_size / config->uNumFrames_HdmiFrameBuffer; i += 4) { // Frame #3 - Blue pixels
		*pStorageMem++ = 0x6E29F029; // Blue
	}
	Xil_DCacheFlush(); // Flush Cache
	vfb_common_init( //
			config->uDeviceId_VDMA_HdmiFrameBuffer, // uDeviceId
			&(config->vdma_hdmi)                    // pAxiVdma
			);
	vfb_tx_init(&(config->vdma_hdmi),                  // pAxiVdma
			&(config->vdmacfg_hdmi_read),          // pReadCfg
			config->hdmio_resolution,              // uVideoResolution
			config->hdmio_resolution,              // uStorageResolution
			config->uBaseAddr_MEM_HdmiFrameBuffer, // uMemAddr
			config->uNumFrames_HdmiFrameBuffer     // uNumFrames
			);
	sleep(5);
	   vfb_rx_init(
	       &(config->vdma_hdmi),                  // pAxiVdma
	       &(config->vdmacfg_hdmi_write),         // pWriteCfg
	       config->hdmio_resolution,              // uVideoResolution
	       config->hdmio_resolution,              // uStorageResolution
	       config->uBaseAddr_MEM_HdmiFrameBuffer, // uMemAddr
	       config->uNumFrames_HdmiFrameBuffer     // uNumFrames
	   );
	return 0;
}

// Picture data
// Main function. Initializes the devices and configures VDMA
int camera_main() {
	init_platform();

	camera_config_init(&camera_config);
	fmc_imageon_enable(&camera_config);

	// Park both READ and WRITE channels on frame 1.
	set_park_frame(&(camera_config.vdma_hdmi), 1, 1);
	set_park_frame(&(camera_config.vdma_hdmi), 1, 2);

	// Enable park.G
#define READ_CR *((volatile u32*)(camera_config.vdma_hdmi.BaseAddr + XAXIVDMA_RX_OFFSET + XAXIVDMA_CR_OFFSET))
#define WRITE_CR *((volatile u32*)(camera_config.vdma_hdmi.BaseAddr + XAXIVDMA_TX_OFFSET + XAXIVDMA_CR_OFFSET))

	READ_CR &= ~0x2;
	WRITE_CR &= ~0x2;

#undef READ_CR
#undef WRITE_CR
	return 0;
}
