/*****************************************************************************
 * Joseph Zambreno
 * Phillip Jones
 *
 * Department of Electrical and Computer Engineering
 * Iowa State University
 *****************************************************************************/

/*****************************************************************************
 * video_generator.c - configuration functions for the VTC video timing
 * controller. Configures a VTC to generate timing at the specified frequency.
 * Mostly redundant functionality with video_detector.c
 *
 *
 * NOTES:
 * 02/04/14 by JAZ::Design created.
 *****************************************************************************/

#include "camera_app.h"
#include <unistd.h>

#define Xil_AssertVoid(Expression)                \
{                                                  \
    if (Expression) {                              \
        Xil_AssertStatus = XIL_ASSERT_NONE;       \
    } else {                                       \
        Xil_Assert(__FILE__, __LINE__);            \
        Xil_AssertStatus = XIL_ASSERT_OCCURRED;   \
        return;                                    \
    }                                              \
}

/*****************************************************************************/
/**
*
* This function sets up the Video Timing Controller Signal configuration.
*
* @param	None.
*
* @return	None.
*
* @note		None.
*
****************************************************************************/
static void SignalSetup( XVtc *pVtc, Xuint32 ResolutionId, XVtc_Signal *SignalCfgPtr )
{
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
	HSyncWidth  = VideoTiming.HSyncWidth;
	HBackPorch  = VideoTiming.HBackPorch;
	VFrontPorch = VideoTiming.VFrontPorch;
	VSyncWidth  = VideoTiming.VSyncWidth;
	VBackPorch  = VideoTiming.VBackPorch;
	LineWidth   = VideoTiming.HActiveVideo;
	FrameHeight = VideoTiming.VActiveVideo;

	/* Clear the VTC Signal config structure */

	memset((void *)SignalCfgPtr, 0, sizeof(XVtc_Signal));

	/* Populate the VTC Signal config structure. Ignore the Field 1 */

//	SignalCfgPtr->HFrontPorchStart = 0;
//	SignalCfgPtr->HTotal = HFrontPorch + HSyncWidth + HBackPorch
//				+ LineWidth - 1;
//	SignalCfgPtr->HBackPorchStart = HFrontPorch + HSyncWidth;
//	SignalCfgPtr->HSyncStart = HFrontPorch;
//	SignalCfgPtr->HActiveStart = HFrontPorch + HSyncWidth + HBackPorch;
//
//	SignalCfgPtr->V0FrontPorchStart = 0;
//	SignalCfgPtr->V0Total = VFrontPorch + VSyncWidth + VBackPorch
//				+ FrameHeight - 1;
//	SignalCfgPtr->V0BackPorchStart = VFrontPorch + VSyncWidth;
//	SignalCfgPtr->V0SyncStart = VFrontPorch;
//	SignalCfgPtr->V0ChromaStart = VFrontPorch + VSyncWidth + VBackPorch;
//	SignalCfgPtr->V0ActiveStart = VFrontPorch + VSyncWidth + VBackPorch;

	SignalCfgPtr->HFrontPorchStart = LineWidth;
	SignalCfgPtr->HTotal = HFrontPorch + HSyncWidth + HBackPorch
				+ LineWidth;
	SignalCfgPtr->HBackPorchStart = LineWidth + HFrontPorch + HSyncWidth;
	SignalCfgPtr->HSyncStart = LineWidth + HFrontPorch;
	SignalCfgPtr->HActiveStart = 0;

	SignalCfgPtr->V0FrontPorchStart = FrameHeight;
	SignalCfgPtr->V0Total = VFrontPorch + VSyncWidth + VBackPorch
				+ FrameHeight;
	SignalCfgPtr->V0BackPorchStart = FrameHeight + VFrontPorch + VSyncWidth;
	SignalCfgPtr->V0SyncStart = FrameHeight + VFrontPorch;
	SignalCfgPtr->V0ChromaStart = 0;
	SignalCfgPtr->V0ActiveStart = 0;

	 return;
}


