/******************************************************************************
Copyright 2010 Broadcom Corporation.  All rights reserved.

Unless you and Broadcom execute a separate written software license agreement
governing use of this software, this software is licensed to you under the
terms of the GNU General Public License version 2, available at
http://www.gnu.org/copyleft/gpl.html (the "GPL").

Notwithstanding the above, under no circumstances may you combine this software
in any way with any other Broadcom software provided under a license other than
the GPL, without Broadcom's express prior written consent.
******************************************************************************/

/**
*
*   @file   camdrv_totoro.c
*
*   @brief  This file is the lower level driver API of stv0987 ISP/sensor.
*
*/
/**
 * @addtogroup CamDrvGroup
 * @{
 */

  /****************************************************************************/
  /*                          Include block                                   */
  /****************************************************************************/
#include <stdarg.h>
#include <stddef.h>

#include <linux/version.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/sysctl.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#if 0
#include <mach/reg_camera.h>
#include <mach/reg_lcd.h>
#endif
#include <mach/reg_clkpwr.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>

#include <linux/broadcom/types.h>
#include <linux/broadcom/bcm_major.h>
#include <linux/broadcom/hw_cfg.h>
#include <linux/broadcom/hal_camera.h>
#include <linux/broadcom/lcd.h>
#include <linux/broadcom/bcm_sysctl.h>
#include <linux/broadcom/PowerManager.h>
#include <plat/dma.h>
#include <linux/dma-mapping.h>

#include "hal_cam_drv_ath.h"
//#include "camdrv_dev.h"
#include <linux/regulator/consumer.h>

#include <plat/csl/csl_cam.h>

#include <linux/videodev2.h> //BYKIM_CAMACQ
#include "camacq_api.h"
#include "camacq_isx005.h"//BYKIM_DTP
#include <linux/gpio.h>

#if 1 //CYK 
void  CAMDRV_GetCurrentLux(CamSensorSelect_t sensor,UInt8 * vp_currentLux);
void  CAMDRV_GetCurrentEIT(CamSensorSelect_t sensor,UInt8 * vp_currentEIT);
void CAMDRV_CheckJpegStatus(void);    //For ISX500 sensor
void  CAMDRV_Calibration(void);
//BOOL CAMDRV_set_default_calibration(void);
int CAMDRV_DecodeInterleaveData(unsigned char *pInterleaveData, 	// (IN) Pointer of Interleave Data
						 int interleaveDataSize, 			// (IN) Data Size of Interleave Data
						 int yuvWidth, 						// (IN) Width of YUV Thumbnail
						 int yuvHeight, 					// (IN) Height of YUV Thumbnail
						 unsigned char *pJpegData,			// (OUT) Pointer of Buffer for Receiving JPEG Data 
						 int *pJpegSize,  					// (OUT) Pointer of JPEG Data Size
						 unsigned char *pYuvData);			// (OUT) Pointer of Buffer for Receiving YUV Data 
void CAMDRV_GetISOSpeed( CamSensorSelect_t sensor );

void CAMDRV_GetShutterSpeed( CamSensorSelect_t sensor );
void CAMDRV_ResetMode(void);
#endif

/*****************************************************************************/
/* start of CAM configuration */
/*****************************************************************************/

#define CAMERA_IMAGE_INTERFACE  CSL_CAM_INTF_CPI
#define CAMERA_PHYS_INTF_PORT   CSL_CAM_PORT_AFE_1
#define CAM_COLOR_R1R0              0x01670100//swsw_dual
#define CAM_COLOR_G1G0              0x00B70056
#define CAM_COLOR_B1                0x000001C6

CAMDRV_RESOLUTION_T    sViewFinderResolution        = CAMDRV_RESOLUTION_640x240;
CAMDRV_IMAGE_TYPE_T    sViewFinderFormat            = CAMDRV_IMAGE_YUV422;
CAMDRV_RESOLUTION_T    sCaptureImageResolution        = CAMDRV_RESOLUTION_UXGA;
CAMDRV_IMAGE_TYPE_T    sCaptureImageFormat            = CAMDRV_IMAGE_JPEG;

static struct regulator *cam_regulator_i;//swsw_dual
static struct regulator *cam_regulator_c;
static struct regulator *cam_regulator_a;

#define SENSOR_CLOCK_SPEED              CamDrv_24MHz


#define CAM_3M_RST          63
#define CAM_VGA_RST         42
#define CAM_IO_VGA_3M_EN    23
#define CAM_VGA_D_EN        52
#define MEGA_STBY              55
#define VGA_STBY            53

extern struct stCamacqSensorManager_t* GetCamacqSensorManager(); //BYKIM_CAMACQ


/*---------Sensor Power On */

static CamSensorIntfCntrl_st_t CamPowerOnSeq[] = {

	{GPIO_CNTRL, VGA_STBY,   GPIO_SetHigh},
	{PAUSE, 1, Nop_Cmd},
	{MCLK_CNTRL, SENSOR_CLOCK_SPEED,    CLK_TurnOn},
	{PAUSE, 5, Nop_Cmd},
	
	{GPIO_CNTRL, CAM_VGA_RST,   GPIO_SetHigh},
	{PAUSE, 5, Nop_Cmd},
	
      {GPIO_CNTRL, VGA_STBY,   GPIO_SetLow},
	{PAUSE, 1, Nop_Cmd},

	{GPIO_CNTRL, MEGA_STBY,   GPIO_SetHigh},
	{PAUSE, 5, Nop_Cmd},

    {GPIO_CNTRL, CAM_3M_RST,   GPIO_SetHigh},
         {PAUSE, 1, Nop_Cmd}


};

/*---------Sensor Power Off*/
static CamSensorIntfCntrl_st_t CamPowerOffSeq[] = {



{GPIO_CNTRL, CAM_3M_RST,   GPIO_SetLow},
{PAUSE, 3, Nop_Cmd},
{MCLK_CNTRL, CamDrv_NO_CLK,	CLK_TurnOff},
{PAUSE, 5, Nop_Cmd},
{GPIO_CNTRL, MEGA_STBY,	 GPIO_SetLow},
{GPIO_CNTRL, CAM_VGA_RST, GPIO_SetLow},
 {PAUSE, 1, Nop_Cmd}

		
};

//---------Sensor Flash Enable
static CamSensorIntfCntrl_st_t  CamFlashEnable[] = 
{
// -------Enable Flash
    {GPIO_CNTRL, 0xFF, GPIO_SetHigh},
    {PAUSE, 10, Nop_Cmd}
};

//---------Sensor Flash Disable
static CamSensorIntfCntrl_st_t  CamFlashDisable[] = 
{
// -------Disable Flash
    {GPIO_CNTRL, 0xFF, GPIO_SetLow},
    {PAUSE, 10, Nop_Cmd}
};

static CAM_Sensor_Supported_Params_t CamPrimaryDefault_st =
{
	/*****************************************
	   In still image  mode sensor capabilities are below 
	   *****************************************/
	4, // Number of still capture mode the sensor can support 
	{
		{2048,1536},{1600,1200},{1280,960},{320,240}
	},		//What kind of resolution sensor can support for still image capture
	1, //Number of output format for still image capture
	{CamDataFmtJPEG}, //output format for still imag eapture
	

	/*********************************************
	  If still image JPEG supported , what thumbnail sensor can give 
	  **********************************************/
	1, // number of Thumbnail resolutions supported .
	{
		{320 ,240} //Thumbnail/preview resolutions
	},

	1,  //Number of Thumbnail formats supported 
	{CamDataFmtYCbCr},  //Thumbnail formats 
	

	/**********************************************
	   In Video or preview mode sensor capabilities are below 
	 *************************************************/
	1,   //Number of video or preview mode the sensor can support
	{
		{320,240}  //What kind of resolution sensor can support for video resolution
	}, 
	1, //Number of output format for preview/video mode
	{CamDataFmtYCbCr},  //output format for preview/video mode
	

	/*****************************************
	  zoom support by sensor 
	  *****************************************/
	24, //Number of zoom setps sensor can support 
	{CamZoom_1_0,CamZoom_1_125,CamZoom_1_25,CamZoom_1_375,CamZoom_1_5,CamZoom_1_625,CamZoom_1_75,CamZoom_1_875,CamZoom_2_0,CamZoom_1_035,CamZoom_1_07,CamZoom_1_105,CamZoom_1_14,CamZoom_1_175,CamZoom_1_21,CamZoom_1_245,CamZoom_1_28,CamZoom_1_075,CamZoom_1_225,CamZoom_1_3,CamZoom_1_45,CamZoom_1_525,CamZoom_1_15,CamZoom_1_6}, // zoom steps
	
	"SAMSUNG",
	"GT-B5512" //target name
};

/** Primary Sensor Configuration and Capabilities  */
static HAL_CAM_ConfigCaps_st_t CamPrimaryCfgCap_st = 
{
    // CAMDRV_DATA_MODE_S *video_mode
    {
        320,                           // unsigned short        max_width;                //Maximum width resolution
        240,                           // unsigned short        max_height;                //Maximum height resolution
        0,                             // UInt32                data_size;                //Minimum amount of data sent by the camera
        10,                            // UInt32                framerate_lo_absolute;  //Minimum possible framerate u24.8 format
        20,                            // UInt32                framerate_hi_absolute;  //Maximum possible framerate u24.8 format
        CAMDRV_TRANSFORM_NONE,         // CAMDRV_TRANSFORM_T    transform;            //Possible transformations in this mode / user requested transformations
        CAMDRV_IMAGE_YUV422,           // CAMDRV_IMAGE_TYPE_T    format;                //Image format of the frame.
        CAMDRV_IMAGE_YUV422_YCbYCr,    // CAMDRV_IMAGE_ORDER_T    image_order;        //Format pixel order in the frame.
	//CAMDRV_IMAGE_YUV422_CbYCrY,
	//CAMDRV_IMAGE_YUV422_CrYCbY,
	//CAMDRV_IMAGE_YUV422_YCrYCb,
	CAMDRV_DATA_SIZE_16BIT,        // CAMDRV_DATA_SIZE_T    image_data_size;    //Packing mode of the data.
        CAMDRV_DECODE_NONE,            // CAMDRV_DECODE_T        periph_dec;         //The decoding that the VideoCore transciever (eg CCP2) should perform on the data after reception.
        CAMDRV_ENCODE_NONE,            // CAMDRV_ENCODE_T        periph_enc;            //The encoding that the camera IF transciever (eg CCP2) should perform on the data before writing to memory.
        0,                             // int                    block_length;        //Block length for DPCM encoded data - specified by caller
        CAMDRV_DATA_SIZE_NONE,         // CAMDRV_DATA_SIZE_T    embedded_data_size; //The embedded data size from the frame.
        CAMDRV_MODE_VIDEO,             // CAMDRV_CAPTURE_MODE_T    flags;            //A bitfield of flags that can be set on the mode.
        15,                            // UInt32                framerate;            //Framerate achievable in this mode / user requested framerate u24.8 format
        0,                             // UInt8                mechanical_shutter;    //It is possible to use mechanical shutter in this mode (set by CDI as it depends on lens driver) / user requests this feature */  
        2                             // UInt32                pre_frame;            //Frames to throw out for ViewFinder/Video capture 
    },

    // CAMDRV_DATA_MODE_S *stills_mode
   {
   
        1600,                           // unsigned short max_width;   Maximum width resolution
        1200,                           // unsigned short max_height;  Maximum height resolution         
        0,                              // UInt32                data_size;                //Minimum amount of data sent by the camera
        8,                             // UInt32                framerate_lo_absolute;  //Minimum possible framerate u24.8 format
        15,                             // UInt32                framerate_hi_absolute;  //Maximum possible framerate u24.8 format
        CAMDRV_TRANSFORM_NONE,          // CAMDRV_TRANSFORM_T    transform;            //Possible transformations in this mode / user requested transformations                                   
        CAMDRV_IMAGE_JPEG,              // CAMDRV_IMAGE_TYPE_T    format;                //Image format of the frame.
        CAMDRV_IMAGE_YUV422_YCbYCr,     // CAMDRV_IMAGE_ORDER_T    image_order;        //Format pixel order in the frame.
        CAMDRV_DATA_SIZE_16BIT,         // CAMDRV_DATA_SIZE_T    image_data_size;    //Packing mode of the data.
        CAMDRV_DECODE_NONE,             // CAMDRV_DECODE_T        periph_dec;         //The decoding that the VideoCore transciever (eg CCP2) should perform on the data after reception.
        CAMDRV_ENCODE_NONE,             // PERIPHERAL_ENCODE_T    periph_enc;            //The encoding that the camera IF transciever (eg CCP2) should perform on the data before writing to memory.
        0,                              // int                    block_length;        //Block length for DPCM encoded data - specified by caller
        CAMDRV_DATA_SIZE_NONE,          // CAMDRV_DATA_SIZE_T    embedded_data_size; //The embedded data size from the frame.
        CAMDRV_MODE_VIDEO,              // CAMDRV_CAPTURE_MODE_T    flags;            //A bitfield of flags that can be set on the mode.
        8,                             // UInt32                framerate;            //Framerate achievable in this mode / user requested framerate u24.8 format
        0,                              // UInt8                mechanical_shutter;    //It is possible to use mechanical shutter in this mode (set by CDI as it depends on lens driver) / user requests this feature */  
        4                               // UInt32                pre_frame;            //Frames to throw out for Stills capture     
    },
 
    ///< Focus Settings & Capabilities:  CAMDRV_FOCUSCONTROL_S *focus_control_st;
    {
    #ifdef AUTOFOCUS_ENABLED
        CamFocusControlAuto,        	// CAMDRV_FOCUSCTRLMODE_T default_setting=CamFocusControlOff;
        CamFocusControlAuto,        	// CAMDRV_FOCUSCTRLMODE_T cur_setting;
        CamFocusControlOn |             // UInt32 settings;  Settings Allowed: CamFocusControlMode_t bit masked
        CamFocusControlOff |
        CamFocusControlAuto |
        CamFocusControlAutoLock |
        CamFocusControlCentroid |
        CamFocusControlQuickSearch |
        CamFocusControlInfinity |
        CamFocusControlMacro
    #else
        CamFocusControlOff,             // CAMDRV_FOCUSCTRLMODE_T default_setting=CamFocusControlOff;
        CamFocusControlOff,             // CAMDRV_FOCUSCTRLMODE_T cur_setting;
        CamFocusControlOff              // UInt32 settings;  Settings Allowed: CamFocusControlMode_t bit masked
    #endif
    },
    ///< Digital Zoom Settings & Capabilities:  CAMDRV_DIGITALZOOMMODE_S *digital_zoom_st;        
    {
        CamZoom_1_0,        ///< CAMDRV_ZOOM_T default_setting;  default=CamZoom_1_0:  Values allowed  CamZoom_t
        CamZoom_1_0,        ///< CAMDRV_ZOOM_T cur_setting;  CamZoom_t
        CamZoom_4_0,        ///< CAMDRV_ZOOM_T max_zoom;  Max Zoom Allowed (256/max_zoom = *zoom)
        TRUE                    ///< Boolean capable;  Sensor capable: TRUE/FALSE:
    },
    ///< Sensor ESD Settings & Capabilities:  CAMDRV_ESD_S *esd_st;
    {
        0x01,                           ///< UInt8 ESDTimer;  Periodic timer to retrieve the camera status (ms)
        FALSE                           ///< Boolean capable;  TRUE/FALSE:
    },
    CAMERA_IMAGE_INTERFACE,                ///< UInt32 intf_mode;  Sensor Interfaces to Baseband
    CAMERA_PHYS_INTF_PORT,                ///< UInt32 intf_port;  Sensor Interface Physical Port    
    "LUISA"    
};            



/*---------Sensor Primary Configuration CCIR656*/
static CamIntfConfig_CCIR656_st_t CamPrimaryCfg_CCIR656_st = {
	// Vsync, Hsync, Clock 
	CSL_CAM_SYNC_EXTERNAL,				///< UInt32 sync_mode;				(default)CAM_SYNC_EXTERNAL:  Sync External or Embedded
	CSL_CAM_SYNC_DEFINES_ACTIVE,		///< UInt32 vsync_control;			(default)CAM_SYNC_DEFINES_ACTIVE:		VSYNCS determines active data
	CSL_CAM_SYNC_ACTIVE_HIGH,			///< UInt32 vsync_polarity; 		   default)ACTIVE_LOW/ACTIVE_HIGH:		  Vsync active	
	CSL_CAM_SYNC_DEFINES_ACTIVE,		///< UInt32 hsync_control;			(default)FALSE/TRUE:					HSYNCS determines active data
	CSL_CAM_SYNC_ACTIVE_HIGH,			///< UInt32 hsync_polarity; 		(default)ACTIVE_HIGH/ACTIVE_LOW:		Hsync active 
	CSL_CAM_CLK_EDGE_NEG,				///< UInt32 data_clock_sample;		(default)RISING_EDGE/FALLING_EDGE:		Pixel Clock Sample edge
	CSL_CAM_PIXEL_8BIT, 				///< UInt32 bus_width;				(default)CAM_BITWIDTH_8:				Camera bus width
	0,							///< UInt32 data_shift; 				   (default)0:							   data shift (+) left shift  (-) right shift	 
	CSL_CAM_FIELD_H_V,					///< UInt32 field_mode; 			(default)CAM_FIELD_H_V: 				field calculated
	CSL_CAM_INT_FRAME_END,				///< UInt32 data_intr_enable;		CAM_INTERRUPT_t:  
	CSL_CAM_INT_FRAME_END,				///< UInt32 pkt_intr_enable;		CAM_INTERRUPT_t:  

};  //BYKIM_CHANGE CLK_EDGE

