/*****************************************************************************
 * video_detector.c - configuration functions for the VTC video timing
 * controller. Note that the "video detection" functions do both
 *detection and generation.
 *****************************************************************************/

#include "camera_app.h"

/*****************************************************************************/
/**
 *
 * vdet_init
 * - initializes the VTC detector
 *
 * @param	VtcDeviceID is the device ID of the Video Timing
 *Controller core. pVtc is a pointer to a VTC instance
 *
 * @return	0 if all tests pass, 1 otherwise.
 *
 * @note		None.
 *
 ******************************************************************************/
int vdet_init(XVtc *pVtc, u16 VtcDeviceID) {
	int Status;
	XVtc_Config *VtcCfgPtr;

	/* Look for the device configuration info for the Video Timing
	 * Controller.
	 */
	VtcCfgPtr = XVtc_LookupConfig(VtcDeviceID);
	if (VtcCfgPtr == NULL) {
		return 1;
	}

	/* Initialize the Video Timing Controller instance */

	Status = XVtc_CfgInitialize(pVtc, VtcCfgPtr, VtcCfgPtr->BaseAddress);
	if (Status != XST_SUCCESS) {
		return 1;
	}

	// Enable Synchronization of Generator with Detector
	//	XVtc_EnableSync(pVtc);
	XVtc_DisableSync(pVtc);

	/* Enable both generator and detector modules */
	XVtc_Enable(pVtc);

	return 0;
}