XVtc_Config *XVtc_LookupConfig(u16 DeviceId)
{
	extern XVtc_Config XVtc_ConfigTable[];
	XVtc_Config *CfgPtr = NULL;
	int i;

	/* Checking for device id for which instance it is matching */
	for (i = 0; i < XPAR_XVTC_NUM_INSTANCES; i++) {
		/* Assigning address of config table if both device ids
		 * are matched
		 */
		if (XVtc_ConfigTable[i].DeviceId == DeviceId) {
			CfgPtr = &XVtc_ConfigTable[i];
			break;
		}
	}

	return CfgPtr;
}


/*****************************************************************************/
/**
*
* vgen_init
* - initializes the VTC detector
*
* @param	VtcDeviceID is the device ID of the Video Timing Controller core.
*           pVtc is a pointer to a VTC instance

*
* @return	0 if all tests pass, 1 otherwise.
*
* @note		None.
*
******************************************************************************/
int vgen_init(XVtc *pVtc, u16 VtcDeviceID)
{
	int Status;
	XVtc_Config *VtcCfgPtr;

	Xuint32 Width;
	Xuint32 Height;
	int ResolutionId;

	/* Look for the device configuration info for the Video Timing
	 * Controller.
	 */
	VtcCfgPtr = XVtc_LookupConfig( VtcDeviceID );
	if (VtcCfgPtr == NULL) {
		return 1;
	}

	/* Initialize the Video Timing Controller instance */

	Status = XVtc_CfgInitialize(pVtc, VtcCfgPtr,
		VtcCfgPtr->BaseAddress);
	if (Status != XST_SUCCESS) {
		return 1;
	}

	XVtc_DisableSync(pVtc);

	sleep(1);

	/* Enable the generator module */

	// phjones update to 1 arg.  XVtc_Enable(pVtc, XVTC_EN_GENERATOR);
	XVtc_EnableGenerator(pVtc);


	//	XVtc_DisableSync(pVtc);

	return 0;
}