// ************************* REMEMBER TO CHANGE IT WHEN YOU CHANGE TO CCP ***************************
//CSI host connection
//---------Sensor Primary Configuration CCP CSI (sensor_config_ccp_csi)
static CamIntfConfig_CCP_CSI_st_t  CamPrimaryCfg_CCP_CSI_st = 
{
    CSL_CAM_INPUT_SINGLE_LANE,                    ///< UInt32 input_mode;     CSL_CAM_INPUT_MODE_T:
    CSL_CAM_INPUT_MODE_DATA_CLOCK,              ///< UInt32 clk_mode;       CSL_CAM_CLOCK_MODE_T:  
    CSL_CAM_ENC_NONE,                           ///< UInt32 encoder;        CSL_CAM_ENCODER_T
    FALSE,                                      ///< UInt32 enc_predictor;  CSL_CAM_PREDICTOR_MODE_t
    CSL_CAM_DEC_NONE,                           ///< UInt32 decoder;        CSL_CAM_DECODER_T
    FALSE,                                      ///< UInt32 dec_predictor;  CSL_CAM_PREDICTOR_MODE_t
    CSL_CAM_PORT_CHAN_0,                                 ///< UInt32 sub_channel;    CSL_CAM_CHAN_SEL_t
    TRUE,                                       ///< UInt32 term_sel;       BOOLEAN
    CSL_CAM_PIXEL_8BIT,                             ///< UInt32 bus_width;      CSL_CAM_BITWIDTH_t
    CSL_CAM_PIXEL_NONE,                         ///< UInt32 emb_data_type;  CSL_CAM_DATA_TYPE_T
    CSL_CAM_PORT_CHAN_0,                                 ///< UInt32 emb_data_channel; CSL_CAM_CHAN_SEL_t
    FALSE,                                      ///< UInt32 short_pkt;      BOOLEAN
    CSL_CAM_PIXEL_NONE,                         ///< UInt32 short_pkt_chan; CSL_CAM_CHAN_SEL_t
    CSL_CAM_INT_FRAME_END,                         ///< UInt32 data_intr_enable; CSL_CAM_INTERRUPT_t:  
    CSL_CAM_INT_FRAME_END,                          ///< UInt32 pkt_intr_enable;  CSL_CAM_INTERRUPT_t:  
}; 

/*---------Sensor Primary Configuration YCbCr Input*/
static CamIntfConfig_YCbCr_st_t CamPrimaryCfg_YCbCr_st = {
	// YCbCr(YUV422) Input format = YCbCr=YUV= U0 Y0 V0 Y1	 U2 Y2 V2 Y3 ....
	TRUE,					//[00] Boolean yuv_full_range;	   (default)FALSE: CROPPED YUV=16-240  TRUE: FULL RANGE YUV= 1-254	
	SensorYCSeq_CbYCrY, 	//[01] CamSensorYCbCrSeq_t sensor_yc_seq;	 (default) SensorYCSeq_YCbYCr								
	//	  SensorYCSeq_YCbYCr,	  //[01] CamSensorYCbCrSeq_t sensor_yc_seq;    (default) SensorYCSeq_YCbYCr 							  
	
	// Currently Unused
	FALSE,					//[02] Boolean input_byte_swap;    Currently UNUSED!! (default)FALSE:  TRUE:
	FALSE,					//[03] Boolean input_word_swap;    Currently UNUSED!! (default)FALSE:  TRUE:
	FALSE,					//[04] Boolean output_byte_swap;   Currently UNUSED!! (default)FALSE:  TRUE:
	FALSE,					//[05] Boolean output_word_swap;   Currently UNUSED!! (default)FALSE:  TRUE:
	
	// Sensor Color Conversion Coefficients:  color conversion fractional coefficients are scaled by 2^8
	//						 e.g. for R0 = 1.164, round(1.164 * 256) = 298 or 0x12a
	CAM_COLOR_R1R0, 		//[06] UInt32 cc_red R1R0;			YUV422 to RGB565 color conversion red
	CAM_COLOR_G1G0, 		//[07] UInt32 cc_green G1G0;		YUV422 to RGB565 color conversion green
	CAM_COLOR_B1			//[08] UInt32 cc_blue B1;			YUV422 to RGB565 color conversion blue

};


/*---------Sensor Primary Configuration IOCR */
static CamIntfConfig_IOCR_st_t CamPrimaryCfg_IOCR_st = {
	FALSE,              //[00] Boolean cam_pads_data_pd;      (default)FALSE: IOCR2 D0-D7 pull-down disabled       TRUE: IOCR2 D0-D7 pull-down enabled
    FALSE,              //[01] Boolean cam_pads_data_pu;      (default)FALSE: IOCR2 D0-D7 pull-up disabled         TRUE: IOCR2 D0-D7 pull-up enabled
    FALSE,              //[02] Boolean cam_pads_vshs_pd;        (default)FALSE: IOCR2 Vsync/Hsync pull-down disabled TRUE: IOCR2 Vsync/Hsync pull-down enabled
    FALSE,              //[03] Boolean cam_pads_vshs_pu;        (default)FALSE: IOCR2 Vsync/Hsync pull-up disabled   TRUE: IOCR2 Vsync/Hsync pull-up enabled
    FALSE,              //[04] Boolean cam_pads_clk_pd;         (default)FALSE: IOCR2 CLK pull-down disabled         TRUE: IOCR2 CLK pull-down enabled
    FALSE,              //[05] Boolean cam_pads_clk_pu;         (default)FALSE: IOCR2 CLK pull-up disabled           TRUE: IOCR2 CLK pull-up enabled
    
    7 << 12,  //[06] UInt32 i2c_pwrup_drive_strength;   (default)IOCR4_CAM_DR_12mA:   IOCR drive strength
    0x00,               //[07] UInt32 i2c_pwrdn_drive_strength;   (default)0x00:    I2C2 disabled
    0x00,               //[08] UInt32 i2c_slew;                           (default) 0x00: 42ns

    7 << 12,  //[09] UInt32 cam_pads_pwrup_drive_strength;   (default)IOCR4_CAM_DR_12mA:  IOCR drive strength
    1 << 12    //[10] UInt32 cam_pads_pwrdn_drive_strength;   (default)IOCR4_CAM_DR_2mA:   IOCR drive strength
};

/* XXXXXXXXXXXXXXXXXXXXXXXXXXX IMPORTANT XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
/* TO DO: MURALI */
/* HAVE TO PROGRAM THIS IN THE ISP. */
/*---------Sensor Primary Configuration JPEG */
static CamIntfConfig_Jpeg_st_t CamPrimaryCfg_Jpeg_st = {
    1632,                            ///< UInt32 jpeg_packet_size_bytes;     Bytes/Hsync
    1536,                           ///< UInt32 jpeg_max_packets;           Max Hsyncs/Vsync = (3.2Mpixels/4) / 512
    CamJpeg_FixedPkt_VarLine,       ///< CamJpegPacketFormat_t pkt_format;  Jpeg Packet Format
};

/* XXXXXXXXXXXXXXXXXXXXXXXXXXX IMPORTANT XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
/* TO DO: MURALI */
/* WILL NEED TO MODIFY THIS. */
/*---------Sensor Primary Configuration Stills n Thumbs */
static CamIntfConfig_PktMarkerInfo_st_t CamPrimaryCfg_PktMarkerInfo_st = {
	2,          ///< UInt8       marker_bytes; # of bytes for marker, (= 0 if not used)
    0,          ///< UInt8       pad_bytes; # of bytes for padding, (= 0 if not used)
    
    TRUE,       ///< Boolean     TN_marker_used; Thumbnail marker used
    0xFFBE,     ///< UInt32      SOTN_marker; Start of Thumbnail marker, (= 0 if not used)
    0xFFBF,     ///< UInt32      EOTN_marker; End of Thumbnail marker, (= 0 if not used)
    
    TRUE,       ///< Boolean     SI_marker_used; Status Info marker used
    0xFFBC,     ///< UInt32      SOSI_marker; Start of Status Info marker, (= 0 if not used)
    0xFFBD,     ///< UInt32      EOSI_marker; End of Status Info marker, (= 0 if not used)

    FALSE,      ///< Boolean     Padding_used; Status Padding bytes used
    0x0000,     ///< UInt32      SOPAD_marker; Start of Padding marker, (= 0 if not used)
    0x0000,     ///< UInt32      EOPAD_marker; End of Padding marker, (= 0 if not used)
    0x0000      ///< UInt32      PAD_marker; Padding Marker, (= 0 if not used)
};


//---------Sensor Primary Configuration Video n ViewFinder
static CamIntfConfig_InterLeaveMode_st_t CamPrimaryCfg_InterLeaveVideo_st = 
{
    CamInterLeave_SingleFrame,      ///< CamInterLeaveMode_t mode;              Interleave Mode
    CamInterLeave_PreviewLast,      ///< CamInterLeaveSequence_t sequence;      InterLeaving Sequence
    CamInterLeave_PktFormat         ///< CamInterLeaveOutputFormat_t format;    InterLeaving Output Format
};

//---------Sensor Primary Configuration Stills n Thumbs
static CamIntfConfig_InterLeaveMode_st_t CamPrimaryCfg_InterLeaveStills_st = 
{
    CamInterLeave_SingleFrame,      ///< CamInterLeaveMode_t mode;              Interleave Mode
    CamInterLeave_PreviewLast,      ///< CamInterLeaveSequence_t sequence;      InterLeaving Sequence
    CamInterLeave_PktFormat         ///< CamInterLeaveOutputFormat_t format;    InterLeaving Output Format
};


/*---------Sensor Primary Configuration */
static CamIntfConfig_st_t CamSensorCfg_st = {
	&CamPrimaryCfgCap_st,               // *sensor_config_caps;
    &CamPrimaryCfg_CCIR656_st,          // *sensor_config_ccir656;
    &CamPrimaryCfg_CCP_CSI_st,            // *sensor_config_ccp_csi;
    &CamPrimaryCfg_YCbCr_st,            // *sensor_config_ycbcr;
    NULL,
    &CamPrimaryCfg_IOCR_st,             // *sensor_config_iocr;
    &CamPrimaryCfg_Jpeg_st,             // *sensor_config_jpeg;
    &CamPrimaryCfg_InterLeaveVideo_st,  // *sensor_config_interleave_video;
    &CamPrimaryCfg_InterLeaveStills_st, // *sensor_config_interleave_stills;
    &CamPrimaryCfg_PktMarkerInfo_st,     // *sensor_config_pkt_marker_info;
	&CamPrimaryDefault_st
};

// --------Primary Sensor Frame Rate Settings
static CamFrameRate_st_t PrimaryFrameRate_st =
{
    CamRate_15,                     ///< CamRates_t default_setting; 
    CamRate_15,                     ///< CamRates_t cur_setting; 
    CamRate_15                      ///< CamRates_t max_setting;
};

// --------Secondary Sensor Frame Rate Settings
static CamFrameRate_st_t SecondaryFrameRate_st =
{
    CamRate_15,                     ///< CamRates_t default_setting; 
    CamRate_15,                     ///< CamRates_t cur_setting; 
    CamRate_15                      ///< CamRates_t max_setting;
};

//---------FLASH/TORCH State
static FlashLedState_t  stv0986_sys_flash_state = Flash_Off;
static Boolean          stv0986_fm_is_on        = FALSE;    
static Boolean          stv0986_torch_is_on     = FALSE;    

// --------Primary Sensor Flash State Settings
static CamFlashLedState_st_t PrimaryFlashState_st =
{
    Flash_Off,                      // FlashLedState_t default_setting:
    Flash_Off,                      // FlashLedState_t cur_setting;       
    (Flash_Off |                    // Settings Allowed: bit mask
        Flash_On |                       
        Torch_On |                       
        FlashLight_Auto )
};

// --------Secondary Sensor Flash State Settings
static CamFlashLedState_st_t SecondaryFlashState_st =
{
    Flash_Off,                      // FlashLedState_t default_setting:
    Flash_Off,                      // FlashLedState_t cur_setting;       
    Flash_Off                       // Settings Allowed: bit mask
};

// --------Sensor Rotation Mode Settings
static CamRotateMode_st_t RotateMode_st =
{
    CamRotate0,                     // CamRotate_t default_setting:
    CamRotate0,                     // CamRotate_t cur_setting;       
    CamRotate0                      // Settings Allowed: bit mask
};

// --------Sensor Mirror Mode Settings
static CamMirrorMode_st_t PrimaryMirrorMode_st =
{
    CamMirrorNone,                  // CamMirror_t default_setting:
    CamMirrorNone,                  // CamMirror_t cur_setting;       
    (CamMirrorNone |                // Settings Allowed: bit mask
        CamMirrorHorizontal |                       
        CamMirrorVertical)
};

// --------Sensor Mirror Mode Settings
static CamMirrorMode_st_t SecondaryMirrorMode_st =
{
    CamMirrorNone,                  // CamMirror_t default_setting:
    CamMirrorNone,                  // CamMirror_t cur_setting;       
    CamMirrorNone                   // Settings Allowed: bit mask
};

// --------Sensor Image Quality Settings
static CamSceneMode_st_t SceneMode_st =
{
    CamSceneMode_Auto,              // CamSceneMode_t default_setting:
    CamSceneMode_Auto,              // CamSceneMode_t cur_setting;        
    CamSceneMode_Auto               // Settings Allowed: bit mask
};
static CamDigitalEffect_st_t DigitalEffect_st =
{
    CamDigEffect_NoEffect,          // CamDigEffect_t default_setting:
    CamDigEffect_NoEffect,          // CamDigEffect_t cur_setting;        
    (CamDigEffect_NoEffect |        // Settings Allowed: bit mask
        CamDigEffect_MonoChrome |                       
        CamDigEffect_NegColor |                       
        CamDigEffect_Antique )
};
static CamFlicker_st_t Flicker_st =
{
    CamFlicker50Hz,                 // CamFlicker_t default_setting:
    CamFlicker50Hz,                 // CamFlicker_t cur_setting;      
    (CamFlickerAuto |               // Settings Allowed: bit mask
        CamFlicker50Hz |                       
        CamFlicker60Hz )
};
static CamWBMode_st_t WBmode_st =
{
    CamWB_Auto,                     // CamWB_WBMode_t default_setting:
    CamWB_Auto,                     // CamWB_WBMode_t cur_setting;        
    (CamWB_Auto |                   // Settings Allowed: bit mask
        CamWB_Daylight |                       
        CamWB_Incandescent|                       
        CamWB_WarmFluorescent|                       
        CamWB_Cloudy)
};
static CamExposure_st_t Exposure_st =
{
    CamExposure_Enable,             // CamExposure_t default_setting:
    CamExposure_Enable,             // CamExposure_t cur_setting;     
    CamExposure_Enable              // Settings Allowed: bit mask
};
static CamMeteringType_st_t Metering_st =
{
    CamMeteringType_CenterWeighted,           // CamMeteringType_t default_setting:
    CamMeteringType_CenterWeighted,           // CamMeteringType_t cur_setting;     
    (CamMeteringType_CenterWeighted |					// Settings Allowed: bit mask
        CamMeteringType_Matrix |					   
        CamMeteringType_Spot)
};
static CamSensitivity_st_t Sensitivity_st =
{
    CamSensitivity_Auto,            // CamSensitivity_t default_setting:
    CamSensitivity_Auto,            // CamSensitivity_t cur_setting;      
    CamSensitivity_Auto             // Settings Allowed: bit mask
};
static CamFunctionEnable_st_t CamWideDynRange_st =
{
    FALSE,                          // Boolean default_setting;
    FALSE,                          // Boolean cur_setting; 
    FALSE                           // Boolean configurable; 
};
static CamImageAppearance_st_t Contrast_st =
{
    CamContrast_0,             // Int8 default_setting:
    CamContrast_0,             // Int8 cur_setting;       
    TRUE                         // Boolean configurable;
};
static CamImageAppearance_st_t Brightness_st =
{
    CamBrightness_Nom,           // Int8 default_setting:
    CamBrightness_Nom,           // Int8 cur_setting;       
    FALSE                        // Boolean configurable;
};
static CamImageAppearance_st_t Saturation_st =
{
    CamSaturation_0,           // Int8 default_setting:
    CamSaturation_0,           // Int8 cur_setting;       
    TRUE                         // Boolean configurable;
};
static CamImageAppearance_st_t Hue_st =
{
    CamHue_Nom,                  // Int8 default_setting:
    CamHue_Nom,                  // Int8 cur_setting;       
    FALSE                        // Boolean configurable;
};
static CamImageAppearance_st_t Gamma_st =
{
    CamGamma_Nom,                // Int8 default_setting:
    CamGamma_Nom,                // Int8 cur_setting;       
    TRUE                         // Boolean configurable;
};
static CamImageAppearance_st_t Sharpness_st =
{
    CamSharpness_0,            // Int8 default_setting:
    CamSharpness_0,            // Int8 cur_setting;       
    FALSE                        // Boolean configurable;
};
static CamImageAppearance_st_t AntiShadingPower_st =
{
    CamAntiShadingPower_Nom,     // Int8 default_setting:
    CamAntiShadingPower_Nom,     // Int8 cur_setting;       
    FALSE                        // Boolean configurable;
};
static CamJpegQuality_st_t JpegQuality_st =
{
    CamJpegQuality_Max,         // Int8 default_setting:
    CamJpegQuality_Max,         // Int8 cur_setting;       
    TRUE                        // Boolean configurable;
};

