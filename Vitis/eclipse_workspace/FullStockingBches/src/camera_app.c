#include "camera_app.h"
#include "fmc_iic.h"
#include "fmc_ipmi.h"
#include "platform.h"
#include "xaxis_switch.h"
#include "xil_cache.h"
#include "xv_hcresampler.h"
#include "xv_letterbox.h"
#include "xv_letterbox_l2.h"
#include "xv_vcresampler.h"
#include "xvidc.h"
#include "xvprocss_coreinit.h"
#include "xvprocss_router.h"
#include "xvprocss_vdma.h"

camera_config_t camera_config;

vres_timing_t vres_resolutions[8] = {
    {"VGA", 480, 10, 2, 33, 0, 640, 16, 96, 48,
     0}, // VIDEO_RESOLUTION_VGA
    {"NTSC", 480, 9, 6, 30, 1, 720, 16, 62, 60,
     1}, // VIDEO_RESOLUTION_NTSC
    {"SVGA", 600, 1, 4, 23, 1, 800, 40, 128, 88,
     1}, // VIDEO_RESOLUTION_SVGA
    {"XGA", 768, 3, 6, 29, 0, 1024, 24, 136, 160,
     0}, // VIDEO_RESOLUTION_XGA
    {"720P", 720, 5, 5, 20, 1, 1280, 110, 40, 220,
     1}, // VIDEO_RESOLUTION_720P
    {"SXGA", 1024, 1, 3, 26, 0, 1280, 48, 184, 200,
     0}, // VIDEO_RESOLUTION_SXGA
    {"1080P", 1080, 4, 5, 36, 1, 1920, 88, 44, 148,
     1}, // VIDEO_RESOLUTION_1080P
    {"UXGA", 1200, 1, 3, 46, 0, 1600, 64, 192, 304,
     0} // VIDEO_RESOLUTION_UXGA
};