void XVtc_SetGenerator(XVtc *InstancePtr, XVtc_Signal *SignalCfgPtr)
{
	u32 RegValue;
	u32 r_htotal, r_vtotal, r_hactive, r_vactive;
	XVtc_Signal *SCPtr;
	XVtc_HoriOffsets horiOffsets;

	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);
	Xil_AssertVoid(SignalCfgPtr != NULL);

	SCPtr = SignalCfgPtr;
	if(SCPtr->OriginMode == 0)
	{
		r_htotal = SCPtr->HTotal+1;
		r_vtotal = SCPtr->V0Total+1;

		r_hactive = r_htotal - SCPtr->HActiveStart;
		r_vactive = r_vtotal - SCPtr->V0ActiveStart;

		RegValue = (r_htotal) & XVTC_SB_START_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
					XVTC_GHSIZE_OFFSET, RegValue);

		RegValue = (r_vtotal) & XVTC_VSIZE_F0_MASK;
		RegValue |= ((SCPtr->V1Total+1) << XVTC_VSIZE_F1_SHIFT) &
							XVTC_VSIZE_F1_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
					XVTC_GVSIZE_OFFSET, RegValue);


		RegValue = (r_hactive) & XVTC_ASIZE_HORI_MASK;
		RegValue |= ((r_vactive) << XVTC_ASIZE_VERT_SHIFT ) &
							XVTC_ASIZE_VERT_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
						XVTC_GASIZE_OFFSET, RegValue);
		/* For some resolutions, the FIELD1 vactive size is different
		 * from FIELD0, e.g. XVIDC_VM_720x486_60_I (SDI NTSC),
		 * As there is no vactive FIELD1 entry in the video common
		 * library, program it separately. For resolutions where
		 * vactive values are different, it should be taken care in
		 * corrosponding driver. Otherwise program same values in
		 * FIELD0 and FIELD1 registers */
		RegValue = ((r_vactive) << XVTC_ASIZE_VERT_SHIFT) &
				XVTC_ASIZE_VERT_MASK;

		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
						XVTC_GASIZE_F1_OFFSET, RegValue);

		/* Update the Generator Horizontal 1 Register */
		RegValue = (SCPtr->HSyncStart + r_hactive) &
						XVTC_SB_START_MASK;
		RegValue |= ((SCPtr->HBackPorchStart + r_hactive) <<
				XVTC_SB_END_SHIFT) & XVTC_SB_END_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
					XVTC_GHSYNC_OFFSET, RegValue);

		/* Update the Generator Vertical 1 Register (field 0) */
		RegValue = (SCPtr->V0SyncStart + r_vactive -1) &
						XVTC_SB_START_MASK;
		RegValue |= ((SCPtr->V0BackPorchStart + r_vactive -1) <<
				XVTC_SB_END_SHIFT) & XVTC_SB_END_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
					XVTC_GVSYNC_OFFSET, RegValue);

		/* Update the Generator Vertical Sync Register (field 1) */
		RegValue = (SCPtr->V1SyncStart + r_vactive -1) &
						XVTC_SB_START_MASK;
		RegValue |= ((SCPtr->V1BackPorchStart + r_vactive -1) <<
					XVTC_SB_END_SHIFT) & XVTC_SB_END_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
					XVTC_GVSYNC_F1_OFFSET, RegValue);

		/* Chroma Start */
		RegValue = XVtc_ReadReg(InstancePtr->Config.BaseAddress,
							XVTC_GFENC_OFFSET);
		RegValue &= ~XVTC_ENC_CPARITY_MASK;
		RegValue = (((SCPtr->V0ChromaStart - SCPtr->V0ActiveStart) <<
						XVTC_ENC_CPARITY_SHIFT) &
					XVTC_ENC_CPARITY_MASK) | RegValue;

		RegValue &= ~XVTC_ENC_PROG_MASK;
		RegValue |= (SCPtr->Interlaced << XVTC_ENC_PROG_SHIFT) &
				XVTC_ENC_PROG_MASK;

		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
						XVTC_GFENC_OFFSET, RegValue);

		/* Setup default Horizontal Offsets - can override later with
		 * XVtc_SetGeneratorHoriOffset()
		 */
		horiOffsets.V0BlankHoriStart = r_hactive;
		horiOffsets.V0BlankHoriEnd = r_hactive;
		horiOffsets.V0SyncHoriStart = SCPtr->HSyncStart + r_hactive;
		horiOffsets.V0SyncHoriEnd = SCPtr->HSyncStart + r_hactive;

		horiOffsets.V1BlankHoriStart = r_hactive;
		horiOffsets.V1BlankHoriEnd = r_hactive;
		horiOffsets.V1SyncHoriStart = SCPtr->HSyncStart + r_hactive;
		horiOffsets.V1SyncHoriEnd = SCPtr->HSyncStart + r_hactive;

	}
	else
	{
		/* Total in mode=1 is the line width */
		r_htotal = SCPtr->HTotal;
		/* Total in mode=1 is the frame height */
		r_vtotal = SCPtr->V0Total;
		r_hactive = SCPtr->HFrontPorchStart;
		r_vactive = SCPtr->V0FrontPorchStart;

		RegValue = (r_htotal) & XVTC_SB_START_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
					XVTC_GHSIZE_OFFSET, RegValue);

		RegValue = (r_vtotal) & XVTC_VSIZE_F0_MASK;
		RegValue |= ((SCPtr->V1Total) << XVTC_VSIZE_F1_SHIFT) &
							XVTC_VSIZE_F1_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
						XVTC_GVSIZE_OFFSET, RegValue);


		RegValue = (r_hactive) & XVTC_ASIZE_HORI_MASK;
		RegValue |= ((r_vactive) << XVTC_ASIZE_VERT_SHIFT) &
							XVTC_ASIZE_VERT_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
						XVTC_GASIZE_OFFSET, RegValue);
		/* For some resolutions, the FIELD1 vactive size is different
		 * from FIELD0, e.g. XVIDC_VM_720x486_60_I (SDI NTSC),
		 * As there is no vactive FIELD1 entry in the video common
		 * library, program it separately. For resolutions where
		 * vactive values are different, it should be taken care in
		 * corrosponding driver. Otherwise program same values in
		 * FIELD0 and FIELD1 registers */
		RegValue = ((r_vactive) << XVTC_ASIZE_VERT_SHIFT) &
				XVTC_ASIZE_VERT_MASK;

		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
						XVTC_GASIZE_F1_OFFSET, RegValue);

		/* Update the Generator Horizontal 1 Register */
		RegValue = (SCPtr->HSyncStart) & XVTC_SB_START_MASK;
		RegValue |= ((SCPtr->HBackPorchStart) << XVTC_SB_END_SHIFT) &
						XVTC_SB_END_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
					XVTC_GHSYNC_OFFSET, RegValue);


		/* Update the Generator Vertical Sync Register (field 0) */
		RegValue = (SCPtr->V0SyncStart) & XVTC_SB_START_MASK;
		RegValue |= ((SCPtr->V0BackPorchStart) << XVTC_SB_END_SHIFT) &
						XVTC_SB_END_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
						XVTC_GVSYNC_OFFSET, RegValue);

		/* Update the Generator Vertical Sync Register (field 1) */
		RegValue = (SCPtr->V1SyncStart) & XVTC_SB_START_MASK;
		RegValue |= ((SCPtr->V1BackPorchStart) << XVTC_SB_END_SHIFT) &
						XVTC_SB_END_MASK;
		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
					XVTC_GVSYNC_F1_OFFSET, RegValue);

		/* Chroma Start */
		  RegValue = XVtc_ReadReg(InstancePtr->Config.BaseAddress,
							XVTC_GFENC_OFFSET);
		RegValue &= ~XVTC_ENC_CPARITY_MASK;
		RegValue = (((SCPtr->V0ChromaStart - SCPtr->V0ActiveStart) <<
							XVTC_ENC_CPARITY_SHIFT)
					& XVTC_ENC_CPARITY_MASK) | RegValue;

		RegValue &= ~XVTC_ENC_PROG_MASK;
		RegValue |= (SCPtr->Interlaced << XVTC_ENC_PROG_SHIFT) &
						XVTC_ENC_PROG_MASK;

		XVtc_WriteReg(InstancePtr->Config.BaseAddress,
					XVTC_GFENC_OFFSET, RegValue);

		/* Setup default Horizontal Offsets - can override later with
		 * XVtc_SetGeneratorHoriOffset()
		 */
		horiOffsets.V0BlankHoriStart = r_hactive;
		horiOffsets.V0BlankHoriEnd = r_hactive;
		horiOffsets.V0SyncHoriStart = SCPtr->HSyncStart;
		horiOffsets.V0SyncHoriEnd = SCPtr->HSyncStart;
		horiOffsets.V1BlankHoriStart = r_hactive;
		horiOffsets.V1BlankHoriEnd = r_hactive;
		horiOffsets.V1SyncHoriStart = SCPtr->HSyncStart;
		horiOffsets.V1SyncHoriEnd = SCPtr->HSyncStart;

	}
	XVtc_SetGeneratorHoriOffset(InstancePtr, &horiOffsets);

}