static CamFunctionEnable_st_t CamFrameStab_st =
{
    FALSE,                          // Boolean default_setting;
    FALSE,                          // Boolean cur_setting;    
    FALSE                           // Boolean configurable;   
};
static CamFunctionEnable_st_t CamAntiShake_st =
{
    FALSE,                          // Boolean default_setting;
    FALSE,                          // Boolean cur_setting;    
    FALSE                           // Boolean configurable;   
};
static CamFunctionEnable_st_t CamFaceDetection_st =
{
    FALSE,                          // Boolean default_setting;
    FALSE,                          // Boolean cur_setting;    
    FALSE                           // Boolean configurable;   
};
static CamFunctionEnable_st_t CamAutoFocus_st =
{
    FALSE,                          // Boolean default_setting;
    FALSE,                          // Boolean cur_setting;    
    FALSE                           // Boolean configurable;   
};

// --------Sensor Image Quality Configuration
static CamSensorImageConfig_st_t ImageSettingsConfig_st =
{
    &SceneMode_st,          // CamSceneMode_st_t        *sensor_scene;              Scene Mode Setting & Capabilities                               
    &DigitalEffect_st,      // CamDigitalEffect_st_t    *sensor_digitaleffect;      Digital Effects Setting & Capabilities                          
    &Flicker_st,            // CamFlicker_st_t          *sensor_flicker;            Flicker Control Setting & Capabilities                          
    &WBmode_st,             // CamWBMode_st_t           *sensor_wb;                 White Balance Setting & Capabilities                            
    &Exposure_st,           // CamExposure_st_t         *sensor_exposure;           Exposure Setting & Capabilities                                 
    &Metering_st,           // CamMeteringType_st_t     *sensor_metering;           Metering Setting & Capabilities                                 
    &Sensitivity_st,        // CamSensitivity_st_t      *sensor_sensitivity;        Sensitivity Setting & Capabilities                              
    &CamWideDynRange_st,    // CamFunctionEnable_st_t   *sensor_wdr;                Wide Dynamic Range Setting & Capabilities                       
                                                                                                                                                    
    &PrimaryFrameRate_st,   // CamFrameRate_st_t        *sensor_framerate;          Frame Rate Output Settings & Capabilities                       
    &PrimaryFlashState_st,  // CamFlashLedState_st_t    *sensor_flashstate;         Flash Setting & Capabilities                                    
    &RotateMode_st,         // CamRotateMode_st_t       *sensor_rotatemode;         Rotation Setting & Capabilities                                 
    &PrimaryMirrorMode_st,  // CamMirrorMode_st_t       *sensor_mirrormode;         Mirror Setting & Capabilities                                   
    &JpegQuality_st,        // CamJpegQuality_st_t      *sensor_jpegQuality;        Jpeg Quality Setting & Capabilities:  Values allowed 0 to 10    
    &CamFrameStab_st,       // CamFunctionEnable_st_t   *sensor_framestab;          Frame Stabilization Setting & Capabilities                      
    &CamAntiShake_st,       // CamFunctionEnable_st_t   *sensor_antishake;          Anti-Shake Setting & Capabilities                               
    &CamFaceDetection_st,   // CamFunctionEnable_st_t   *sensor_facedetect;         Face Detection Setting & Capabilities                               
    &CamAutoFocus_st,       // CamFunctionEnable_st_t   *sensor_autofocus;          Auto Focus Setting & Capabilities         
                                                                                                                                                    
    &Contrast_st,           // CamImageAppearance_st_t   *sensor_contrast;          default=0:  Values allowed  -100 to 100, zero means no change   
    &Brightness_st,         // CamImageAppearance_st_t   *sensor_brightness;        default=0:  Values allowed  0=All black  100=All white          
    &Saturation_st,         // CamImageAppearance_st_t   *sensor_saturation;        default=0:  Values allowed  -100 to 100, zero means no change   
    &Hue_st,                // CamImageAppearance_st_t   *sensor_hue;               default=0:  Values allowed  -100 to 100, zero means no change   
    &Gamma_st,              // CamImageAppearance_st_t   *sensor_gamma;             default=0:  Values allowed  -100 to 100, zero means no change   
    &Sharpness_st,          // CamImageAppearance_st_t   *sensor_sharpness;         default=0:  Values allowed  -100 to 100, zero means no change   
    &AntiShadingPower_st    // CamImageAppearance_st_t   *sensor_antishadingpower;  default=0:  Values allowed  -100 to 100, zero means no change   
};

typedef struct samsung_short_t{
        unsigned short addr;
        unsigned short data;
} sr200pc10_short_t;



// --------I2C Specific Variables  
static CamIntfConfig_I2C_st_t CamIntfCfgI2C = {0, 0, 0, 0, 0, 0};                                                 
static HAL_CAM_Result_en_t        sCamI2cStatus = HAL_CAM_SUCCESS;        // capture callback function 
static HAL_CAM_Result_en_t     sSensorI2cStatus = HAL_CAM_SUCCESS;    	  // I2C status
static void*  sSemaphoreSensorI2c = NULL ;        						  // for coordinating I2C reads & writes




static HAL_CAM_Result_en_t
SensorSetPreviewMode(CamImageSize_t image_resolution,
		     CamDataFmt_t image_format);

static HAL_CAM_Result_en_t CAMDRV_SetFrameRate(CamRates_t fps,
				CamSensorSelect_t sensor);

static HAL_CAM_Result_en_t CAMDRV_SetZoom(CamZoom_t step, CamSensorSelect_t sensor);

void  CAMDRV_CheckModeChange(void);

//HAL_CAM_Result_en_t CamacqExtWriteI2cLists( const void *pvArg, int iResType ); //BYKIM_CAMACQ


//***************************************************************************
//
// Function Name:  SensorWriteI2c
//
// Description:  I2C write to Camera device 
//
// \param      camRegID  I2C device Sub-Addr
// \param      DataCnt   I2C data write count
// \param      Data      I2C data pointer
//
// \return     Result_t
//
// \note       Semaphore protected write, waits for callback to release semaphore
//               before returning with status
//
//***************************************************************************
HAL_CAM_Result_en_t SensorWriteI2c( UInt16 camRegID, UInt16 DataCnt, UInt8 *Data )
{
    HAL_CAM_Result_en_t result = 0;
    result |= CAM_WriteI2c(camRegID, DataCnt, Data);
	//Ratnesh : Fix it : for the time being I2CDRV_Write() is commented..
    //I2CDRV_Write( (I2C_WRITE_CB_t)SensorI2cCb, camI2cAccess );
	// obtain semaphore, released when camWCb is called
    return  sSensorI2cStatus;
}


//***************************************************************************
//
//       cam_WriteAutoIncrement1, write array to I2c port
//
//***************************************************************************
    UInt8 i2c_data[2000];
void cam_WriteAutoIncrement1( UInt16 sub_addr, ... )
{
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    va_list list;
    int i;
    UInt16 size = 0;
  //  char temp[8];

    if (sCamI2cStatus == HAL_CAM_SUCCESS)
    { 

      //  Log_DebugPrintf(LOGID_SYSINTERFACE_HAL_CAM, "cam_WriteAutoIncrement1(): i2c_sub_adr = 0x%x\r\n");
        va_start(list, sub_addr);

        i = va_arg( list, int );// next value in argument list.
        while(i!=-1)
        {
            i2c_data[size++] = (UInt8)i;              
            i = va_arg( list, int );// next value in argument list.
        }
        va_end(list);

        result |= SensorWriteI2c( sub_addr, (UInt16)size, i2c_data );  // write values to I2C
        if (result != HAL_CAM_SUCCESS)
        {
            sCamI2cStatus |= result; 
        }

       
#if 0
        sprintf(dstr, "cam_WriteAutoIncrement1(0x%4x", sub_addr);
        for (i=0; i<size; i++)
        {
            sprintf(temp, " 0x%02X,", i2c_data[i]);
            strcat(dstr, temp);
        }
        strcat(dstr, ")");        
        Log_DebugPrintf(LOGID_SYSINTERFACE_HAL_CAM,"%s\r\n", dstr);        
#endif       
    }
    else
    {
        //printk(KERN_INFO"cam_WriteAutoIncrement1(): sCamI2cStatus ERROR: \r\n");
    }
}

void cam_WriteAutoIncrement2( UInt16 sub_addr, int *p )
{
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    va_list list;
    int i = 0;
    UInt16 size = 0;

    if (sCamI2cStatus == HAL_CAM_SUCCESS)
    { 

      //  Log_DebugPrintf(LOGID_SYSINTERFACE_HAL_CAM, "cam_WriteAutoIncrement2(): i2c_sub_adr = 0x%x\r\n");
        i = *p;
		p++;
        while(i!=-1)
        {
            i2c_data[size++] = (UInt8)i;              
            i = *p;
			p++;
        }
        result |= SensorWriteI2c( sub_addr, (UInt16)size, i2c_data );  // write values to I2C
        if (result != HAL_CAM_SUCCESS)
        {
            sCamI2cStatus |= result; 
            printk(KERN_INFO"cam_WriteAutoIncrement2(): ERROR: \r\n");
        }

       
    }
    else
    {
        printk(KERN_INFO"cam_WriteAutoIncrement2(): sCamI2cStatus ERROR: \r\n");
    }
}

//***************************************************************************
//
//       cam_WaitValue, wait for mode change of stv0987
//
//***************************************************************************
HAL_CAM_Result_en_t cam_WaitValue(UInt32 timeout, UInt16 sub_addr, UInt8 value)
{
    UInt8 register_value;
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    u32 temp;
     printk(KERN_INFO"cam_WaitValue\r\n");
#if 0 	
	do
    {
        register_value = (stv0987_read(sub_addr));
	temp =  register_value;
	register_value &= 0x00FF;
        msleep( 1 * 1000 / 1000 );    // Minimum wait time is 500us (for SLEEP --> IDLE)
        timeout--;
    } while ( (timeout != 0) && (register_value != value) );

	
    if (timeout == 0)
    {
        result = HAL_CAM_ERROR_OTHERS;
        register_value = stv0987_read(bSystemErrorStatus);      
    }
#endif
    return result;
}

/*****************************************************************************
*
* Function Name:   CAMDRV_GetRegulator
*
* Description: Get Camera sensor's LDO
*
* Notes:
*
*****************************************************************************/
static int CAMDRV_GetRegulator()//swsw_dual
{
	int rc;
/*
	cam_regulator_i = regulator_get(NULL, "cam_vddi");
	if (!cam_regulator_i || IS_ERR(cam_regulator_i)) {
		printk(KERN_ERR "luisa_[cam_vddi]No Regulator available\n");
		rc = -EFAULT;
		return rc;
	}
*/	
	cam_regulator_a = regulator_get(NULL, "cam_vdda");
	if (!cam_regulator_a || IS_ERR(cam_regulator_a)) {
		printk(KERN_ERR "luisa_[cam_vdda]No Regulator available\n");
		rc = -EFAULT;
		return rc;
	}
	cam_regulator_c = regulator_get(NULL, "cam_vddc");
	if (!cam_regulator_c|| IS_ERR(cam_regulator_c)) {
		printk(KERN_ERR "luisa_[cam_vddc]No Regulator available\n");
		rc = -EFAULT;
		return rc;
	}
	return 1;

}
/*****************************************************************************
*
* Function Name:   CAMDRV_TurnOnRegulator
*
* Description: Turn on Camera sensor's Regulator
*
* Notes:
*
*****************************************************************************/
static int CAMDRV_TurnOnRegulator()//swsw_dual
{
	int rc;
	CAMDRV_GetRegulator();
	
	
    regulator_set_voltage(cam_regulator_c,1200000,1200000);	
	if (cam_regulator_c)
		rc = regulator_enable(cam_regulator_c);

            msleep(1);
    regulator_set_voltage(cam_regulator_a,2800000,2800000);
    if (cam_regulator_a)
    rc = regulator_enable(cam_regulator_a);

            msleep(1);
            gpio_direction_output(52, 1);//CAM_VGA_D_EN
            msleep(1);
            gpio_direction_output(23, 1);  //CAM_IO_EN
            msleep(1);
    
	
}

static void cam_InitStatus ()
{
    sCamI2cStatus = HAL_CAM_SUCCESS;
}

static HAL_CAM_Result_en_t cam_GetStatus ()
{
    return sCamI2cStatus;
}

/*****************************************************************************
*
* Function Name:   CAMDRV_TurnOffRegulator
*
* Description: Turn off Camera sensor's Regulator
*
* Notes:
*
*****************************************************************************/
static int CAMDRV_TurnOffRegulator()
{	
	int rc=1;


        gpio_direction_output(23, 0);  //CAM_IO_EN

        gpio_direction_output(52, 0);//CAM_VGA_D_EN

	if (cam_regulator_a)
		rc = regulator_disable(cam_regulator_a);	
        msleep(1);
	if (cam_regulator_c)
		rc = regulator_disable(cam_regulator_c);	
	return rc;
}
/*****************************************************************************
*
* Function Name:   CAMDRV_GetIntfConfig
*
* Description: Return Camera Sensor Interface Configuration
*
* Notes:
*
*****************************************************************************/
static CamIntfConfig_st_t *CAMDRV_GetIntfConfig(CamSensorSelect_t nSensor)
{

/* ---------Default to no configuration Table */
	CamIntfConfig_st_t *config_tbl = NULL;
        printk(KERN_INFO"CAMDRV_GetIntfConfig \r\n");

	switch (nSensor) {
	case CamSensorPrimary:	/* Primary Sensor Configuration */
	default:
		CamSensorCfg_st.sensor_config_caps = &CamPrimaryCfgCap_st;
		break;
	case CamSensorSecondary:	/* Secondary Sensor Configuration */
		CamSensorCfg_st.sensor_config_caps = NULL;
		break;
	}
	config_tbl = &CamSensorCfg_st;
	return config_tbl;
}

/*****************************************************************************
*
* Function Name:   CAMDRV_GetIntfSeqSel
*
* Description: Returns
*
* Notes:
*
*****************************************************************************/
static CamSensorIntfCntrl_st_t *CAMDRV_GetIntfSeqSel(CamSensorSelect_t nSensor,
					      CamSensorSeqSel_t nSeqSel,
					      UInt32 *pLength)
{

/* ---------Default to no Sequence  */
	CamSensorIntfCntrl_st_t *power_seq = NULL;
	*pLength = 0;
        printk(KERN_INFO"CAMDRV_GetIntfSeqSel \r\n");
	switch (nSeqSel) {
	case SensorInitPwrUp:	/* Camera Init Power Up (Unused) */
	case SensorPwrUp:

			//printk(KERN_INFO"SensorPwrUp Sequence\r\n");
			*pLength = sizeof(CamPowerOnSeq);
			power_seq = CamPowerOnSeq;

#if 0//swsw_dual

		if ((nSensor == CamSensorPrimary)
		    || (nSensor == CamSensorSecondary)) {
			//printk(KERN_INFO"SensorPwrUp Sequence\r\n");
			*pLength = sizeof(CamPowerOnSeq);
			power_seq = CamPowerOnSeq;
		}
#endif
		break;

	case SensorInitPwrDn:	/* Camera Init Power Down (Unused) */
	case SensorPwrDn:	/* Both off */

			*pLength = sizeof(CamPowerOffSeq);
			power_seq = CamPowerOffSeq;

#if 0//swsw_dual

		if ((nSensor == CamSensorPrimary)
		    || (nSensor == CamSensorSecondary)) {
			//printk(KERN_INFO"SensorPwrDn Sequence\r\n");
			*pLength = sizeof(CamPowerOffSeq);
			power_seq = CamPowerOffSeq;
		}

#endif
		break;

	case SensorFlashEnable:	/* Flash Enable */
		break;

	case SensorFlashDisable:	/* Flash Disable */
		break;

	default:
		break;
	}
	return power_seq;

}