int camera_main() {
    init_platform();

    // Initialize camera configuration
    camera_config_t camera_config;
    camera_config.uBaseAddr_IIC_FmcIpmi = 0x41610000;    // Device for reading
    camera_config.uBaseAddr_IIC_FmcImageon = 0x41600000; // Device Config
    camera_config.uBaseAddr_VITA_SPI = 0x43C30000;       // Config Device
    camera_config.uBaseAddr_VITA_CAM = 0x43C20000; // Device for receiving
    camera_config.uDeviceId_VTC_tpg = 0; // Video Timer Controller (VTC) ID
    camera_config.uDeviceId_VDMA_HdmiFrameBuffer = 0U; // VDMA ID
    camera_config.uBaseAddr_MEM_HdmiFrameBuffer = 0x10000000; // VDMA base address for Frame buffers
    camera_config.uNumFrames_HdmiFrameBuffer = 5U; // Number of VDMA Frame buffers

    // Enable FMC Imageon
    int Status;
    Xuint32 i;
    int vita_enabled_error = 0;
    int vita_enable_attempt = 1;
    Xuint32 value;
    XVprocSs proc_ss_RGB_YCrCb_444;
    XVprocSs proc_ss_444_to_422;
    XVprocSs_Config *Config_ptr;
    XVprocSs_Config *Config_ptr_422;
    XVtc_Signal Signal; /* VTC Signal configuration */
    XVtc_Signal *SignalCfgPtr = &Signal;
    XVtc_Polarity Polarity;       /* Polarity configuration */
    XVtc_HoriOffsets HoriOffsets; /* Horizontal offsets configuration */
    XVtc_SourceSelect SourceSelect; /* Source Selection configuration */
    vres_timing_t VideoTiming;
    XAxiVdma_Config *vdmaConfigPtr;
    XVtc_Config *VtcCfgPtr;

    int HFrontPorch;
    int HSyncWidth;
    int HBackPorch;
    int VFrontPorch;
    int VSyncWidth;
    int VBackPorch;
    int LineWidth;
    int FrameHeight;

    XVtc *pVtc = &(camera_config.vtc_tpg);

    Xuint32 storage_size =
        camera_config.uNumFrames_HdmiFrameBuffer * ((1920 * 1080) << 1);
    volatile Xuint32 *pStorageMem =
        (Xuint32 *)camera_config.uBaseAddr_MEM_HdmiFrameBuffer;

    Xuint8 iic_cdce913_ssc_on[3][2] = {
        {0x10, 0x6D}, // SSC = 011 (0.75%)
        {0x11, 0xB6}, //
        {0x12, 0xDB}  //
    };

    camera_config.bVerbose = 1;
    camera_config.vita_aec = 0;       // off
    camera_config.vita_again = 0;     // 1.0
    camera_config.vita_dgain = 128;   // 1.0
    camera_config.vita_exposure = 90; // 90% of frame period

    // Initialize FMC IPMI I2C Controller
    Status = fmc_iic_axi_init(&(camera_config.fmc_ipmi_iic),
                             "FMC-IPMI I2C Controller",
                             camera_config.uBaseAddr_IIC_FmcIpmi);
    if (!Status) {
        return 1;
    }

    // FMC Module Validation
    if (fmc_ipmi_detect(&(camera_config.fmc_ipmi_iic), "FMC-IMAGEON",
                        FMC_ID_ALL)) {
        fmc_ipmi_enable(&(camera_config.fmc_ipmi_iic), FMC_ID_SLOT1);
    } else {
        return 1;
    }

    // Initialize FMC Imageon I2C Controller
    Status = fmc_iic_axi_init(&(camera_config.fmc_imageon_iic),
                             "FMC-IMAGEON I2C Controller",
                             camera_config.uBaseAddr_IIC_FmcImageon);
    if (!Status) {
        return 1;
    }

    // Initialize and configure FMC Imageon
    fmc_imageon_init(&(camera_config.fmc_imageon), "FMC-IMAGEON",
                    &(camera_config.fmc_imageon_iic));
    fmc_imageon_vclk_init(&(camera_config.fmc_imageon));
    fmc_imageon_vclk_config(&(camera_config.fmc_imageon), 6);

    // Force reset high
    camera_config.fmc_ipmi_iic.fpGpoRead(&(camera_config.fmc_ipmi_iic), &value);
    value = value | 0x00000004; // Force bit 2 to 1
    camera_config.fmc_ipmi_iic.fpGpoWrite(&(camera_config.fmc_ipmi_iic), value);
    usleep(200000);

    // Force reset low
    camera_config.fmc_ipmi_iic.fpGpoRead(&(camera_config.fmc_ipmi_iic), &value);
    value = value & ~0x00000004; // Force bit 2 to 0
    camera_config.fmc_ipmi_iic.fpGpoWrite(&(camera_config.fmc_ipmi_iic), value);
    usleep(500000);

    // Configure HDMI output
    camera_config.hdmio_width = 1920;
    camera_config.hdmio_height = 1080;
    camera_config.hdmio_timing.IsHDMI = 0; // DVI Mode
    camera_config.hdmio_timing.IsEncrypted = 0;
    camera_config.hdmio_timing.IsInterlaced = 0;
    camera_config.hdmio_timing.ColorDepth = 8;
    camera_config.hdmio_timing.HActiveVideo = 1920;
    camera_config.hdmio_timing.HFrontPorch = 88;
    camera_config.hdmio_timing.HSyncWidth = 44;
    camera_config.hdmio_timing.HSyncPolarity = 1;
    camera_config.hdmio_timing.HBackPorch = 148;
    camera_config.hdmio_timing.VActiveVideo = 1080;
    camera_config.hdmio_timing.VFrontPorch = 4;
    camera_config.hdmio_timing.VSyncWidth = 5;
    camera_config.hdmio_timing.VSyncPolarity = 1;
    camera_config.hdmio_timing.VBackPorch = 36;

    // Detect resolution
    camera_config.hdmio_resolution = -1;
    for (i = 0; i < 8; i++) {
        if (camera_config.hdmio_width == vres_resolutions[i].HActiveVideo &&
            camera_config.hdmio_height == vres_resolutions[i].VActiveVideo) {
            camera_config.hdmio_resolution = i;
            break;
        }
    }

    // Initialize VTC
    VtcCfgPtr = XVtc_LookupConfig(camera_config.uDeviceId_VTC_tpg);
    if (VtcCfgPtr == NULL) {
        return 1;
    }
    Status = XVtc_CfgInitialize(&(camera_config.vtc_tpg), VtcCfgPtr,
                               VtcCfgPtr->BaseAddress);
    if (Status != 0L) {
        return 1;
    }
    XVtc_DisableSync(&(camera_config.vtc_tpg));
    sleep(1);
    XVtc_EnableGenerator(&(camera_config.vtc_tpg));

    // Configure VTC generator
    sleep(5);
    /* Set up Polarity of all outputs */
    memset((void *)&Polarity, 0, sizeof(Polarity));
    Polarity.ActiveChromaPol = 1;
    Polarity.ActiveVideoPol = 1;
    Polarity.FieldIdPol = 0;
    Polarity.VBlankPol = 1;
    Polarity.VSyncPol = 1;
    Polarity.HBlankPol = 1;
    Polarity.HSyncPol = 1;

    XVtc_SetPolarity(pVtc, &Polarity);

    memset((void *)&HoriOffsets, 0, sizeof(HoriOffsets));
    HoriOffsets.V0BlankHoriEnd = 1920;
    HoriOffsets.V0BlankHoriStart = 1920;
    HoriOffsets.V0SyncHoriEnd = 1920;
    HoriOffsets.V0SyncHoriStart = 1920;
    XVtc_SetGeneratorHoriOffset(pVtc, &HoriOffsets);

    // Get timing parameters from resolution
    VideoTiming.pName = vres_resolutions[camera_config.hdmio_resolution].pName;
    VideoTiming.HActiveVideo = vres_resolutions[camera_config.hdmio_resolution].HActiveVideo;
    VideoTiming.HFrontPorch = vres_resolutions[camera_config.hdmio_resolution].HFrontPorch;
    VideoTiming.HSyncWidth = vres_resolutions[camera_config.hdmio_resolution].HSyncWidth;
    VideoTiming.HBackPorch = vres_resolutions[camera_config.hdmio_resolution].HBackPorch;
    VideoTiming.HSyncPolarity = vres_resolutions[camera_config.hdmio_resolution].HSyncPolarity;
    VideoTiming.VActiveVideo = vres_resolutions[camera_config.hdmio_resolution].VActiveVideo;
    VideoTiming.VFrontPorch = vres_resolutions[camera_config.hdmio_resolution].VFrontPorch;
    VideoTiming.VSyncWidth = vres_resolutions[camera_config.hdmio_resolution].VSyncWidth;
    VideoTiming.VBackPorch = vres_resolutions[camera_config.hdmio_resolution].VBackPorch;
    VideoTiming.VSyncPolarity = vres_resolutions[camera_config.hdmio_resolution].VSyncPolarity;

    HFrontPorch = VideoTiming.HFrontPorch;
    HSyncWidth = VideoTiming.HSyncWidth;
    HBackPorch = VideoTiming.HBackPorch;
    VFrontPorch = VideoTiming.VFrontPorch;
    VSyncWidth = VideoTiming.VSyncWidth;
    VBackPorch = VideoTiming.VBackPorch;
    LineWidth = VideoTiming.HActiveVideo;
    FrameHeight = VideoTiming.VActiveVideo;

    memset((void *)SignalCfgPtr, 0, sizeof(XVtc_Signal));
    /* Populate the VTC Signal config structure. Ignore the Field 1 */
    SignalCfgPtr->HFrontPorchStart = LineWidth;
    SignalCfgPtr->HTotal =
        HFrontPorch + HSyncWidth + HBackPorch + LineWidth;
    SignalCfgPtr->HBackPorchStart =
        LineWidth + HFrontPorch + HSyncWidth;
    SignalCfgPtr->HSyncStart = LineWidth + HFrontPorch;
    SignalCfgPtr->HActiveStart = 0;
    SignalCfgPtr->V0FrontPorchStart = FrameHeight;
    SignalCfgPtr->V0Total =
        VFrontPorch + VSyncWidth + VBackPorch + FrameHeight;
    SignalCfgPtr->V0BackPorchStart =
        FrameHeight + VFrontPorch + VSyncWidth;
    SignalCfgPtr->V0SyncStart = FrameHeight + VFrontPorch;
    SignalCfgPtr->V0ChromaStart = 0;
    SignalCfgPtr->V0ActiveStart = 0;
    XVtc_SetGenerator(pVtc, &Signal);

    /* Set up source select */
    memset((void *)&SourceSelect, 0, sizeof(SourceSelect));
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

    // Initialize HDMI output
    Status = fmc_imageon_hdmio_init(&(camera_config.fmc_imageon), 1,
                                   &(camera_config.hdmio_timing), 0);
    if (!Status) {
        return 1;
    }

    // Initialize VITA camera sensor
    onsemi_vita_init(
        &(camera_config.onsemi_vita),
        "VITA-2000",
        camera_config.uBaseAddr_VITA_SPI,
        camera_config.uBaseAddr_VITA_CAM
    );
    camera_config.onsemi_vita.uManualTap = 25;
    onsemi_vita_spi_config(
        &(camera_config.onsemi_vita),
        (7) // AXI-Lite SPI Speed (HZ) / 10,000,000 Hz
    );

    // Enable SSC (Spread Spectrum Clocking)
    fmc_imageon_iic_mux(&(camera_config.fmc_imageon), 3);

    for (i = 0; i < 3; i++) {
        camera_config.fmc_imageon.pIIC->fpIicWrite(
            camera_config.fmc_imageon.pIIC, 0x65,
            (0x80 | iic_cdce913_ssc_on[i][0]),
            &(iic_cdce913_ssc_on[i][1]), 1);
    }

    // Initialize frame buffers with test patterns
    for (i = 0; i < storage_size / camera_config.uNumFrames_HdmiFrameBuffer;
         i += 4) {                 // Frame #1 - Red pixels
        *pStorageMem++ = 0xF0525A52; // Red
    }
    for (i = 0; i < storage_size / camera_config.uNumFrames_HdmiFrameBuffer;
         i += 4) {                 // Frame #2 - Green pixels
        *pStorageMem++ = 0x36912291; // Green
    }
    for (i = 0; i < storage_size / camera_config.uNumFrames_HdmiFrameBuffer;
         i += 4) {                 // Frame #3 - Blue pixels
        *pStorageMem++ = 0x6E29F029; // Blue
    }
    Xil_DCacheFlush(); // Flush Cache

    // Initialize VDMA
    vdmaConfigPtr =
        XAxiVdma_LookupConfig(camera_config.uDeviceId_VDMA_HdmiFrameBuffer);
    if (!vdmaConfigPtr) {
        return 1;
    }

    /* Initialize DMA engine */
    Status = XAxiVdma_CfgInitialize(&(camera_config.vdma_hdmi), vdmaConfigPtr,
                                   vdmaConfigPtr->BaseAddress);
    if (Status != 0L) {
        return 1;
    }

    // Initialize VDMA transmit channel
    XAxiVdma_DmaSetup vdmacfg_hdmi_read;
    Xuint32 video_width, video_height;
    Xuint32 storage_width, storage_height, storage_stride, storage_size_calc,
        storage_offset;

    // Get Video dimensions
    video_height = vres_resolutions[camera_config.hdmio_resolution].VActiveVideo;    // in lines
    video_width = vres_resolutions[camera_config.hdmio_resolution].HActiveVideo << 1; // in bytes

    // Get Storage dimensions
    storage_height = vres_resolutions[camera_config.hdmio_resolution].VActiveVideo;    // in lines
    storage_width = vres_resolutions[camera_config.hdmio_resolution].HActiveVideo << 1; // in bytes
    storage_stride = storage_width;
    storage_size_calc = storage_width * storage_height;
    storage_offset =
        ((storage_height - video_height) >> 1) * storage_width +
        ((storage_width - video_width) >> 1);

    vdmacfg_hdmi_read.VertSizeInput = video_height;
    vdmacfg_hdmi_read.HoriSizeInput = video_width;
    vdmacfg_hdmi_read.Stride = storage_stride;
    vdmacfg_hdmi_read.FrameDelay = 0; /* This example does not test frame delay */
    vdmacfg_hdmi_read.EnableCircularBuf = 1;
    vdmacfg_hdmi_read.EnableSync = 1;
    vdmacfg_hdmi_read.PointNum = 1;
    vdmacfg_hdmi_read.EnableFrameCounter = 0;  /* Endless transfers */
    vdmacfg_hdmi_read.FixedFrameStoreAddr = 0; /* We are not doing parking */

    Status = XAxiVdma_DmaConfig(&(camera_config.vdma_hdmi), 2, &vdmacfg_hdmi_read);
    if (Status != 0L) {
        return 1;
    }

    Xuint32 Addr = camera_config.uBaseAddr_MEM_HdmiFrameBuffer + storage_offset;
    for (i = 0; i < camera_config.uNumFrames_HdmiFrameBuffer; i++) {
        vdmacfg_hdmi_read.FrameStoreStartAddr[i] = Addr;
        Addr += storage_size_calc;
    }

    Status = XAxiVdma_DmaSetBufferAddr(&(camera_config.vdma_hdmi), 2,
                                      vdmacfg_hdmi_read.FrameStoreStartAddr);
    if (Status != 0L) {
        return 1;
    }

    /* Start the DMA engine to transfer */
    // MM2S Startup
    Status = XAxiVdma_DmaStart(&(camera_config.vdma_hdmi), 2);
    if (Status != 0L) {
        return 1;
    }

    u32 uBaseAddr = camera_config.vdma_hdmi.BaseAddr; // @suppress("Field cannot be resolved")
    u32 uDMACR = *((volatile int *)(uBaseAddr));
    uDMACR |= 0x00000080;
    *((volatile int *)(uBaseAddr)) = uDMACR;

    // Initialize VDMA receive channel
    XAxiVdma_DmaSetup vdmacfg_hdmi_write;

    vdmacfg_hdmi_write.VertSizeInput = video_height;
    vdmacfg_hdmi_write.HoriSizeInput = video_width;
    vdmacfg_hdmi_write.Stride = storage_stride;
    vdmacfg_hdmi_write.FrameDelay = 0; /* This example does not test frame delay */
    vdmacfg_hdmi_write.EnableCircularBuf = 1;
    vdmacfg_hdmi_write.EnableSync = 1;
    vdmacfg_hdmi_write.PointNum = 1;
    vdmacfg_hdmi_write.EnableFrameCounter = 0; /* Endless transfers */
    vdmacfg_hdmi_write.FixedFrameStoreAddr = 0; /* We are not doing parking */

    Status = XAxiVdma_DmaConfig(&(camera_config.vdma_hdmi), 1, &vdmacfg_hdmi_write);
    if (Status != 0L) {
        return 1;
    }

    Addr = camera_config.uBaseAddr_MEM_HdmiFrameBuffer + storage_offset;
    for (i = 0; i < camera_config.uNumFrames_HdmiFrameBuffer; i++) {
        vdmacfg_hdmi_write.FrameStoreStartAddr[i] = Addr;
        Addr += storage_size_calc;
    }

    Status = XAxiVdma_DmaSetBufferAddr(&(camera_config.vdma_hdmi), 1,
                                      vdmacfg_hdmi_write.FrameStoreStartAddr);
    if (Status != 0L) {
        return 1;
    }

    Status = XAxiVdma_DmaStart(&(camera_config.vdma_hdmi), 1);
    if (Status != 0L) {
        return 1;
    }
    XAxiVdma_FsyncSrcSelect(&(camera_config.vdma_hdmi), 2, 1);

    // Initialize VITA camera sensor
    do {
        int vita_width, vita_height, vita_rate, result;

        result =
            onsemi_vita_sensor_initialize(&(camera_config.onsemi_vita), 101, 0);
        if (result == 0) {
            vita_enabled_error = -1;
        }
        onsemi_vita_sensor_initialize(&(camera_config.onsemi_vita), 103, 0);
        sleep(1);
        result = onsemi_vita_sensor_1080P60(&(camera_config.onsemi_vita),
                                           camera_config.bVerbose);
        if (result == 0) {
            vita_enabled_error = -1;
        }
        sleep(1);
        onsemi_vita_get_status(&(camera_config.onsemi_vita),
                              &(camera_config.vita_status_t1), 0);
        sleep(1);
        onsemi_vita_get_status(&(camera_config.onsemi_vita),
                              &(camera_config.vita_status_t2), 0);
        vita_width = camera_config.vita_status_t1.cntImagePixels * 4;
        vita_height = camera_config.vita_status_t1.cntImageLines;
        vita_rate = camera_config.vita_status_t2.cntFrames -
                   camera_config.vita_status_t1.cntFrames;
        if ((vita_width != 1920) || (vita_height != 1080) ||
            (vita_rate == 0)) {
            vita_enabled_error = 1;
        } else {
            vita_enabled_error = 0;
        }

        if (vita_enable_attempt > 3) {
            return -1;
        }
        vita_enable_attempt++;
    } while (vita_enabled_error != 0);

    // Video Processing Subsystem (Only re-sampling) 4:4:4 to 4:2:2
    Config_ptr_422 = XVprocSs_LookupConfig(1);

    Status = XVprocSs_CfgInitialize(&proc_ss_444_to_422, Config_ptr_422,
                                   0x43C10000);
    if (Status != 0L) {
        return -1;
    }

    // Set Up HW REG Width for SS1
    Xil_Out16((0x43C10010), (u16)(1920)); // Active Pixels per Scanline
    // Set Up HW REG Height for SS1
    Xil_Out16((0x43C10018), (u16)(1080)); // Active Lines per Frame
    // Set HW REG Input Video Format for SS1
    Xil_Out8((0x43C10020), (u8)(0x01));
    // Set HW REG Output Video Format for SS1
    Xil_Out8((0x43C10028), (u8)(0x02));
    // Set Mode for SS1
    Xil_Out32((0x43C10000),
             (u32)(0x81) // Control 0x10000001 means start and freerun mode
    );

    XVprocSs_Start(&proc_ss_444_to_422);

    Config_ptr = XVprocSs_LookupConfig(0);

    Status = XVprocSs_CfgInitialize(&proc_ss_RGB_YCrCb_444, Config_ptr,
                                  0x43C00000);
    if (Status != 0L) {
        return -1;
    }

    Status = XV_CscSetColorspace(proc_ss_RGB_YCrCb_444.CscPtr,
                              0, //
                              1, //
                              1, //
                              1, //
                              2  //
    );
    if (Status != 0L) {
        return -1;
    }

    Status = XVprocSs_SetSubsystemConfig(&proc_ss_RGB_YCrCb_444);
    if (Status != 0L) {
        return -1;
    }
    Status = XV_CscSetColorspace(proc_ss_RGB_YCrCb_444.CscPtr,
                              0, //
                              1, //
                              1, //
                              1, //
                              2  //
    );
    if (Status != 0L) {
        return -1;
    }
    XVprocSs_Start(&proc_ss_RGB_YCrCb_444);

    // Demosaic Bayer Pattern to 24b RGB IP Setup
    Xil_Out32(0x43C40010, (u32)(1920)); // Number of Active Pixels per Scanline
    Xil_Out32((0x43C40018), (u32)(1080)); // Number of Active Lines per Frame
    Xil_Out32((0x43C40028), (u32)(0)); // Bayer sampling grid starting position
    Xil_Out32((0x43C40000), (u32)(0x81)); // Start and freerun mode

    // Park both READ and WRITE channels on frame 1.
    // Set Park Frame READ channel
    u32 PARK_READ = *((volatile u32 *)(camera_config.vdma_hdmi.BaseAddr + 0x00000028));
    PARK_READ = (PARK_READ & ~0x1F0) | ((u32)(1 & 0x1F) << 8);
    *((volatile u32 *)(camera_config.vdma_hdmi.BaseAddr + 0x00000028)) = PARK_READ;

    // Set Park Frame WRITE channel
    u32 PARK_WRITE = *((volatile u32 *)(camera_config.vdma_hdmi.BaseAddr + 0x00000028));
    PARK_WRITE = (PARK_WRITE & ~0x1F) | ((u32)(1 & 0x1F));
    *((volatile u32 *)(camera_config.vdma_hdmi.BaseAddr + 0x00000028)) = PARK_WRITE;

    // Enable park
    *((volatile u32 *)(camera_config.vdma_hdmi.BaseAddr + 0x00000030)) &= ~0x2;
    *((volatile u32 *)(camera_config.vdma_hdmi.BaseAddr)) &= ~0x2;

    return 0;
}