void XVtc_SetSource(XVtc *InstancePtr, XVtc_SourceSelect *SourcePtr)
{
	u32 CtrlRegValue;

	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);
	Xil_AssertVoid(SourcePtr != NULL);

	/* Read Control register value back and clear all source selection bits
	 * first
	 */
	CtrlRegValue = XVtc_ReadReg(InstancePtr->Config.BaseAddress,
					(XVTC_CTL_OFFSET));
	CtrlRegValue &= ~XVTC_CTL_ALLSS_MASK;

	/* Change the register value according to the setting in the source
	 * selection configuration structure
	 */

	if (SourcePtr->FieldIdPolSrc)
		CtrlRegValue |= XVTC_CTL_FIPSS_MASK;

	if (SourcePtr->ActiveChromaPolSrc)
		CtrlRegValue |= XVTC_CTL_ACPSS_MASK;

	if (SourcePtr->ActiveVideoPolSrc)
		CtrlRegValue |= XVTC_CTL_AVPSS_MASK;

	if (SourcePtr->HSyncPolSrc)
		CtrlRegValue |= XVTC_CTL_HSPSS_MASK;

	if (SourcePtr->VSyncPolSrc)
		CtrlRegValue |= XVTC_CTL_VSPSS_MASK;

	if (SourcePtr->HBlankPolSrc)
		CtrlRegValue |= XVTC_CTL_HBPSS_MASK;

	if (SourcePtr->VBlankPolSrc)
		CtrlRegValue |= XVTC_CTL_VBPSS_MASK;


	if (SourcePtr->VChromaSrc)
		CtrlRegValue |= XVTC_CTL_VCSS_MASK;

	if (SourcePtr->VActiveSrc)
		CtrlRegValue |= XVTC_CTL_VASS_MASK;

	if (SourcePtr->VBackPorchSrc)
		CtrlRegValue |= XVTC_CTL_VBSS_MASK;

	if (SourcePtr->VSyncSrc)
		CtrlRegValue |= XVTC_CTL_VSSS_MASK;

	if (SourcePtr->VFrontPorchSrc)
		CtrlRegValue |= XVTC_CTL_VFSS_MASK;

	if (SourcePtr->VTotalSrc)
		CtrlRegValue |= XVTC_CTL_VTSS_MASK;

	if (SourcePtr->HBackPorchSrc)
		CtrlRegValue |= XVTC_CTL_HBSS_MASK;

	if (SourcePtr->HSyncSrc)
		CtrlRegValue |= XVTC_CTL_HSSS_MASK;

	if (SourcePtr->HFrontPorchSrc)
		CtrlRegValue |= XVTC_CTL_HFSS_MASK;

	if (SourcePtr->HTotalSrc)
		CtrlRegValue |= XVTC_CTL_HTSS_MASK;

	if (SourcePtr->InterlacedMode)
		CtrlRegValue |= XVTC_CTL_INTERLACE_MASK;

	XVtc_WriteReg(InstancePtr->Config.BaseAddress, (XVTC_CTL_OFFSET),
			CtrlRegValue);
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
int vgen_config(XVtc *pVtc, int ResolutionId, int bVerbose)
{
	int Status;

	XVtc_Signal Signal;		/* VTC Signal configuration */
	XVtc_Polarity Polarity;		/* Polarity configuration */
	XVtc_HoriOffsets HoriOffsets;  /* Horizontal offsets configuration */
	XVtc_SourceSelect SourceSelect;	/* Source Selection configuration */

	usleep(5);

    if ( bVerbose )
    {
		xil_printf( "\tVideo Resolution = %s\n\r", vres_get_name(ResolutionId) );
	}

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

	/* Set up Generator */

	memset((void *)&HoriOffsets, 0, sizeof(HoriOffsets));
	HoriOffsets.V0BlankHoriEnd = 1920;
	HoriOffsets.V0BlankHoriStart = 1920;
	HoriOffsets.V0SyncHoriEnd = 1920;
	HoriOffsets.V0SyncHoriStart = 1920;

	XVtc_SetGeneratorHoriOffset(pVtc, &HoriOffsets);

	SignalSetup(pVtc,ResolutionId, &Signal);

	if ( bVerbose == 2 )
	{
		xil_printf("\tVTC Generator Configuration\n\r" );
		xil_printf("\t\tHorizontal Timing:\n\r" );
		xil_printf("\t\t\tHFrontPorchStart %d\r\n", Signal.HFrontPorchStart);
		xil_printf("\t\t\tHSyncStart %d\r\n", Signal.HSyncStart);
		xil_printf("\t\t\tHBackPorchStart %d\r\n", Signal.HBackPorchStart);
		xil_printf("\t\t\tHActiveStart = %d\r\n", Signal.HActiveStart);
		xil_printf("\t\t\tHTotal = %d\r\n", Signal.HTotal);
		xil_printf("\t\tVertical Timing:\n\r" );
		xil_printf("\t\t\tV0FrontPorchStart %d\r\n", Signal.V0FrontPorchStart);
		xil_printf("\t\t\tV0SyncStart %d\r\n", Signal.V0SyncStart);
		xil_printf("\t\t\tV0BackPorchStart %d\r\n", Signal.V0BackPorchStart);
		xil_printf("\t\t\tV0ActiveStart %d\r\n", Signal.V0ActiveStart);
		xil_printf("\t\t\tV0Total %d\r\n", Signal.V0Total);
	}

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


	/* Return success */

	return 0;
}