/***************************************************************************
*
*
*       CAMDRV_Supp_Init performs additional device specific initialization
*
*   @return  HAL_CAM_Result_en_t
*
*       Notes:
****************************************************************************/
static HAL_CAM_Result_en_t CAMDRV_Supp_Init(CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t ret_val = HAL_CAM_SUCCESS;

	return ret_val;
}				

//BYKIM_CAMACQ
static HAL_CAM_Result_en_t CAMDRV_SensorSetConfigTablePts(CamSensorSelect_t sensor);
static HAL_CAM_Result_en_t Init_luisa_sensor(CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    struct stCamacqSensorManager_t* pstSensorManager = NULL; 
    struct stCamacqSensor_t* pstSensor = NULL;
    S32 iRet = 0;

    printk(KERN_ERR"%s(): , sensor : %d \r\n", __FUNCTION__, sensor );

       cam_InitStatus(); 
    
    pstSensorManager = GetCamacqSensorManager();
    if( pstSensorManager == NULL )
    {
        printk(KERN_ERR"pstSensorManager is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }

    pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
    if( pstSensor == NULL )
    {
        printk(KERN_ERR"pstSensor is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }
    
    	// Set Config Table Pointers for Selected Sensor:
    	CAMDRV_SensorSetConfigTablePts(sensor);
    
	printk(KERN_ERR"write ISX005_init0\n");
       Drv_Capturemode = FALSE;  //BYKIM_TEMP_TORINO
	// CamacqExtWriteI2cLists(sr200pc10_init0,1);   // wingi 
       iRet =  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SETPLL );
	if(iRet<0)
	{
	    printk(KERN_ERR"write CAMACQ_SENSORDATA_SETPLL error \n");
  	    return HAL_CAM_ERROR_INTERNAL_ERROR;
	}
       CAMDRV_Calibration();
        iRet =  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_INIT );
	if(iRet<0)
	{
	    printk(KERN_ERR"write CAMACQ_SENSORDATA_INIT error \n");
  	    return HAL_CAM_ERROR_INTERNAL_ERROR;
	}
      //reset settings	
       Drv_Scene =  0;
       Drv_Brightness=0;
       Drv_WB=0;
       Drv_ME=0;
       Drv_Effect=0;
       Drv_Size = 0;
     //  Drv_Iso = 0;
       Drv_Zoom= 0;
       Drv_DTPmode= 0;
       Drv_Mode = -1;

        printk(KERN_ERR"Init_luisa_sensor end \n");
        return HAL_CAM_SUCCESS;
	
#if 0 
	printk(KERN_ERR"write src200pc10_preview_table\n");
	// CamacqExtWriteI2cLists(sr200pc10_preview_table,1);   // wingi 
	pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_PREVIEW );
#endif
}
	
	


/****************************************************************************
*
* Function Name:   HAL_CAM_Result_en_t CAMDRV_Wakeup(CamSensorSelect_t sensor)
*
* Description: This function wakesup camera via I2C command.  Assumes camera
*              is enabled.
*
* Notes:
*
****************************************************************************/
static HAL_CAM_Result_en_t CAMDRV_Wakeup(CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct timeval start, end;
	printk(KERN_ERR"CAMDRV_Wakeup :  \r\n");
	result = Init_luisa_sensor(sensor);
	//Call totoro_init() before
	printk(KERN_ERR"Init_luisa_sensor :%d \r\n",result);
	return result;
}

static UInt16 CAMDRV_GetDeviceID(CamSensorSelect_t sensor)
{
	printk(KERN_ERR"CAMDRV_GetDeviceID : Empty \r\n");
        return 0;
}



/** The CAMDRV_GetResolution returns the sensor output size of the image resolution requested
    @param [in] size
        Image size requested from Sensor.
    @param [in] mode
        Capture mode for resolution requested.
    @param [in] sensor
        Sensor for which resolution is requested.
    @param [out] *sensor_size
        Actual size of requested resolution.
    
    @return Result_t
        status returned.
 */
static HAL_CAM_Result_en_t CAMDRV_GetResolution( 
			CamImageSize_t size, 
			CamCaptureMode_t mode, 
			CamSensorSelect_t sensor,
			HAL_CAM_ResolutionSize_st_t *sensor_size )

{
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;;
	printk(KERN_ERR"CAMDRV_GetResolution : Empty \r\n");
    return result;
}

//****************************************************************************
// Function Name:   HAL_CAM_Result_en_t CAMDRV_SetImageQuality(Int8 quality, CamSensorSelect_t sensor)
// Description:     Set the JPEG Quality (quantization) level [0-100]:
// Notes:       This function can be for average user's use.
//****************************************************************************
static HAL_CAM_Result_en_t CAMDRV_SetImageQuality(UInt8 quality, CamSensorSelect_t sensor)
{
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    UInt8 setting;
                                                   
    printk(KERN_ERR"CAMDRV_SetImageQuality(): quality, Empty\r\n");
    return result;
}


//****************************************************************************
//
// Function Name:   Result_t CAMDRV_SetVideoCaptureMode(CAMDRV_RESOLUTION_T image_resolution, CAMDRV_IMAGE_TYPE_T image_format)
//
// Description: This function configures Video Capture (Same as ViewFinder Mode)
//
// Notes:
//
//****************************************************************************

static HAL_CAM_Result_en_t CAMDRV_SetVideoCaptureMode(
        CamImageSize_t image_resolution, 
        CamDataFmt_t image_format,
        CamSensorSelect_t sensor,
        CamMode_t mode
        )

{

    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    struct stCamacqSensorManager_t* pstSensorManager = NULL;
    struct stCamacqSensor_t* pstSensor = NULL;
    
    printk(KERN_ERR"%s(): , mode : %d \r\n", __FUNCTION__, mode );

    pstSensorManager = GetCamacqSensorManager();
    if( pstSensorManager == NULL )
    {
        printk(KERN_ERR"pstSensorManager is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }

    pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
    if( pstSensor == NULL )
    {
        printk(KERN_ERR"pstSensor is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }
    
#if 0 
	printk(KERN_ERR"write src200pc10_init0\n");
	// CamacqExtWriteI2cLists(sr200pc10_init0,1);   // wingi 
    pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_INIT );
#endif
    // CamacqExtWriteI2cLists(sr200pc10_preview_table, 1); // wingi
    pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_PREVIEW);
    
     if(mode==CamVideo)
    {
	printk(KERN_ERR"CAMDRV_SetVideoCaptureMode(): Video Preview!!!!\r\n");
        pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_CAMCORDER);	
    }
	Drv_Mode = mode;
	//To do
    
    msleep(200); 	//wait mode change
    return result;
}


/****************************************************************************
*
* Function Name:   HAL_CAM_Result_en_t CAMDRV_SetFrameRate(CamRates_t fps)
*
* Description: This function sets the frame rate of the Camera Sensor
*
* Notes:    15 or 30 fps are supported.
*
****************************************************************************/
static HAL_CAM_Result_en_t CAMDRV_SetFrameRate(CamRates_t fps,
					CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    struct stCamacqSensorManager_t* pstSensorManager = NULL;
    struct stCamacqSensor_t* pstSensor = NULL;
    
    printk(KERN_ERR"%s(): , fps : %d , Drv_fps = %d \r\n", __FUNCTION__, fps,Drv_fps );

   if(((gv_ForceSetSensor==NORMAL_SET)&&(fps==Drv_fps))||(gv_ForceSetSensor==FORCELY_SKIP))	
   {
	Drv_fps= fps;
	return HAL_CAM_SUCCESS;
   }	   	

    pstSensorManager = GetCamacqSensorManager();
    if( pstSensorManager == NULL )
    {
        printk(KERN_ERR"pstSensorManager is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }

    pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
    if( pstSensor == NULL )
    {
        printk(KERN_ERR"pstSensor is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }
	switch( fps )
	{
             case CamRate_15:
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_fps_fixed_15,CAMACQ_SENSOR_MAIN);
             break;

             case CamRate_10: 
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_fps_fixed_10,CAMACQ_SENSOR_MAIN); //BYKIM_0923
             break;

             #if 0 
             case CamRate_24:
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_fps_fixed_24,CAMACQ_SENSOR_MAIN );
             break;

             case CamRate_30:
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_fps_var_30,CAMACQ_SENSOR_MAIN );
             break;
	     #endif		
		default:
		printk(KERN_ERR"not supported framerate \r\n");
			break;
	}

	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetFrameRate(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
	Drv_fps = fps;
	return result;
}

//****************************************************************************
//
// Function Name:   Result_t CAMDRV_SensorSetSleepMode()
//
// Description: This function sets the ISP in Sleep Mode
//
// Notes:
//
//****************************************************************************
static HAL_CAM_Result_en_t CAMDRV_SensorSetSleepMode(void)
{
    UInt8 register_value;
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	printk(KERN_ERR"CAMDRV_SensorSetSleepMode(): Empty\r\n");
 
    return result;   
} 


//****************************************************************************
//
// Function Name:   Result_t CAMDRV_EnableVideoCapture(CamSensorSelect_t sensor)
//
// Description: This function starts camera video capture mode
//
// Notes:
//                  SLEEP -> IDLE -> Movie
//****************************************************************************
static HAL_CAM_Result_en_t CAMDRV_EnableVideoCapture(CamSensorSelect_t sensor)
{
    UInt8 register_value,error_value;
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	printk(KERN_ERR"CAMDRV_EnableVideoCapture(): Empty\r\n");
    return result;
}


/****************************************************************************
/
/ Function Name:   HAL_CAM_Result_en_t CAMDRV_SetZoom(UInt8 numer, UInt8 denum)
/
/ Description: This function performs zooming via camera sensor
/
/ Notes:
/
****************************************************************************/
static HAL_CAM_Result_en_t CAMDRV_SetZoom(CamZoom_t step, CamSensorSelect_t sensor)
{

	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;
#if 1 

	printk(KERN_ERR"CAMDRV_SetZoom() called,step = %d, Drv_Zoom= %d\r\n",step,Drv_Zoom);
       if(step==Drv_Zoom)	
       {
		printk(KERN_ERR"Do not set zoom \r\n");
		return HAL_CAM_SUCCESS;
	}	   	

	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	switch( step )
	{
             case CamZoom_1_0:
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_00,CAMACQ_SENSOR_MAIN );
              //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_0);
             break;
             
             case CamZoom_1_125:
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_01,CAMACQ_SENSOR_MAIN );
                //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_1);
             break;
             
             case CamZoom_1_25:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_02,CAMACQ_SENSOR_MAIN );
                //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_2);
             break;
             
             case CamZoom_1_375:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_03,CAMACQ_SENSOR_MAIN );
              //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_3);
             break;
             
             case CamZoom_1_5:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_04,CAMACQ_SENSOR_MAIN );
              //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_4);
              break;
             
             case CamZoom_1_625:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_05,CAMACQ_SENSOR_MAIN );
             //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_5);
             break;
             
             case CamZoom_1_75:
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_06,CAMACQ_SENSOR_MAIN );
              // pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_6);
             break;
             
             case CamZoom_1_875:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_07,CAMACQ_SENSOR_MAIN );
             // pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_7);
             break;
             
             case CamZoom_2_0:
	     CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_08,CAMACQ_SENSOR_MAIN );
             //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_8);
             break;

             case CamZoom_1_035:
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_09,CAMACQ_SENSOR_MAIN );
                //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_1);
             break;
             
             case CamZoom_1_07:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_10,CAMACQ_SENSOR_MAIN);
                //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_2);
             break;
             
             case CamZoom_1_105:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_11,CAMACQ_SENSOR_MAIN );
              //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_3);
             break;
             
             case CamZoom_1_14:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_12,CAMACQ_SENSOR_MAIN );
              //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_4);
              break;
             
             case CamZoom_1_175:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_13,CAMACQ_SENSOR_MAIN );
             //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_5);
             break;
             
             case CamZoom_1_21:
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_14,CAMACQ_SENSOR_MAIN );
              // pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_6);
             break;
             
             case CamZoom_1_245:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_15,CAMACQ_SENSOR_MAIN );
             // pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_7);
             break;
             
             case CamZoom_1_28:
	     CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_16,CAMACQ_SENSOR_MAIN );
             //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_8);
             break;
			 
             case CamZoom_1_075:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_17,CAMACQ_SENSOR_MAIN );
              //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_3);
             break;
             
             case CamZoom_1_225:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_18,CAMACQ_SENSOR_MAIN );
              //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_4);
              break;
             
             case CamZoom_1_3:
	      CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_19,CAMACQ_SENSOR_MAIN );
             //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_5);
             break;
             
             case CamZoom_1_45:
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_20,CAMACQ_SENSOR_MAIN);
              // pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_6);
             break;
                          
             case CamZoom_1_525:
	     CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_21,CAMACQ_SENSOR_MAIN );
             //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_8);
             break;
			 
             case CamZoom_1_15:
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_22,CAMACQ_SENSOR_MAIN );
              // pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_6);
             break;
                          
             case CamZoom_1_6:
	     CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_zoom_23,CAMACQ_SENSOR_MAIN );
             //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ZOOM_8);
             break;
			 
             default:
                 printk(KERN_ERR"not supported zoom step \r\n");
             break;
        }

	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetBrightness(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
#endif	
    Drv_Zoom=  step;
    return result;
}

/****************************************************************************
/
/ Function Name:   void CAMDRV_SetCamSleep(CamSensorSelect_t sensor )
/
/ Description: This function puts ISP in sleep mode & shuts down power.
/
/ Notes:
/
****************************************************************************/
static HAL_CAM_Result_en_t CAMDRV_SetCamSleep(CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	printk(KERN_ERR"CAMDRV_SetCamSleep(): Empty\r\n");

	/* To be implemented. */
	return result;
}

static void CAMDRV_StoreBaseAddress(void *virt_ptr)
{
}

static UInt32 CAMDRV_GetJpegSize(CamSensorSelect_t sensor, void *data)
{

	UInt8 register_value;
    UInt32 jpeg_size=0;
	printk(KERN_ERR"CAMDRV_GetJpegSize(): Empty\r\n");

    return jpeg_size;

}


static UInt16 *CAMDRV_GetJpeg(short *buf)
{
	return (UInt16 *) NULL;
}

static UInt8 *CAMDRV_GetThumbnail(void *buf, UInt32 offset)
{
	return (UInt8 *)NULL;
}

//****************************************************************************
//
// Function Name:   Result_t CAMDRV_DisableCapture(CamSensorSelect_t sensor)
//
// Description: This function halts camera video capture
//
// Notes:
//                  ViewFinder -> IDLE
//                  Movie -> IDLE
//                  Stills -> IDLE
//****************************************************************************
static HAL_CAM_Result_en_t CAMDRV_DisableCapture(CamSensorSelect_t sensor)
{
    UInt8 register_value,error_value;
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	printk(KERN_ERR"CAMDRV_DisableCapture(): Empty\r\n");
    return result;
}


/****************************************************************************
/
/ Function Name:   HAL_CAM_Result_en_t CAMDRV_DisablePreview(void)
/
/ Description: This function halts MT9M111 camera video
/
/ Notes:
/
****************************************************************************/
static HAL_CAM_Result_en_t CAMDRV_DisablePreview(CamSensorSelect_t sensor)
{
	printk(KERN_ERR"CAMDRV_DisablePreview(): Empty\r\n");
	return sCamI2cStatus;
}


//****************************************************************************
//
// Function Name:   Result_t CAMDRV_CfgStillnThumbCapture(CamImageSize_t image_resolution, CAMDRV_IMAGE_TYPE_T format, CamSensorSelect_t sensor)
//
// Description: This function configures Stills Capture
//
// Notes:
//
//****************************************************************************
static HAL_CAM_Result_en_t CAMDRV_CfgStillnThumbCapture(
        CamImageSize_t stills_resolution, 
        CamDataFmt_t stills_format,
        CamImageSize_t thumb_resolution, 
        CamDataFmt_t thumb_format,
        CamSensorSelect_t sensor
        )

{
    UInt8 register_value = 0,error_value = 0;
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    struct stCamacqSensorManager_t* pstSensorManager = NULL;
    struct stCamacqSensor_t* pstSensor = NULL;
    UInt8  Outdoor_Flag = 0, Low_Flag=0, Night_Flag=0;//BYKIM_PREVENT
	
	printk(KERN_ERR"%s(): \r\n, sensor : %d ", __FUNCTION__, sensor );

    pstSensorManager = GetCamacqSensorManager();
    if( pstSensorManager == NULL )
    {
        printk(KERN_ERR"pstSensorManager is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }

    pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
    if( pstSensor == NULL )
    {
        printk(KERN_ERR"pstSensor is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }
	
#if 1	
    // CamacqExtWriteI2cLists1(sr200pc10_capture_table, 1); // this
    printk(KERN_ERR"gv_Nightshot_mode = %d\r\n",gv_Nightshot_mode);

    //check outdoor condition
    CAMDRV_GetCurrentEIT(sensor,&Outdoor_Flag);
	
     if(gv_Nightshot_mode==TRUE)
     {
          CAMDRV_GetCurrentLux(sensor, &Night_Flag);   
     }
     else
     {
          CAMDRV_GetCurrentLux(sensor, &Low_Flag);   
     }
     printk(KERN_ERR"Outdoor_Flag[%d],Low_Flag[%d],Night_Flag[%d]\r\n",Outdoor_Flag,Low_Flag,Night_Flag);

	// reset  mode bits. 
        {
               u8 INT_CLR_ALL[3]={0x00,0xfc,0x1f};  
               CamacqExtWriteI2c( pstSensor->m_pI2cClient,INT_CLR_ALL,3);
         }


      if(Outdoor_Flag==TRUE)
        { 
  	    pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_CAPTURE_OUTDOOR );
        }
    else
        { 
        if(gv_Nightshot_mode==TRUE)
        { 
            if(Night_Flag==TRUE)
        { 
  	    pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_CAPTURE_NIGHTSCENE );
        }
        else
        {
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_CAPTURE_NIGHTSCENE_OFF );
            }          
        }
        else
        {
            if(Low_Flag==TRUE)
            { 
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_CAPTURE_LOWLUX );
            }            
            else
            {
  	    pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_CAPTURE_NORMAL );
        }
        }
    }
#else
  	    pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_CAPTURE );
#endif

       CAMDRV_CheckModeChange();	//To check mode change

	  return result; //BYKIM_PREVENT
}

/****************************************************************************
/ Function Name:   HAL_CAM_Result_en_t CAMDRV_SetSceneMode(
/					CamSceneMode_t scene_mode)
/
/ Description: This function will set the scene mode of camera
/ Notes:
****************************************************************************/

static HAL_CAM_Result_en_t CAMDRV_SetSceneMode(CamSceneMode_t scene_mode,
					CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
        struct stCamacqSensorManager_t* pstSensorManager = NULL;
        struct stCamacqSensor_t* pstSensor = NULL;
	
	printk(KERN_ERR"CAMDRV_SetSceneMode() called, scene_mode =  0x%08x Drv_Scene= 0x%08x\r\n",scene_mode,Drv_Scene);
#if 1 
       if(scene_mode==Drv_Scene)	
       {
		printk(KERN_ERR"Do not set scene_mode \r\n");
		
#if 0//swsw_dual

		if(scene_mode!=CamSceneMode_Auto)
		{
                    gv_ForceSetSensor=FORCELY_SKIP;
		}
#endif
		return HAL_CAM_SUCCESS;
	}	   	

        pstSensorManager = GetCamacqSensorManager();
        if( pstSensorManager == NULL )
        {
            printk(KERN_ERR"pstSensorManager is NULL \r\n");
            return HAL_CAM_ERROR_OTHERS;
        }
    
        pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
        if( pstSensor == NULL )
        {
            printk(KERN_ERR"pstSensor is NULL \r\n");
            return HAL_CAM_ERROR_OTHERS;
        }
	
	gv_Nightshot_mode =FALSE;

        if((Drv_Scene==CamSceneMode_Auto)&&(scene_mode!=CamSceneMode_Auto)) //It means reset all settings
        {
            gv_ForceSetSensor=FORCELY_SKIP;
            printk(KERN_ERR"gv_ForceSetSensor is FORCELY_SKIP [%d]: Write INIT values\r\n",gv_ForceSetSensor);	
          //  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_ISO_AUTO);
            pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_AE_CENTERWEIGHTED);
            pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_EFFECT_NONE);
            pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_WB_AUTO);
            pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_BRIGHTNESS_4);
        }

	switch(scene_mode) 
	{
           case CamSceneMode_Auto:
	    {
	       pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_NONE);
               if(Drv_Scene>CamSceneMode_Auto) //It means reset all settings
               	{
                    gv_ForceSetSensor=FORCELY_SET;
                    printk(KERN_ERR"gv_ForceSetSensor is FORCELY_SET [%d] : Rollback the values\r\n",gv_ForceSetSensor);	
               	}
            }
		break;
            case CamSceneMode_Candlelight:
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_CANDLE);
            break;
            case CamSceneMode_Landscape:
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_LANDSCAPE);
            break;
            case CamSceneMode_Sunset:
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_SUNSET);
            break;
            case CamSceneMode_Fallcolor:
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_FALL);
            break;
            case CamSceneMode_Night:
            {        
                gv_Nightshot_mode =TRUE;
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_NIGHT);
            }
            break;
            case CamSceneMode_Party_Indoor:
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_PARTY_INDOOR);
            break;
            case CamSceneMode_Dusk_Dawn:
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_DAWN);
            break;
            case CamSceneMode_Againstlight:
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_AGAINST_LIGHT);
            break;
            
            case CamSceneMode_Sports:
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_SPORTS);
            break;
            
            case CamSceneMode_Beach_Snow:
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_BEACH_SNOW);
            break;
		
            case CamSceneMode_Firework:
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_FIRE);
            break;
			
	   default:
	   	scene_mode = CamSceneMode_Auto;
		 //  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SCENE_NONE);
		break;
    }
	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetSceneMode(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
    }
#endif	
    Drv_Scene=scene_mode;    
    return result;
}

/****************************************************************************
/ Function Name:   HAL_CAM_Result_en_t CAMDRV_SetWBMode(CamWB_WBMode_t wb_mode)
/
/ Description: This function will set the white balance of camera
/ Notes:
****************************************************************************/
//BYKIM_CAMACQ

static HAL_CAM_Result_en_t CAMDRV_SetWBMode(CamWB_WBMode_t wb_mode,
				     CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
        struct stCamacqSensorManager_t* pstSensorManager = NULL;
        struct stCamacqSensor_t* pstSensor = NULL;
		
        printk(KERN_ERR"CAMDRV_SetWBMode() called, wb_mode = 0x%08x  Drv_WB=  0x%08x \r\n",wb_mode,Drv_WB);
#if 1
       if(((gv_ForceSetSensor==NORMAL_SET)&&(wb_mode==Drv_WB))||(gv_ForceSetSensor==FORCELY_SKIP))	
       {
		Drv_WB= wb_mode;
		return HAL_CAM_SUCCESS;
	}	   	

	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
    {
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
           
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
    switch( wb_mode )
    {
        case CamWB_Auto:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_WB_AUTO);
            break;
        case CamWB_Daylight:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_WB_DAYLIGHT);
            break;
        case CamWB_Incandescent:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_WB_INCANDESCENT);
            break;
        case CamWB_DaylightFluorescent:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_WB_FLUORESCENT);
            break;
        case CamWB_Cloudy:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_WB_CLOUDY);
            break;
        default:
            wb_mode = CamWB_Auto;
               
            break;
    }

    if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetWBMode(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
#endif	
    Drv_WB = wb_mode;
    return result;
}


//BYKIM_TUNING

/****************************************************************************
/ Function Name:   HAL_CAM_Result_en_t CAMDRV_SetMeteringType(CamMeteringType_t ae_mode)
/
/ Description: This function will set the metering exposure of camera
/ Notes:
****************************************************************************/

static HAL_CAM_Result_en_t CAMDRV_SetMeteringType(CamMeteringType_t type,
				     CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
        struct stCamacqSensorManager_t* pstSensorManager = NULL;
        struct stCamacqSensor_t* pstSensor = NULL;
		
        printk(KERN_ERR"CAMDRV_SetMeteringType() called,ae_mode = 0x%08x Drv_ME= 0x%08x  \r\n",type,Drv_ME);
#if 1 
       if(((gv_ForceSetSensor==NORMAL_SET)&&(type==Drv_ME))||(gv_ForceSetSensor==FORCELY_SKIP))	
       {
		Drv_ME = type;
		return HAL_CAM_SUCCESS;
	}	   	
    
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
    switch( type )
    {
        case CamMeteringType_CenterWeighted:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_AE_CENTERWEIGHTED);
            break;
        case CamMeteringType_Matrix:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_AE_MATRIX);
            break;
        case CamMeteringType_Spot:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_AE_SPOT);
            break;
        default:
		printk(KERN_ERR"not supported ae_mode \r\n");
               
            break;
    }

    if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetMeteringType(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
#endif	
	
    Drv_ME = type;	
    return result;
}

/****************************************************************************
/ Function Name:   HAL_CAM_Result_en_t CAMDRV_SetAntiBanding(
/					CamAntiBanding_t effect)
/
/ Description: This function will set the antibanding effect of camera
/ Notes:
****************************************************************************/

static HAL_CAM_Result_en_t CAMDRV_SetAntiBanding(CamAntiBanding_t effect,
					  CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	printk(KERN_ERR"CAMDRV_SetAntiBanding() called Empty!!,effect = %d\r\n",effect);
	
	
	return result;
}

/****************************************************************************
/ Function Name:   HAL_CAM_Result_en_t CAMDRV_SetFlashMode(
					FlashLedState_t effect)
/
/ Description: This function will set the flash mode of camera
/ Notes:
****************************************************************************/

static HAL_CAM_Result_en_t CAMDRV_SetFlashMode(FlashLedState_t effect,
					CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	printk(KERN_ERR"CAMDRV_SetFlashMode() called Empty!!,effect = %d\r\n",effect);
	
	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetFlashMode(): Error[%d] \r\n",sCamI2cStatus);
		result = sCamI2cStatus;
	}
    return result;
}

/****************************************************************************
/ Function Name:   HAL_CAM_Result_en_t CAMDRV_SetFocusMode(
/					CamFocusStatus_t effect)
/
/ Description: This function will set the focus mode of camera
/ Notes:
****************************************************************************/

static HAL_CAM_Result_en_t CAMDRV_SetFocusMode(CamFocusControlMode_t effect,
					CamSensorSelect_t sensor)
{
	printk(KERN_ERR"CAMDRV_SetFocusMode() called Empty!!, effect = %d \r\n",effect);
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;

	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetFocusMode(): Error[%d] \r\n",
			 sCamI2cStatus);
		result = sCamI2cStatus;
	}
	return result;
}

static HAL_CAM_Result_en_t CAMDRV_TurnOffAF()
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	printk(KERN_ERR"CAMDRV_TurnOffAF() called Empty!! \r\n");
	
	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_TurnOnAF(): Error[%d] \r\n",
			 sCamI2cStatus);
		result = sCamI2cStatus;
	}
	return result;
}

static HAL_CAM_Result_en_t CAMDRV_TurnOnAF()
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	printk(KERN_ERR"CAMDRV_TurnOnAF() called Empty!! \r\n");
	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_TurnOnAF(): Error[%d] \r\n",
			 sCamI2cStatus);
		result = sCamI2cStatus;
	}
	return result;
}

/****************************************************************************
/ Function Name:   HAL_CAM_Result_en_t CAMDRV_SetJpegQuality(
/					CamFocusStatus_t effect)
/
/ Description: This function will set the focus mode of camera
/ Notes:
****************************************************************************/
static HAL_CAM_Result_en_t CAMDRV_SetJpegQuality(CamJpegQuality_t quality,
					  CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;

       printk(KERN_INFO"CAMDRV_SetJpegQuality() called, quality = %d Drv_Quality= %d Empty \r\n",quality,Drv_Quality);
       if(quality==Drv_Quality)	
       {
		printk(KERN_ERR"Do not set quality \r\n");
		return HAL_CAM_SUCCESS;
	}	   	
#if 1
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	switch( quality )
	{
		case CamJpegQuality_Min:
		//pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_QUALITY_N);
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_jpeg_quality_normal,CAMACQ_SENSOR_MAIN );
			break;
		case CamJpegQuality_Nom:
		//pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_QUALITY_F);
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_jpeg_quality_fine,CAMACQ_SENSOR_MAIN );
			break;
		case CamJpegQuality_Max:
		//pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_QUALITY_SF);
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_jpeg_quality_superfine,CAMACQ_SENSOR_MAIN );
			break;
		default:
		printk(KERN_ERR"not supported quality \r\n");

			break;
	}

	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetDigitalEffect(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
         Drv_Quality=quality;    
	//result = CAMDRV_SetImageQuality(quality,sensor);
#endif	
	return result;
}

/****************************************************************************
/ Function Name:   HAL_CAM_Result_en_t CAMDRV_SetDigitalEffect(
/					CamDigEffect_t effect)
/
/ Description: This function will set the digital effect of camera
/ Notes:
****************************************************************************/
static HAL_CAM_Result_en_t CAMDRV_SetDigitalEffect(CamDigEffect_t effect,
					    CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;

	printk(KERN_ERR"CAMDRV_SetDigitalEffect() called,effect = 0x%08x Drv_Effect= 0x%08x \r\n",effect,Drv_Effect);
#if 1 
       if(((gv_ForceSetSensor==NORMAL_SET)&&(effect==Drv_Effect))||(gv_ForceSetSensor==FORCELY_SKIP))	
       {
		Drv_Effect =effect ;
		return HAL_CAM_SUCCESS;
	}	   	
	
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	switch( effect )
	{
		case CamDigEffect_NoEffect:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_EFFECT_NONE);
			break;
		case CamDigEffect_MonoChrome:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_EFFECT_GRAY);
			break;
		case CamDigEffect_NegColor:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_EFFECT_NEGATIVE);
			break;
		case CamDigEffect_SepiaGreen:
		pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_EFFECT_SEPIA);
			break;
		default:
		printk(KERN_ERR"not supported effect \r\n");

			break;
	}

	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetDigitalEffect(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
#endif	
    Drv_Effect=effect;		
    return result;
}

//BYKIM_TUNING
//****************************************************************************
// Function Name:	HAL_CAM_Result_en_t CAMDRV_SetBrightness(CamBrightnessLevel_t brightness, CamSensorSelect_t sensor)
// Description:
// Notes:		This function can be for average user's use.
//****************************************************************************
static HAL_CAM_Result_en_t CAMDRV_SetBrightness(CamBrightnessLevel_t brightness, CamSensorSelect_t sensor)
{

	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;
#if 1 

	printk(KERN_ERR"CAMDRV_SetBrightness() called,brightness =  0x%08xDrv_Brightness=% 0x%08x\r\n",brightness,Drv_Brightness);

       if(((gv_ForceSetSensor==NORMAL_SET)&&(brightness==Drv_Brightness))||(gv_ForceSetSensor==FORCELY_SKIP))	
       {
		Drv_Brightness = brightness ;
		return HAL_CAM_SUCCESS;
	}	   	
	
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	switch( brightness )
	{
             case CamBrightnessLevel_0:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_BRIGHTNESS_0);
             break;
             
             case CamBrightnessLevel_1:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_BRIGHTNESS_1);
             break;
             
             case CamBrightnessLevel_2:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_BRIGHTNESS_2);
             break;
             
             case CamBrightnessLevel_3:
                  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_BRIGHTNESS_3);
             break;
             
             case CamBrightnessLevel_4:
                  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_BRIGHTNESS_4);
             break;
             
             case CamBrightnessLevel_5:
                  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_BRIGHTNESS_5);
             break;
             
             case CamBrightnessLevel_6:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_BRIGHTNESS_6);
             break;
             
             case CamBrightnessLevel_7:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_BRIGHTNESS_7);
             break;
             
             case CamBrightnessLevel_8:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_BRIGHTNESS_8);
             break;
			 
             default:
                 printk(KERN_ERR"not supported brightness \r\n");
             break;
        }

	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetBrightness(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
#endif	
    Drv_Brightness=brightness;
    return result;
}




//BYKIM_TUNING

/****************************************************************************
/ Function Name:   HAL_CAM_Result_en_t CAMDRV_SetMeteringType(CamMeteringType_t ae_mode)
/
/ Description: This function will set the metering exposure of camera
/ Notes:
****************************************************************************/

static HAL_CAM_Result_en_t CAMDRV_SetJpegsize( CamImageSize_t stills_resolution,CamSensorSelect_t sensor)
{
	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
        struct stCamacqSensorManager_t* pstSensorManager = NULL;
        struct stCamacqSensor_t* pstSensor = NULL;
		
       printk(KERN_ERR"CAMDRV_SetJpegsize() called,stills_resolution = 0x%08x Drv_Size= 0x%08x\r\n",stills_resolution,Drv_Size);
       if(stills_resolution==Drv_Size)	
       {
		printk(KERN_ERR"Do not set Jpegsize \r\n");
		Drv_Size = stills_resolution;
		return HAL_CAM_SUCCESS;
	}			
    #if 1 
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
    switch( stills_resolution )
    {
        case CamImageSize_QXGA:
		//pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SIZE_3M);
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_jpeg_3m,CAMACQ_SENSOR_MAIN );
            break;
        case CamImageSize_UXGA:
		//pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SIZE_2M);
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_jpeg_2m,CAMACQ_SENSOR_MAIN );
            break;
        case CamImageSize_4VGA:
		//pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SIZE_1M);
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_jpeg_1m,CAMACQ_SENSOR_MAIN );
            break;
        case CamImageSize_VGA:
		//pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SIZE_VGA);
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_jpeg_vga,CAMACQ_SENSOR_MAIN );		
            break;		
        case CamImageSize_QVGA:
		//pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SIZE_QVGA);
	       CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_jpeg_qvga,CAMACQ_SENSOR_MAIN );		
            break;			
			
        default:
		printk(KERN_ERR"not supported ae_mode \r\n");
               
            break;
    }

    if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetMeteringType(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}

    #endif
    Drv_Size = stills_resolution;	
    return result;
}



//****************************************************************************
// Function Name:   HAL_CAM_Result_en_t CAMDRV_SetSaturation(Int8 saturation)
// Description:  for Preview
// Notes:       This function is NOT for average user's use.
//****************************************************************************
static HAL_CAM_Result_en_t CAMDRV_SetSaturation(CamSaturation_t saturation, CamSensorSelect_t sensor)
{

	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;

	printk(KERN_ERR"CAMDRV_SetSaturation() called,saturation = %d \r\n",saturation);
	
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	switch( saturation )
	{
             case CamSaturation_m2:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_SATURATION_0);
             break;
             
             case CamSaturation_m1:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_SATURATION_1);
             break;
             
             case CamSaturation_0:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_SATURATION_2);
             break;
             
             case CamSaturation_p1:
                  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_SATURATION_3);
             break;
			 
             case CamSaturation_p2:
                  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_SATURATION_4);
             break;
			 
             default:
                 printk(KERN_ERR"not supported saturation \r\n");
             break;
        }

	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetSaturation(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
    return result;
}




//****************************************************************************
// Function Name:   HAL_CAM_Result_en_t CAMDRV_SetSharpness(Int8 sharpness)
//
// Description: This function sets the sharpness for Preview
//
// Notes:    11 levels supported: MIN_SHARPNESS(0) for no sharpening,
//           and MAX_SHARPNESS(11) for 200% sharpening
//          This function is NOT for average user's use.
//****************************************************************************
static HAL_CAM_Result_en_t CAMDRV_SetSharpness( CamSharpness_t  sharpness, CamSensorSelect_t sensor)
{

	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;

	printk(KERN_ERR"CAMDRV_SetSharpness() called,sharpness = %d \r\n",sharpness);
	
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	switch( sharpness )
	{
             case CamSharpness_m2:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_SHARPNESS_0);
             break;
             
             case CamSharpness_m1:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_SHARPNESS_1);
             break;
             
             case CamSharpness_0:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_SHARPNESS_2);
             break;
             
             case CamSharpness_p1:
                  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_SHARPNESS_3);
             break;
			 
             case CamSharpness_p2:
                  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_SHARPNESS_4);
             break;
			 
             default:
                 printk(KERN_ERR"not supported sharpness \r\n");
             break;
        }

	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetSharpness(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
    return result;
}
    


//****************************************************************************
// Function Name:   HAL_CAM_Result_en_t CAMDRV_SetContrast(Int8 contrast)
// Description:     Set contrast for preview image.
// Notes:       This function can be for average user's use.
//****************************************************************************
static HAL_CAM_Result_en_t CAMDRV_SetContrast(CamContrast_t contrast, CamSensorSelect_t sensor)
{

	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;

	printk(KERN_ERR"CAMDRV_SetContrast() called,contrast = %d \r\n",contrast);
	
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	switch( contrast )
	{
             case CamContrast_m2:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_CONTRAST_0);
             break;
             
             case CamContrast_m1:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_CONTRAST_1);
             break;
             
             case CamContrast_0:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_CONTRAST_2);
             break;
             
             case CamContrast_p1:
                  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_CONTRAST_3);
             break;
			 
             case CamContrast_p2:
                  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_CONTRAST_4);
             break;
			 
             default:
                 printk(KERN_ERR"not supported contrast \r\n");
             break;
        }

	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetContrast(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
    return result;
}


/* * Set the required Sensitivity (Image Quality Settings).  NOT for average user's use.

  @param iso        [in] the required Sensitivity.
  @return           HAL_CAM_Result_en_t 
*/
static HAL_CAM_Result_en_t CAMDRV_SetSensitivity( CamSensitivity_t iso, CamSensorSelect_t sensor)
{

	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;

	printk(KERN_ERR"CAMDRV_SetSensitivity() called,iso =  0x%08x Drv_Iso= 0x%08x \r\n",iso,Drv_Iso);

#if 0
       if(((gv_ForceSetSensor==NORMAL_SET)&&(iso==Drv_Iso))||(gv_ForceSetSensor==FORCELY_SKIP))	
       {
		Drv_Iso= iso;
		return HAL_CAM_SUCCESS;
	}	   	
			
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}

	switch( iso )
	{
             case CamSensitivity_Auto:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_ISO_AUTO);
             break;
             
             case CamSensitivity_100:
                 pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_ISO_100);
             break;
             
             case CamSensitivity_200:
                  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_ISO_200);
             break;
			 
             case CamSensitivity_400:
                  pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_ISO_400);
             break;
			 
             default:
                 printk(KERN_ERR"not supported contrast \r\n");
             break;
        }
	
	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetContrast(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
    Drv_Iso = 	iso;

#endif	
    return result;
}


static HAL_CAM_Result_en_t CAMDRV_SetDTPmode(unsigned int testmode,CamSensorSelect_t sensor)
{

	HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;

	printk(KERN_ERR"CAMDRV_SetDTPmode() called,testmode = %d \r\n",testmode);

       if(testmode==Drv_DTPmode)	
       {
		printk(KERN_ERR"Do not set DTPmode,Drv_DTPmode=%d  \r\n",Drv_DTPmode);
		return HAL_CAM_SUCCESS;
	}	   	
		
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}

	if(testmode==TRUE)
	{ 
	        msleep(800);
                CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_dtp_on,CAMACQ_SENSOR_MAIN);
               //pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_CONTRAST_0);
	       // msleep(800);
	}	
        else
        { 
                CamacqExtDirectlyWriteI2cLists(pstSensor->m_pI2cClient, reg_main_dtp_off,CAMACQ_SENSOR_MAIN );
                // pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor,  CAMACQ_SENSORDATA_CONTRAST_0);
	}			

	if (sCamI2cStatus != HAL_CAM_SUCCESS) {
		printk(KERN_INFO"CAMDRV_SetContrast(): Error[%d] \r\n", sCamI2cStatus);
		result = sCamI2cStatus;
	}
	
    Drv_DTPmode = testmode;
    return result;
}


static HAL_CAM_Result_en_t CAMDRV_Setmode(
        CamMode_t mode,
        CamSensorSelect_t sensor
         )

{

    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    struct stCamacqSensorManager_t* pstSensorManager = NULL;
    struct stCamacqSensor_t* pstSensor = NULL;
    
    printk(KERN_ERR"%s(): , mode : %d \r\n", __FUNCTION__, mode );

    if(mode==Drv_Mode)	
    {
	printk(KERN_ERR"Do not set mode, already set Drv_Mode=%d  \r\n",Drv_Mode);
	return HAL_CAM_SUCCESS;
     }	

    pstSensorManager = GetCamacqSensorManager();
    if( pstSensorManager == NULL )
    {
        printk(KERN_ERR"pstSensorManager is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }

    pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
    if( pstSensor == NULL )
    {
        printk(KERN_ERR"pstSensor is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }
    
   
     if(mode==CamVideo)
    {
	printk(KERN_ERR"CAMDRV_SetVideoCaptureMode(): Video Preview!!!!\r\n");
        pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_CAMCORDER);	
    }
/*	 
    else
    {
	printk(KERN_ERR"CAMDRV_SetVideoCaptureMode(): Camera Preview!\r\n");
        pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_PREVIEW);
    }	//To do
    */
    Drv_Mode  = mode;
    return result;
}


//BYKIM_ESD
static HAL_CAM_Result_en_t CAMDRV_GetESDValue( bool *esd_value,CamSensorSelect_t sensor)
{
      struct stCamacqSensorManager_t* pstSensorManager = NULL;
      struct stCamacqSensor_t* pstSensor = NULL;
      HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
      U16 Addr1=0x3406;
      U8 ReadValue = 0;
		
	//printk(KERN_ERR"CAMDRV_GetESDValue() called,= %d \r\n");
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
	       return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
	       return HAL_CAM_ERROR_OTHERS;
	}
	
	  *esd_value = FALSE;
	  
        CamacqExtReadI2c( pstSensor->m_pI2cClient, Addr1,2, &ReadValue,1);
	//  printk(KERN_ERR"CAMDRV_GetESDValue() ReadValue1 = %d \r\n",ReadValue);
	  if(ReadValue!=0xff)	
	  {
		printk(KERN_ERR"CAMDRV_GetESDValue  ERROR[%d]!!!!\r\n",ReadValue);
	       *esd_value = TRUE;
	       return result;
	  }	  
	return result;
}

//BYKIM_UNIFY
static HAL_CAM_Result_en_t CAMDRV_SetSensorParams( CAM_Parm_t parm,CamSensorSelect_t sensor)
{
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    struct stCamacqSensorManager_t* pstSensorManager = NULL;
    struct stCamacqSensor_t* pstSensor = NULL;
    
    printk(KERN_ERR"%s() \r\n", __FUNCTION__);
    pstSensorManager = GetCamacqSensorManager();
    if( pstSensorManager == NULL )
    {
        printk(KERN_ERR"pstSensorManager is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }

    pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
    if( pstSensor == NULL )
    {
        printk(KERN_ERR"pstSensor is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }
    result =CAMDRV_SetDTPmode(parm.testmode,sensor);
    result =CAMDRV_Setmode(parm.mode,sensor);
    result =CAMDRV_SetSceneMode(parm.scenemode,sensor);
    result =CAMDRV_SetWBMode(parm.wbmode,sensor);
    result =CAMDRV_SetBrightness(parm.brightness,sensor);
    result =CAMDRV_SetMeteringType(parm.aemode,sensor);
    result =CAMDRV_SetSensitivity(parm.iso,sensor);
    result =CAMDRV_SetDigitalEffect(parm.coloreffects,sensor);
    result =CAMDRV_SetJpegQuality(parm.quality,sensor);
    result =CAMDRV_SetJpegsize(parm.jpegSize,sensor);
    result =CAMDRV_SetZoom(parm.zoom,sensor);
    result =CAMDRV_SetFrameRate(parm.fps,sensor);
	
    if(gv_ForceSetSensor!=NORMAL_SET)
    {
         gv_ForceSetSensor=NORMAL_SET;
         printk(KERN_ERR"gv_ForceSetSensor is NORMAL_SET \r\n");	
    }
    
    return result;
}

static HAL_CAM_Result_en_t CAMDRV_GetSensorValuesForEXIF( CAM_Sensor_Values_For_Exif_t *exif_parm,CamSensorSelect_t sensor)
{
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    struct stCamacqSensorManager_t* pstSensorManager = NULL;
    struct stCamacqSensor_t* pstSensor = NULL;
    char aString[20];  //BYKIM_PREVENT
#if 0 	
    printk(KERN_ERR"%s() \r\n", __FUNCTION__);
    pstSensorManager = GetCamacqSensorManager();
    if( pstSensorManager == NULL )
    {
        printk(KERN_ERR"pstSensorManager is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }

    pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
    if( pstSensor == NULL )
    {
        printk(KERN_ERR"pstSensor is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }
#endif

    sprintf(aString, "1/%d", gv_checkEXPtime); 
    strcpy(exif_parm->exposureTime,aString);

    sprintf(aString,"%d,", gv_isoSpeedRatings); 
    strcpy(exif_parm->isoSpeedRating,aString);

    if(Drv_Scene==CamSceneMode_Auto)
    {
        switch(Drv_Brightness)
        {
            case CamBrightnessLevel_0:   // -2
                strcpy(aString, "-20/10");
                break;
            case CamBrightnessLevel_1:   // -1.5
                strcpy(aString, "-15/10");
                break;
            case CamBrightnessLevel_2:   // -1
                strcpy(aString, "-10/10");
                break;
            case CamBrightnessLevel_3:     // -0.5
                strcpy(aString, "-5/10");
                break;
            case CamBrightnessLevel_4:     // 0
                strcpy(aString, "0/10");
                break;
            case CamBrightnessLevel_5:     // 0.5
                strcpy(aString, "5/10");
                break;
            case CamBrightnessLevel_6:     // 1.0
                strcpy(aString, "10/10");
                break;
            case CamBrightnessLevel_7:     // 1.5
                strcpy(aString, "15/10");
                break;
            case CamBrightnessLevel_8:     // 2.0
                strcpy(aString, "20/10");
                break;            
        }    
    }
   else  if(Drv_Scene==CamSceneMode_Beach_Snow)
   {
        strcpy(aString, "10/10");
    }
   else
   {
        strcpy(aString, "0/10");
    }
    strcpy(exif_parm->exposureBias,aString);

    strcpy(exif_parm->FNumber,(char *)"26/10" );
    strcpy(exif_parm->maxLensAperture,(char *)"2757/1000" );
    strcpy(exif_parm->lensFocalLength,(char *)"279/100" );
    strcpy(exif_parm->exposureProgram,"3" );
    strcpy(exif_parm->colorSpaceInfo,"1");
	
    strcpy(exif_parm->aperture,"" );//NOT_USED 
    strcpy(exif_parm->brightness,"" );//NOT_USED 
    strcpy(exif_parm->softwareUsed,"");//NOT_USED 
    strcpy(exif_parm->shutterSpeed,"");//NOT_USED 
    strcpy(exif_parm->flash,"");//NOT_USED 
    strcpy(exif_parm->userComments,"");//NOT_USED 
    strcpy(exif_parm->contrast,"");//NOT_USED 
    strcpy(exif_parm->saturation,"");//NOT_USED 
    strcpy(exif_parm->sharpness,"");//NOT_USED 
	//NOT_USED 
    return result;
}

static void cam_InitCamIntfCfgI2C( CamIntfConfig_I2C_st_t *pI2cIntfConfig )  
{
    CamIntfCfgI2C.i2c_clock_speed = pI2cIntfConfig->i2c_clock_speed;    
    CamIntfCfgI2C.i2c_device_id   = pI2cIntfConfig->i2c_device_id;  
    CamIntfCfgI2C.i2c_access_mode = pI2cIntfConfig->i2c_access_mode;  
    CamIntfCfgI2C.i2c_sub_adr_op  = pI2cIntfConfig->i2c_sub_adr_op;  
    CamIntfCfgI2C.i2c_page_reg    = pI2cIntfConfig->i2c_page_reg;  
    CamIntfCfgI2C.i2c_max_page    = pI2cIntfConfig->i2c_max_page;  
} 


//****************************************************************************
//
// Function Name:   Result_t CAMDRV_SensorSetConfigTablePts(CAMDRV_SENSOR_SELECTION sensor)
//
// Description: Set pointers in config table to selected sensor.
//
// Notes:
//
//****************************************************************************
static HAL_CAM_Result_en_t CAMDRV_SensorSetConfigTablePts(CamSensorSelect_t sensor)
{
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;

    CamSensorCfg_st.sensor_config_caps = &CamPrimaryCfgCap_st;
    //CamSensorCfg_st.sensor_config_ccp_csi = &CamPrimaryCfg_CCP_CSI_st;
    CamSensorCfg_st.sensor_config_i2c = NULL;//&CamPrimaryCfg_I2C_st;
    ImageSettingsConfig_st.sensor_flashstate = &PrimaryFlashState_st;
    ImageSettingsConfig_st.sensor_framerate = &PrimaryFrameRate_st;
    ImageSettingsConfig_st.sensor_mirrormode = &PrimaryMirrorMode_st;

    ImageSettingsConfig_st.sensor_digitaleffect = &DigitalEffect_st;
    ImageSettingsConfig_st.sensor_wb = &WBmode_st;
    ImageSettingsConfig_st.sensor_metering = &Metering_st;//BYKIM_TUNING
    ImageSettingsConfig_st.sensor_jpegQuality = &JpegQuality_st;
    ImageSettingsConfig_st.sensor_brightness = &Brightness_st;
    ImageSettingsConfig_st.sensor_gamma = &Gamma_st;
    
    return result;
}



struct sens_methods sens_meth = {
    DRV_GetRegulator: CAMDRV_GetRegulator,
    DRV_TurnOnRegulator: CAMDRV_TurnOnRegulator,
    DRV_TurnOffRegulator: CAMDRV_TurnOffRegulator,
    DRV_GetIntfConfig: CAMDRV_GetIntfConfig,
    DRV_GetIntfSeqSel : CAMDRV_GetIntfSeqSel,
    DRV_Wakeup : CAMDRV_Wakeup,
    DRV_GetResolution : CAMDRV_GetResolution,
    DRV_SetVideoCaptureMode : CAMDRV_SetVideoCaptureMode,
    DRV_SetFrameRate : CAMDRV_SetFrameRate,
    DRV_EnableVideoCapture : CAMDRV_EnableVideoCapture,
    DRV_SetCamSleep : CAMDRV_SetCamSleep,
    DRV_GetJpegSize : CAMDRV_GetJpegSize,
    DRV_GetJpeg : CAMDRV_GetJpeg,
    DRV_GetThumbnail : CAMDRV_GetThumbnail,
    DRV_DisableCapture : CAMDRV_DisableCapture,
    DRV_DisablePreview : CAMDRV_DisablePreview,
    DRV_CfgStillnThumbCapture : CAMDRV_CfgStillnThumbCapture,
    DRV_StoreBaseAddress : CAMDRV_StoreBaseAddress,
    DRV_TurnOnAF : CAMDRV_TurnOnAF,
    DRV_TurnOffAF : CAMDRV_TurnOffAF,
    DRV_SetSensorParams : CAMDRV_SetSensorParams, 
    DRV_GetSensorValuesForEXIF : CAMDRV_GetSensorValuesForEXIF,
    DRV_GetESDValue : CAMDRV_GetESDValue
};

struct sens_methods *CAMDRV_primary_get(void)
{
	return &sens_meth;
}

#if 0 //BYKIM_CAMACQ

#define CAMACQ_MAIN_EXT_REG_GET_DATA(dest,srce,idx)	dest[0] = (srce[idx][0] & 0xFF); dest[1] = (srce[idx][1] & 0xFF);
#define CAMACQ_MAIN_EXT_REG_IS_BTM_OF_DATA(A)		(((A[0]==0xff) && (A[1]==0xff))? 1:0)
#define CAMACQ_MAIN_EXT_REG_IS_DELAY(A)				((A[0]==0xfe)? 1:0)

static HAL_CAM_Result_en_t CamacqExtWriteI2cLists( const void *pvArg, int iResType )
{
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS; 
    Int32 iNext = 0;
    UInt8	rgucWriteRegs[2] = {0, };
    UInt16	usDealy=0;
    UInt8 (*pvRegLists)[2] = (UInt8(*)[2])(pvArg);
  	
    {


//// init reglists valiable. ///////////////////////////////////////////////////////
        printk(KERN_INFO"CamacqExtWriteI2cLists \r\n");

        if( pvRegLists == NULL )
            return -1;

        // start!!
        rgucWriteRegs[0] = (pvRegLists[iNext][0] & 0xFF);
        rgucWriteRegs[1] = (pvRegLists[iNext][1] & 0xFF);
	
        while( !CAMACQ_MAIN_EXT_REG_IS_BTM_OF_DATA(rgucWriteRegs) )
        {
            if( CAMACQ_MAIN_EXT_REG_IS_DELAY(rgucWriteRegs) )
            {
                usDealy = (rgucWriteRegs[1])*10;
                printk(KERN_INFO"CamacqExtWriteI2cLists : Case of Delay:(%d) \r\n",usDealy);
                msleep(usDealy);  // Poll system status to confirm Idle state.
                result = HAL_CAM_SUCCESS;
            }
            else
            {
                {
                    //printk(KERN_INFO"CamacqExtWriteI2cLists : Case of Write:{0x%2x, 0x%2x} \r\n", rgucWriteRegs[0], rgucWriteRegs[1]);
                     result = CamacqExtWriteI2c( rgucWriteRegs, 2);
                } // else
            } // else
	                
            iNext++;
            CAMACQ_MAIN_EXT_REG_GET_DATA(rgucWriteRegs, pvRegLists, iNext)
        } // while
        printk(KERN_INFO"CamacqExtWriteI2cLists : END \r\n");
    }
    return result;
}
#endif



void  CAMDRV_CheckModeChange(void)
{
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;
	
        U16 INT_STS =0x00F8;
        U8 Status;
        U32 Count= 100, sensor= 0;//Rear Camera/*BYKIM_0623*/ 
	S32 RetVal=0;

	printk(KERN_ERR"CAMDRV_CheckModeChange() called = %d \r\n");

	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
       
	// check mode changed. 
        do{                      
                CamacqExtReadI2c( pstSensor->m_pI2cClient, INT_STS,2, &Status,1);	
                printk(KERN_ERR"Status[ 0x%02X] \r\n",Status);                                                                       
                Status = Status&0x02;
                if(Status==0x02)                                                                     
                {                       
                    printk(KERN_ERR"Status 1 is OK [Count = %d]\r\n",Count);     
                    break;   							                
                }
	        msleep(3);  // Poll system status to confirm Idle state.						
        }while(--Count);     

        do{                      
                        u8 INT_CLR0[3]={0x00,0xfc,0x02};  
                        CamacqExtWriteI2c( pstSensor->m_pI2cClient,INT_CLR0,3);
                        msleep(1);  // Poll system status to confirm Idle state.
                        CamacqExtReadI2c( pstSensor->m_pI2cClient, INT_STS,2, &Status,1);	
                        Status = Status&0x02;
                        if(Status==0x00)                                                                     
                        {
                            printk(KERN_ERR"MODE CHANGE  is OK [Count = %d]\r\n",Count);     
                            break;   							                
                        }
	        msleep(3);  // Poll system status to confirm Idle state.						
        }while(--Count);     

          printk(KERN_ERR"Count[%d]\r\n",Count);
}

bool  CAMDRV_CheckJpegUpdate(void)
{
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;
	
        U16 INT_STS =0x00F8;
        U8 Status;
        U32 Count=200, sensor= 0;//Rear Camera/*BYKIM_0623*/ 
        bool Retval = TRUE;

	printk(KERN_ERR"CAMDRV_CheckJpegUpdate() called  \r\n");

	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	// check jpeg states. 

        do{          
                        CamacqExtReadI2c( pstSensor->m_pI2cClient, INT_STS,2, &Status,1);	
                        Status = Status&0x04;						
                        if(Status==0x04)    
                        {                       
                    printk(KERN_ERR"Status 2 is OK [Count = %d]\r\n",Count);
                    break;
                }
	         msleep(3);  // Poll system status to confirm Idle state.
	}while(--Count);      				
						
    do{          
                                u8 INT_CLR0[3]={0x00,0xfc,0x04};  
                                CamacqExtWriteI2c( pstSensor->m_pI2cClient,INT_CLR0,3);
                                msleep(1);  // Poll system status to confirm Idle state.
                                CamacqExtReadI2c( pstSensor->m_pI2cClient, INT_STS,2, &Status,1);	
                                Status = Status&0x04;						
                                if(Status==0x00)                                                                     
                                {
                                    printk(KERN_ERR"JPEG_UPDATE  is OK [Count = %d]\r\n",Count);     
		    Retval = TRUE;			
                                    break;   							                
                                }
	}while(--Count);                
  	    printk(KERN_ERR"Count[%d]\r\n",Count);
		
	return Retval;	
                            }
                            
void  CAMDRV_CheckJpegStatus(void)
{
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;
	
        U16 JPEG_State =0x0200;		
	U16 PICT_INFO = 0x0004;	
        U8 Status;
        U32 Count=10, sensor= 0;//Rear Camera/*BYKIM_0623*/ 
	S32 RetVal=0;

	printk(KERN_ERR"CAMDRV_CheckJpegStatus() called\r\n");

	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	// check jpeg states. 

        CamacqExtReadI2c( pstSensor->m_pI2cClient, PICT_INFO,2, &Status,1);	
        Status = Status&0x0E;
        if(Status==0x0E)                                                                     
        {
             printk(KERN_ERR"PICT_INFO[ 0x%02X] \r\n",Status);      
        }

    do{          
             if(CAMDRV_CheckJpegUpdate())
             {
                 msleep(1);  // Poll system status to confirm Idle state.	   
                            CamacqExtReadI2c( pstSensor->m_pI2cClient, JPEG_State,2, &Status,1);	
                  //printk(KERN_ERR"Status[ 0x%02X] \r\n",Status);
                            if(Status==0x00)                                                                     
                            {                       
                                printk(KERN_ERR"JPEG is OK \r\n");
                                break;   							                
                            }  
                            else
                            {                       
                                printk(KERN_ERR"JPEG is NOT OK \r\n");
                            }  
                        }
                }while(--Count);                 
  	        printk(KERN_ERR"Count[%d]\r\n",Count);
#if 1 
            CAMDRV_GetShutterSpeed(sensor);
            CAMDRV_GetISOSpeed(sensor);
#endif	

}


void  CAMDRV_Calibration(void)
{
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;
	
        U8 Status;
        U32 sensor= 0;//Rear Camera
	S32 RetVal=0;

	//u8 OTP_0[2]= {0x02,0x2C}; //OTP 0
        //u8 OTP_1[2]= { 0x02,0x38};//OTP 1
        U16 OTP_0 = 0x022C;
	U16 OTP_1 = 0x0238;
        
        U8 OTP1=0x00,OTP0=0x00;
        U8 OTP_READ[4]={0,};
        u8 OTP_TABLE[12]={0,};
        U16 num;
        U8 OTP_UPDATEED=FALSE;

     //   U8 OTP0_TEMP[4]={0x00,0x00,0x00,0x00}, OTP1_TEMP[4]={0x00,0x00,0x00,0x00}; //OTPX0, OTPX1, OTPX2

	printk(KERN_ERR"CAMDRV_Calibration() called \r\n");
#if 1

	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
}

	printk(KERN_ERR"STEP 1 : GET OTP TABLE \r\n");


	 CamacqExtReadI2c( pstSensor->m_pI2cClient, 0x022C,2, &OTP0,1);		  
	 CamacqExtReadI2c( pstSensor->m_pI2cClient, 0x0238,2, &OTP1,1);

 	 printk(KERN_ERR"OTP0= [0x%02x] \r\n",OTP0);
 	 printk(KERN_ERR"OTP1= [0x%02x] \r\n",OTP1);
#if 0 
          CamacqExtReadI2c( pstSensor->m_pI2cClient, 0x022C,2, OTP0_TEMP,4);	
          CamacqExtReadI2c( pstSensor->m_pI2cClient, 0x0238,2, OTP1_TEMP,4);
          printk(KERN_ERR"OTP_READ= [0x%02x,0x%02x,0x%02x,0x%02x] \r\n", OTP0_TEMP[0], OTP0_TEMP[1], OTP0_TEMP[2], OTP0_TEMP[3]);           
          printk(KERN_ERR"OTP_READ= [0x%02x,0x%02x,0x%02x,0x%02x] \r\n", OTP1_TEMP[0], OTP1_TEMP[1], OTP1_TEMP[2], OTP1_TEMP[3]);           
#endif
		
         OTP0 =  OTP0&(0x10);
         OTP1 =  OTP1&(0x10);
		
	
         if(OTP1==0x10)
              {// CASE1 : READ OPT1 DATA
	           printk(KERN_ERR"CASE1 : READ OPT1 DATA \r\n");
                   //camsensor_isx005_i2c_read_data(0x0238, &OTP10, 4);
              //    OTP0 = OTP10;
           
                   for (num=0; num<3; num++) 
                   {
                       memset(OTP_READ, 0x00, sizeof(OTP_READ));
                       CamacqExtReadI2c( pstSensor->m_pI2cClient, OTP_1,2, &OTP_READ,4);                               
                       printk(KERN_ERR"OTP_READ= [0x%02x,0x%02x,0x%02x,0x%02x] \r\n", OTP_READ[0], OTP_READ[1], OTP_READ[2], OTP_READ[3]);           
                       memcpy(&(OTP_TABLE[num*sizeof(OTP_READ)]), OTP_READ, sizeof(OTP_READ));
                            OTP_1= OTP_1+sizeof(OTP_READ);
 	                   printk(KERN_ERR"OTP_1= [0x%02x] \r\n",OTP_1);
                   }	
           
                   for(num=0; num<10; num++)
                   {
                 	printk(KERN_ERR"OTP_TABLE0[%d]={0x%02X}", num, OTP_TABLE[num]);
                   }	
		  OTP_UPDATEED = TRUE;		   
        
              }
          else if(OTP0==0x10)
              {// CASE2 : READ OPT0 DATA 
                 // camsensor_isx005_i2c_read_data(0x022C &OTP00, 4);
                 
	           printk(KERN_ERR"CASE2 : READ OPT0 DATA \r\n");
         
                  for (num=0; num<3; num++) 
                     {
                         memset(OTP_READ, 0x00, sizeof(OTP_READ));
                         CamacqExtReadI2c( pstSensor->m_pI2cClient, OTP_0,2, &OTP_READ,4);                               
                         printk(KERN_ERR"OTP_READ= [0x%02x,0x%02x,0x%02x,0x%02x] \r\n", OTP_READ[0], OTP_READ[1], OTP_READ[2], OTP_READ[3]);           
       
                         memcpy(&(OTP_TABLE[num*sizeof(OTP_READ)]), OTP_READ, sizeof(OTP_READ));
                         
                         OTP_0= OTP_0+sizeof(OTP_READ);
 	                  printk(KERN_ERR"OTP0= [0x%02x] \r\n",OTP0);
                     }	
         
                 for(num=0; num<10; num++)
                     {
                 	printk(KERN_ERR"OTP_TABLE0[%d]={0x%02X}", num, OTP_TABLE[num]);
                     }      
				 
   	      OTP_UPDATEED = TRUE;		   
              }
              else  // if((((OTP10 & 0x10) >> 4) == 0) && (((OTP00 & 0x10) >> 4) == 0 ) )
              {// CASE3 : Default cal. 
	           printk(KERN_ERR" CASE3 : Default cal  \r\n");
	          // CamacqExtWriteI2cLists(pstSensor->m_pI2cClient, isx005_default_calibration,0 );
                   pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_DEFAULT_CAL);
                  // Module was not calibrated in module vendor.
          }
          

    if (OTP_UPDATEED==TRUE)
    { //CASE 1 || CASE 2:
        U8 shading_index;
        int i;

	printk(KERN_ERR"STEP 2 : SHADING CALIBRATION\r\n");

        shading_index =OTP_TABLE[1]&(0x60);
        shading_index= shading_index>>5;
		
	 printk(KERN_ERR" shading_index=%d  \r\n",shading_index);
	 
        if (shading_index ==0) // 00
        {
            pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SHADING_00);
	    // CamacqExtWriteI2cLists(pstSensor->m_pI2cClient, reg_shading2,0 );
        }
        else if (shading_index==1) // 01
        {
              pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SHADING_01);
	     //CamacqExtWriteI2cLists(pstSensor->m_pI2cClient, reg_shading3,0 );
        }
        else if (shading_index== 2) // 10
        {
              pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SHADING_10);
	     //CamacqExtWriteI2cLists(pstSensor->m_pI2cClient, reg_shading3,0 );
        }
        else if (shading_index==3) // 11
        {
              pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_SHADING_11);
	     //CamacqExtWriteI2cLists(pstSensor->m_pI2cClient, reg_shading3,0 );
        }
        else
        {
	        // CamacqExtWriteI2cLists(pstSensor->m_pI2cClient, isx005_default_calibration,0 );
                pstSensor->m_pstAPIs->WriteDirectSensorData( pstSensor, CAMACQ_SENSORDATA_DEFAULT_CAL);
        }
    }



    if (OTP_UPDATEED==TRUE)
    {

        U16 NORMR, NORMB; //14bit
        U16 AWBPRER, AWBPREB; //10bit
        u16  READ1, READ2,READ3;
	U8 AWB_OFFSET[4] = {0x4A,0x04,0x00,0x00};	
         printk(KERN_ERR"STEP 3: AWB CALIBRATION \r\n");
		
        //NORMR = ((OTP1 & 0x3F) << 8) | ((OTP0 & 0xFF000000) >> 24);
        READ1 = OTP_TABLE[3];
	READ2 = ((OTP_TABLE[4]&(0x3f))<<8);
        NORMR = READ1+READ2;
	printk(KERN_ERR" NORMR=0x%04x  \r\n",NORMR);
        if(NORMR <= 0x3fff)
        {
            AWB_OFFSET[2] = (U8)(NORMR&(0x00ff));
            AWB_OFFSET[3] = (U8)((NORMR&(0xff00))>>8);
	    printk(KERN_ERR"  AWB_OFFSET[1]=0x%02x AWB_OFFSET[2]=0x%02x ,AWB_OFFSET[3]=0x%02x  \r\n",AWB_OFFSET[1],AWB_OFFSET[2],AWB_OFFSET[3]);
			
            CamacqExtWriteI2c( pstSensor->m_pI2cClient,AWB_OFFSET,4);			
        }	
        
       // NORMB =  ((OTP1 & 0xFFFC0) >> 6);
        READ1 = ((OTP_TABLE[4]&0xc0)>>6);
	READ2 = (OTP_TABLE[5]<<2);
        READ3 = ((OTP_TABLE[6]&0x0f)<<10);
	   
        NORMB = READ1+READ2+READ3;      
        printk(KERN_ERR" NORMB=0x%04x  \r\n",NORMB);
        if(NORMB <= 0x3FFF)
        {
             AWB_OFFSET[1] = 0x06;
             AWB_OFFSET[2] = (U8)(NORMB&(0x00ff));
             AWB_OFFSET[3] = (U8)((NORMB&(0xff00))>>8);
	    printk(KERN_ERR"  AWB_OFFSET[1]=0x%02x AWB_OFFSET[2]=0x%02x ,AWB_OFFSET[3]=0x%02x  \r\n",AWB_OFFSET[1],AWB_OFFSET[2],AWB_OFFSET[3]);

            CamacqExtWriteI2c( pstSensor->m_pI2cClient,AWB_OFFSET,4);			
        }

       // AWBPRER = ((OTP1 & 0x3FF00000) >> 20);

        READ1 = ((OTP_TABLE[6]&0xf0)>>4);
	READ2 = ((OTP_TABLE[7]&0x3f)<<4);
	   
        AWBPRER = READ1+READ2;      
        printk(KERN_ERR" AWBPRER=0x%04x  \r\n",AWBPRER);
        if(AWBPRER <= 0x3FFF)
    	{
             AWB_OFFSET[1] = 0x08;
             AWB_OFFSET[2] = (U8)(AWBPRER&(0x00ff));
             AWB_OFFSET[3] = (U8)((AWBPRER&(0xff00))>>8);
	    printk(KERN_ERR"  AWB_OFFSET[1]=0x%02x AWB_OFFSET[2]=0x%02x ,AWB_OFFSET[3]=0x%02x  \r\n",AWB_OFFSET[1],AWB_OFFSET[2],AWB_OFFSET[3]);

            CamacqExtWriteI2c( pstSensor->m_pI2cClient,AWB_OFFSET,4);			
    	}    
    
       // AWBPREB = ((OTP2 & 0xFF) << 2) | ((OTP1 & 0xC0000000) >> 30);

        READ1 = ((OTP_TABLE[7]&0xc0)>>6);
	READ2 = (OTP_TABLE[8]<<2);
	   
        AWBPREB = READ1+READ2;      
        printk(KERN_ERR" AWBPREB=0x%04x  \r\n",AWBPREB);
        if(AWBPREB <= 0x3FFF)
        {
             AWB_OFFSET[1] = 0x0A;
             AWB_OFFSET[2] = (U8)(AWBPREB&(0x00ff));
             AWB_OFFSET[3] = (U8)((AWBPREB&(0xff00))>>8);
	    printk(KERN_ERR"  AWB_OFFSET[1]=0x%02x AWB_OFFSET[2]=0x%02x ,AWB_OFFSET[3]=0x%02x \r\n",AWB_OFFSET[1],AWB_OFFSET[2],AWB_OFFSET[3]);
        
            CamacqExtWriteI2c( pstSensor->m_pI2cClient,AWB_OFFSET,4);			
        }
        }

#endif
        printk(KERN_ERR" CAMDRV_Calibration() end  \r\n");   
}

#if 0 
BOOL CAMDRV_set_default_calibration(void)
{
    int i;
    
    for(i = 0; i < index_reg_default_calibration; i++)
    {
        if (camsensor_isx005_set_data(reg_default_calibration[i] ) == FALSE)
        {
            return FALSE;
        }   
    }
    
    return TRUE;
}
#endif

int CAMDRV_DecodeInterleaveData(unsigned char *pInterleaveData, 	// (IN) Pointer of Interleave Data
						 int interleaveDataSize, 			// (IN) Data Size of Interleave Data
						 int yuvWidth, 						// (IN) Width of YUV Thumbnail
						 int yuvHeight, 					// (IN) Height of YUV Thumbnail
						 unsigned char *pJpegData,			// (OUT) Pointer of Buffer for Receiving JPEG Data 
						 int *pJpegSize,  					// (OUT) Pointer of JPEG Data Size
						 unsigned char *pYuvData)			// (OUT) Pointer of Buffer for Receiving YUV Data 

{
	int ret;
	unsigned long *interleave_ptr;
	unsigned char *jpeg_ptr;
	unsigned char *yuv_ptr;
	unsigned char *p;
	int jpeg_size;
	int yuv_size;
	int i;
         printk(KERN_ERR" CAMDRV_DecodeInterleaveData called[0x%04X]  \r\n",pInterleaveData);   
	if (pInterleaveData == NULL) {
		return FALSE;
	}

	ret = TRUE;
	interleave_ptr = (unsigned long*)pInterleaveData;
	jpeg_ptr = pJpegData;
	yuv_ptr = pYuvData;
	jpeg_size = 0;
	yuv_size = 0;

	if (pJpegSize != NULL) //BYKIM_PREVENT
	{
    *pJpegSize = 0;
	}

	i = 0;

    if(*interleave_ptr!=0xDBFFD8FF)
    {
            printk(KERN_ERR" Not Jpeg files[0x%08X] \r\n", *interleave_ptr);   
            return FALSE;  //Not Jpeg files
    }	
	while(i < interleaveDataSize) {
		if ((*interleave_ptr == 0xFFFFFFFF) || (*interleave_ptr == 0x02FFFFFF) || (*interleave_ptr == 0xFF02FFFF))
		{ // Need to modify here if Big-Endian system.
			// Padding Data
			interleave_ptr++;
			i += 4;

		} 
		else if ((*interleave_ptr & 0xFFFF) == 0x05FF)
		{		
			// Need to modify here if Big-Endian system.
			// Start-code of YUV Data
			p = (unsigned char*)interleave_ptr;
			p += 2;
			i += 2;

			// Extract YUV Data
			if (pYuvData != NULL) 
			{ 
				if(copy_to_user(yuv_ptr, p, yuvWidth * 2) != 0)  //BYKIM_PREVENT
                {
                                   printk(KERN_ERR"Extract YUV Data Fail!!\r\n");   
                                   ret = FALSE;
                }	
				yuv_ptr += yuvWidth * 2;
				yuv_size += yuvWidth * 2;
			}
			
			p += yuvWidth * 2;
			i += yuvWidth * 2;

			// Check End-code of YUV Data
			if ((*p == 0xFF) && (*(p + 1) == 0x06)) 
			{
				interleave_ptr = (unsigned long*)(p + 2);
				i += 2;
			} else 
                       {
                                printk(KERN_ERR"1. ret = FALSE; \r\n");   
				ret = FALSE;
				break;
			}

		} 
		else
		{
			// Extract JPEG Data
			if (pJpegData != NULL) 
			{ 
				if(copy_to_user(jpeg_ptr, interleave_ptr, 4)!=0)  //BYKIM_PREVENT
  			    {
                    printk(KERN_ERR"Extract JPEG Data Fail!!\r\n");   
				    ret = FALSE;
  			    }	
				jpeg_ptr += 4;
				jpeg_size += 4;
			}
			interleave_ptr++;
			i += 4;
		}
	}
	if (ret) 
	{
                printk(KERN_ERR"2. ret = TRUE pJpegData = 0x%x \r\n",pJpegData);   
		if (pJpegData != NULL) 
		{ 
#if 0 //BYKIM_0630
			// Remove Padding after EOI
			for (i=0; i<3; i++) 
			{
				if (*(--jpeg_ptr) != 0xFF) 
				{
					break;
				}
				jpeg_size--;
			}
#else
			for (i=0; i<200000; i++) 
			{
				if (*(jpeg_ptr-1)==0xFF && *(jpeg_ptr)==0xD9) 
				{
					jpeg_size++;
					break;
				}
				jpeg_ptr--;
				jpeg_size--;
			}
#endif
                printk(KERN_ERR"[Albert] i=%d, jpeg_size = %d \r\n", i, jpeg_size);   
			if (pJpegSize != NULL) 
			{
				*pJpegSize = jpeg_size;
			}
			else
			{
       			     printk(KERN_ERR"pJpegSize == NULL \r\n");   
			}
		}
		
		// Check YUV Data Size
		if (pYuvData != NULL)
		{ 
			if (yuv_size != (yuvWidth * yuvHeight * 2)) {
 				 printk(KERN_ERR"2. ret = FALSE; \r\n");   				
				ret = FALSE;
			}
		}
		else
		{
                   printk(KERN_ERR"pYuvData = 0x%x \r\n",pYuvData);   
		}

	}
	return ret;
}

void  CAMDRV_GetCurrentEIT(CamSensorSelect_t sensor, UInt8 *vp_currentEIT)
{
        HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;
	
	U16 OUTDOOR_F= 0x6c21;
	U8 OUTDOOR_State;

	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
	       return HAL_CAM_ERROR_OTHERS;  //BYKIM_PREVENT
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	//STEP 1 : Check Outdoor Condition and Write outdoor table
	 CamacqExtReadI2c( pstSensor->m_pI2cClient, OUTDOOR_F,2, &OUTDOOR_State,1);		  
         printk(KERN_ERR"OUTDOOR_State[ 0x%02X] \r\n",OUTDOOR_State);

	if(OUTDOOR_State==0x01)		
	{
		*vp_currentEIT = TRUE;
	}   
	else
	{
		*vp_currentEIT = FALSE;
	}
         printk(KERN_ERR"CAMDRV_GetCurrentEIT[%d] \r\n",*vp_currentEIT);
}

void  CAMDRV_GetCurrentLux(CamSensorSelect_t sensor,UInt8 *vp_currentLux)
{
        HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
	struct stCamacqSensorManager_t* pstSensorManager = NULL;
	struct stCamacqSensor_t* pstSensor = NULL;
	
	U16 AGC_SCL_NOW=0x027A;
	U8 ReadData[2]={0,0};
        static U16 AGC_SCL_NOW_State;
	pstSensorManager = GetCamacqSensorManager();
	if( pstSensorManager == NULL )
	{
		printk(KERN_ERR"pstSensorManager is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;//BYKIM_PREVENT
	}
	
	pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
	if( pstSensor == NULL )
	{
		printk(KERN_ERR"pstSensor is NULL \r\n");
		return HAL_CAM_ERROR_OTHERS;
	}
	
	//STEP 1 : Check Outdoor Condition and Write outdoor table
	 CamacqExtReadI2c( pstSensor->m_pI2cClient, AGC_SCL_NOW,2, ReadData,2);	
	 AGC_SCL_NOW_State = (U16)ReadData[1]*0x100+ (U16)ReadData[0];
	
         printk(KERN_ERR"AGC_SCL_NOW_ReadData1[ 0x%02X,0x%02X]->0x%04X \r\n",ReadData[0],ReadData[1],AGC_SCL_NOW_State);

	if(AGC_SCL_NOW_State>=0x0A20)		
	{
		*vp_currentLux = TRUE;
	}
	else
	{
		*vp_currentLux = FALSE;
	}
         printk(KERN_ERR"CAMDRV_GetCurrentLux[%d] \r\n",*vp_currentLux);
}

U16 ExifChangeIsoValueToSecStandard(U8 IsoValue)
{
    printk(KERN_INFO"ExifChangeIsoValueToSecStandard[ %d]\r\n",IsoValue);

    if(IsoValue<=1)
        return 25;
    else if(IsoValue>=19)	
        return 1600;
    else
    {
        switch(IsoValue)
        {
            case 2:
                  return 32;
                break;            
            case 3:    
                  return 40;
                break;            
            case 4:
                  return 50;
                break;            
            case 5:        
                  return 64;
                break;            
            case 6:
                  return 80;
                break;            
            case 7:   
                  return 100;
                break;            
            case 8:   
                  return 125;
                break;            
            case 9:  
                  return 160;
                break;            
            case 10:
                  return 200;
                break;            
            case 11:    
                  return 250;
                break;            
            case 12:
                  return 320;
                break;            
            case 13:        
                  return 400;
                break;            
            case 14:
                  return 500;
                break;            
            case 15:   
                  return 640;
                break;            
            case 16:   
                  return 800;
                break;            
            case 17:  
                  return 1000;
                break;            
            case 18:  
                  return 1250;
                break;            
        }
    }
}

void  CAMDRV_GetShutterSpeed(CamSensorSelect_t sensor) //BYKIM_EXIF
{
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    struct stCamacqSensorManager_t* pstSensorManager = NULL;
    struct stCamacqSensor_t* pstSensor = NULL;
    
    U16 SHT_TIME_OUT= 0x00f2;
    U8 ReadData[4]={0,0,0,0};
    U16 SHT_TIME_OUT_H,SHT_TIME_OUT_L;
    U32 Exptime;
    
    pstSensorManager = GetCamacqSensorManager();
    if( pstSensorManager == NULL )
    {
        printk(KERN_ERR"pstSensorManager is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }
    
    pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
    if( pstSensor == NULL )
    {
        printk(KERN_ERR"pstSensor is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }
    
    //STEP 1 : Check Outdoor Condition and Write outdoor table
    CamacqExtReadI2c( pstSensor->m_pI2cClient, SHT_TIME_OUT,2,ReadData,4);		  
    printk(KERN_ERR"Readdata[ 0x%02X],[ 0x%02X],[ 0x%02X],[ 0x%02X]\r\n",ReadData[1],ReadData[0],ReadData[3],ReadData[2]);
    
    SHT_TIME_OUT_L = (U16)ReadData[1]*0x100+ (U16)ReadData[0];
    SHT_TIME_OUT_H = (U16)ReadData[3]*0x100+ (U16)ReadData[2];
    Exptime = (U32)SHT_TIME_OUT_H*0x10000  + (U32)SHT_TIME_OUT_L;
    printk(KERN_ERR"Exptime : %d ",Exptime);
   

    if(Drv_Scene==CamSceneMode_Night)
    {
	if(Exptime>=500000)
		Exptime=500000;
    }
    else if(Drv_Scene==CamSceneMode_Firework)
    {
	if(Exptime>=1000000)
		Exptime=1000000;
    }

    gv_checkEXPtime = 1000000/Exptime;

	
    if(gv_checkEXPtime<=1)
    {
		gv_checkEXPtime=1;
    }
    printk(KERN_ERR"gv_checkEXPtime : %d ",gv_checkEXPtime);

}

void CAMDRV_GetISOSpeed(CamSensorSelect_t sensor)
{
    HAL_CAM_Result_en_t result = HAL_CAM_SUCCESS;
    struct stCamacqSensorManager_t* pstSensorManager = NULL;
    struct stCamacqSensor_t* pstSensor = NULL;
    
    U16 ISOSENS_OUT= 0x00f0;

    U8 ReadData;
    
    pstSensorManager = GetCamacqSensorManager();
    if( pstSensorManager == NULL )
    {
        printk(KERN_ERR"pstSensorManager is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;//BYKIM_PREVENT
    }
    
    pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
    if( pstSensor == NULL )
    {
        printk(KERN_ERR"pstSensor is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }
    
    //STEP 1 : GetISO
    CamacqExtReadI2c( pstSensor->m_pI2cClient, ISOSENS_OUT,2,&ReadData,1);		  
    printk(KERN_ERR"Readdata[ %d]\r\n",ReadData);

        gv_isoSpeedRatings = ExifChangeIsoValueToSecStandard(ReadData);

/*
    if(Drv_Scene==CamSceneMode_Party_Indoor)
    {
	gv_isoSpeedRatings = 200; 	
    }
    else if((Drv_Scene==CamSceneMode_Beach_Snow)||(Drv_Scene==CamSceneMode_Firework))
    {
	gv_isoSpeedRatings = 50; 	
    }
*/	
   printk(KERN_ERR"gv_isoSpeedRatings[ %d]\r\n",gv_isoSpeedRatings);
//    return gv_isoSpeedRatings;
}


void  CAMDRV_ResetMode(void)
{
    u8 MONITOR_MODE[3]={0x00,0x11,0x00}; 
    u8 REFRESH_MODE[3]={0x00,0x12,0x01}; 
    U16 INT_STS =0x00F8;
    U8 Status;
    U32 Count= 10, sensor= 0;//Rear Camera/*BYKIM_0623*/ 
	
    struct stCamacqSensorManager_t* pstSensorManager = NULL;
    struct stCamacqSensor_t* pstSensor = NULL;

    
    pstSensorManager = GetCamacqSensorManager();
    if( pstSensorManager == NULL )
    {
        printk(KERN_ERR"pstSensorManager is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;//BYKIM_PREVENT
    }
    
    pstSensor = pstSensorManager->GetSensor( pstSensorManager, sensor );
    if( pstSensor == NULL )
    {
        printk(KERN_ERR"pstSensor is NULL \r\n");
        return HAL_CAM_ERROR_OTHERS;
    }
    CamacqExtWriteI2c( pstSensor->m_pI2cClient,MONITOR_MODE,3);
    CamacqExtWriteI2c( pstSensor->m_pI2cClient,REFRESH_MODE,3);

    do{                      
            CamacqExtReadI2c( pstSensor->m_pI2cClient, INT_STS,2, &Status,1);	
            Status = Status&0x02;
            if(Status==0x02)                                                                     
            {                       
                printk(KERN_ERR"CAMDRV_ResetMode is OK [Count = %d]\r\n",Count);     
                break;   							                
            }
     msleep(2);  // Poll system status to confirm Idle state.						
    }while(--Count);  
}

