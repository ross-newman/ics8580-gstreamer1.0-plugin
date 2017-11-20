/*
 * � GE Intelligent Platforms Embedded Systems Inc (2014)
 *
 * All rights reserved. No part of this software may be re-produced, re-engineered, 
 * re-compiled, modified, used to create derivatives, stored in a retrieval system, 
 * or transmitted in any form or by any means, electronic, mechanical, photocopying, 
 * recording, or otherwise without the prior written permission of GE Intelligent 
 * Platforms Embedded Systems Inc.
 */
#include "ics8580FunctionalApi.h"

#include <gst/gst.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/types.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/poll.h>

#include "gst8580capture.h"

//GST_DEBUG_CATEGORY_STATIC (gst_8580capture_debug);
//#define GST_CAT_DEFAULT gst_8580capture_debug
//#define GST_LEVEL_DEBUG            8

#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wmissing-braces"

#ifdef WIN32
#define snprintf _snprintf
#endif
typedef unsigned long DWORD;
/* Globals definitions. Redefine them to fit a specific setup. */

#define NUMBER_OF_BUFFERS          2               /* Number of buffers allocated for DMA (2 for double buffering). */
#define DEBUG                      1               /* enable TRACE */
#define ENABLE_CONFIG              1
#define OUTPUT_ENABLED             1               /* Enable Input */
#define PCI_NAME                   "PCIe://0"
#define INPUT_CHANNEL              0			   /* Input channel for video */
#define OUTPUT_CHANNEL             1               /* Output channel for video */
#define TIMESTAMP                  0
#define                            DMA_CHANNEL                (OUTPUT_CHANNEL-1)

/* Some additional error codes */
#define ICS8580_INPUT_ERROR       -100
#define ICS8580_OUTPUT_ERROR      -200
#define ICS8580_STREAM_ERROR      -300

#if TIMESTAMP
#include <time.h>
clock_t start;  
clock_t finish; 
#endif

#if DEBUG
#if 0
#define TRACE(format, ...)  g_print(format,__VA_ARGS__) 
#else
#define TRACE GST_LOG
#endif
#define TRACE_ERROR GST_ERROR
#else
#define TRACE(format, ...) 
#define TRACE_ERROR GST_ERROR
#endif

#define HRESULT int
struct _RESOLUTION_TABLE { /* Defines a lookup table with video resolution options */
	int resolution;        /* ICS8580_VIDEO_RESOLUTION values from ics8580FunctionalApi.h header */
	int width;             /* Video data width for the selected resolution */
	int height;            /* Video data height for the selected resolution */
	int framerate;         /* Video data frame rate for the selected resolution */
} ResolutionTable[] =
{
    ICS8580_VIDEO_RESOLUTION_NONE,           0,    0,   0,
    ICS8580_VIDEO_RESOLUTION_NTSC,         720,  487,  30,
    ICS8580_VIDEO_RESOLUTION_PAL,          720,  576,  25,
    ICS8580_VIDEO_RESOLUTION_525P_60,      720,  480,  60,
    ICS8580_VIDEO_RESOLUTION_625P_50,      720,  576,  50,
    ICS8580_VIDEO_RESOLUTION_720P_50,      1280, 720,  50,
    ICS8580_VIDEO_RESOLUTION_720P_60,      1280, 720,  60,
    ICS8580_VIDEO_RESOLUTION_1080I_50,     1920, 1080, 25,
    ICS8580_VIDEO_RESOLUTION_1080I_60,     1920, 1080, 30,
    ICS8580_VIDEO_RESOLUTION_1080P_24,     1920, 1080, 24,
    ICS8580_VIDEO_RESOLUTION_1080P_25,     1920, 1080, 25,
    ICS8580_VIDEO_RESOLUTION_1080P_30,     1920, 1080, 30,
    ICS8580_VIDEO_RESOLUTION_1080P_60,     1920, 1080, 60,
    ICS8580_VIDEO_RESOLUTION_VGA_60,       640,  480,  60,
    ICS8580_VIDEO_RESOLUTION_VGA_72,       640,  480,  72,
    ICS8580_VIDEO_RESOLUTION_VGA_75,       640,  480,  75,
    ICS8580_VIDEO_RESOLUTION_VGA_85,       640,  480,  85,
    ICS8580_VIDEO_RESOLUTION_SVGA_60,      800,  600,  60,
    ICS8580_VIDEO_RESOLUTION_SVGA_72,      800,  600,  72,
    ICS8580_VIDEO_RESOLUTION_SVGA_75,      800,  600,  75,
    ICS8580_VIDEO_RESOLUTION_SVGA_85,      800,  600,  85,
    ICS8580_VIDEO_RESOLUTION_XGA_60,       1024, 768,  60,
    ICS8580_VIDEO_RESOLUTION_XGA_70,       1024, 768,  70,
    ICS8580_VIDEO_RESOLUTION_XGA_75,       1024, 768,  75,
    ICS8580_VIDEO_RESOLUTION_XGA_85,       1024, 768,  85,
    ICS8580_VIDEO_RESOLUTION_SXGA_60,      1280, 1024, 60,
    ICS8580_VIDEO_RESOLUTION_STANAG_3350A, 1280, 875,  30,
    ICS8580_VIDEO_RESOLUTION_STANAG_3350B, 720,  576,  25,
    ICS8580_VIDEO_RESOLUTION_STANAG_3350C, 720,  487,  30,
    ICS8580_VIDEO_RESOLUTION_UXGA_60,      1600, 1200, 60
};

void resolutionTableLookup(int resolution,int *width,int *height,int *framerate)
{
  int i=0;

  for (i=0; i<30; i++)
  {
    if (ResolutionTable[i].resolution == resolution)
	{
	  *width=ResolutionTable[i].width;
	  *height=ResolutionTable[i].height;
	  *framerate=ResolutionTable[i].framerate;
    }
  }
}

typedef struct
{
	unsigned char r, g, b, a;
} uchar4;

typedef struct
{
	unsigned char r, g, b;
} uchar3;

ICS8580_COMMUNICATION_CHANNEL_HANDLE_T  hChannelHandle = ICS8580_INVALID_COMMUNICATION_CHANNEL_HANDLE_VALUE;
ICS8580_COMMUNICATION_CHANNEL_HANDLE_T  hPcieHandle    = ICS8580_INVALID_COMMUNICATION_CHANNEL_HANDLE_VALUE; 
ICS8580_DATA_CHANNEL_HANDLE_T           hDataTransfer  = NULL;
ICS8580_INT32_T               nCount;
ICS8580_INT32_T               nStatus;
ICS8580_INT32_T               uSkipFrames    = 0;
ICS8580_INT32_T               uTxFrames      = 0;
ICS8580_INT8_T               *pBuffer[NUMBER_OF_BUFFERS];
int g_VideoW=0;
int g_VideoH=0;
int g_BufferAllocated=0;

enum VIEWER_STATE 
{
	VIEWER_STATE_NOT_CONNECTED,
	VIEWER_STATE_CONNECT,
	VIEWER_STATE_CONNECTING,
	VIEWER_STATE_CONNECTED,
	VIEWER_STATE_DISCONNECTED,
	VIEWER_STATE_ERROR
};

int state = VIEWER_STATE_NOT_CONNECTED;

/*
  View the generated file 'viewer_ui.h' to see the following:
    - Constants that can be used.
    - Input group structure containing the input variables.
    - Output group structre containing the output variables.
    - The attribute initialization structures to be passed into dv_create().
    - Example code on how to instrument your code.
*/

ICS8580_VIDEO_IN input_presets[5] = {
	{ICS8580_VIDEO_INPUT5_SDI, ICS8580_VIDEO_TYPE_HD_SDI_SMPTE292M, ICS8580_VIDEO_RESOLUTION_1080I_60},
	{ICS8580_VIDEO_INPUT1_SD_HD_RGBHV_DVI, ICS8580_VIDEO_TYPE_ANALOG_RGBHV, ICS8580_VIDEO_RESOLUTION_UXGA_60},
	{ICS8580_VIDEO_INPUT2_SD_HD_RGBHV, ICS8580_VIDEO_TYPE_ANALOG_RGBHV, ICS8580_VIDEO_RESOLUTION_SXGA_60},
	{ICS8580_VIDEO_INPUT3_SD, ICS8580_VIDEO_TYPE_COMPOSITE, ICS8580_VIDEO_RESOLUTION_PAL},
	{ICS8580_VIDEO_INPUT4_SD, ICS8580_VIDEO_TYPE_COMPOSITE, ICS8580_VIDEO_RESOLUTION_PAL},
};

ICS8580_VIDEO_OUT output_presets[5] = {
	{ICS8580_VIDEO_OUTPUT4_SDI, ICS8580_VIDEO_TYPE_HD_SDI_SMPTE292M, ICS8580_VIDEO_RESOLUTION_1080I_60},
	{ICS8580_VIDEO_OUTPUT3_DVI, ICS8580_VIDEO_TYPE_HDMI_DVI, ICS8580_VIDEO_RESOLUTION_UXGA_60},
	{ICS8580_VIDEO_OUTPUT1_RGB, ICS8580_VIDEO_TYPE_ANALOG_RGBHV, ICS8580_VIDEO_RESOLUTION_SXGA_60},
	{ICS8580_VIDEO_OUTPUT2_TV, ICS8580_VIDEO_TYPE_COMPOSITE, ICS8580_VIDEO_RESOLUTION_PAL},
	{ICS8580_VIDEO_OUTPUT2_TV, ICS8580_VIDEO_TYPE_COMPOSITE, ICS8580_VIDEO_RESOLUTION_PAL},
};

static ICS8580_USER_ARGS params = {0,0,0,0,0,0,0,0};
static int once_fill =0; /* Quick hack to reduce CPU load */

/*******************************************************************
* NAME :            videoOpen(ICS8580_USER_ARGS args)
*
* DESCRIPTION :     Initialise the video channel for input.
*
* INPUTS :
*       PARAMETERS:
*           ICS8580_USER_ARGS     args              GUI arguments struct.
* OUTPUTS :
*       PARAMETERS:
*           none
*       RETURN :
*           Type:   int               Result of update
*           Values: ICS8580_OK            success
*
* NOTES :           none.
*/
//static char* video_tx;
int videoOpen(ICS8580_USER_ARGS args)
{
    params = args;
	TRACE("Allocating video buffer.");
//	video_tx = (char*)malloc(ResolutionTable[(int)args.video_out.videoOutputResolution].width * ResolutionTable[(int)args.video_out.videoOutputResolution].height * 3);

    return Init8580Channels(args);
}

int col=0;

int videoClose(void)
{
    Finalize8580();
//    free (video_tx);
    return ICS8580_OK;
}

/*******************************************************************
* NAME :            fillTestRGB(char *winBuf,int size)
*
* DESCRIPTION :     Initialise the video channel for input (RGB Bars).
*
* INPUTS :
*       PARAMETERS:
*           char     *winBuf           -> The buffer being filled.
*           int      size              The size of the buffer being filled.
* OUTPUTS :
*       PARAMETERS:
*           none.
*       RETURN :
*            Type:   int                   Result of update
*            Values: 1                     success
*
* NOTES :           Used for debug and test only.
*/
#define IMAGE_WIDTH 480
int fillTestRGB(char *winBuf,int size)
    {
    if (once_fill==0)
        {
        int loop,x = 0;
        int colorBar = 0;
        int barsize = IMAGE_WIDTH / 9;
        uchar3 *winPtr;

        winPtr = (uchar3 *)winBuf;

        for (loop=0;loop<size;loop++)
            {
            if ((x>0) && (x<barsize)) colorBar=0;
            if ((x>barsize*1) && (x<barsize*2)) colorBar=1;
            if ((x>barsize*2) && (x<barsize*3)) colorBar=2;
            if ((x>barsize*3) && (x<barsize*4)) colorBar=3;
            if ((x>barsize*4) && (x<barsize*5)) colorBar=4;
            if ((x>barsize*5) && (x<barsize*6)) colorBar=5;
            if ((x>barsize*6) && (x<barsize*7)) colorBar=6;
            if ((x>barsize*7) && (x<barsize*8)) colorBar=7;
            if ((x>barsize*8) && (x<barsize*9)) colorBar=8;
            x++;
            if (x==IMAGE_WIDTH) x=0;

            switch(colorBar)
                {
                case 0:/* Blue */
                    winPtr->r=15;
                    winPtr->g=16;
                    winPtr->b=184;  
                    break;
                case 1:/* Red */
                    winPtr->r=183;
                    winPtr->g=15;
                    winPtr->b=15;  
                    break;
                case 2:/* Magenta */
                    winPtr->r=182;
                    winPtr->g=15;
                    winPtr->b=183;  
                    break;
                case 3:/* Green */
                    winPtr->r=15;
                    winPtr->g=182;
                    winPtr->b=114;  
                    break;
                case 4:/* Cyan */
                    winPtr->r=13;
                    winPtr->g=181;
                    winPtr->b=181;  
                    break;
                case 5:/* Yellow */
                    winPtr->r=182;
                    winPtr->g=181;
                    winPtr->b=13;  
                    break;
                case 7:/* Gray */
                    winPtr->r=181;
                    winPtr->g=181;
                    winPtr->b=181;  
                    break;
                case 6:/* White */
                    winPtr->r=255;
                    winPtr->g=255;
                    winPtr->b=255;  
                    break;
                case 8:/* Black */
                    winPtr->r=0;
                    winPtr->g=0;
                    winPtr->b=0;  
                    break;
                }
            winPtr++;
            }
        col++;
        if (col==254) col=0;
        once_fill=1;
        }
    return(1);
    }	

int Init8580(ICS8580_USER_ARGS args)
{
	ICS8580_CHAR_T                pcieName[]     = PCI_NAME;
    ICS8580_LOGIN_PARAMS          sLoginParams;

//    GST_DEBUG_CATEGORY_INIT (gst_8580capture_debug, "8580capture", 0, "Template 8580capture");
//    gst_debug_set_threshold_for_name ("8580capture", GST_LEVEL_DEBUG); 

    memset(&sLoginParams,  0, sizeof(ICS8580_LOGIN_PARAMS));

	/* Create Socket interface handle for configuring PCIe. Disable automatic event listener. */
    TRACE("Opening PCIe device %s --> \n", (char*)pcieName);
    if(NULL == (hChannelHandle = ics8580DeviceOpenEx((char*)pcieName, ICS8580_STOP_EVENT_LISTENER_FLAG))) {
        Finalize8580();
        TRACE_ERROR("Failed to open PCIe device 'PCIe:\\0'");
        return ICS8580_OUTPUT_ERROR;
    }

    /* Login to DSP application */
    strcpy((char*)sLoginParams.username, args.username);
    strcpy((char*)sLoginParams.password, args.password);
    if (ICS8580_OK != ics8580UserLogin (hChannelHandle, &sLoginParams)) {
        Finalize8580();
        TRACE ("ics8580UserLogin: user %s, password %s - failed\n", sLoginParams.username, sLoginParams.password);
        return ICS8580_ERROR;
    }
	return ICS8580_OK;
}

/*******************************************************************
* NAME :            int Init8580Channels(void)
*
* DESCRIPTION :     Initialise the ICS-8580.
*
* INPUTS :
*       PARAMETERS:
*           none
* OUTPUTS :
*       PARAMETERS:
*           none
*       RETURN :
*            Type:   int                   Result of update
*            Values: 1                     success
*
* NOTES :           Default configuration is for PCI control and video channels (See PCI_CONFIG macro).
*                   Video input resolutions are hardcoded to XGA 1024x768 RBG.
*/
int Init8580Channels(ICS8580_USER_ARGS args)
    {
    if	(args.debug)
    {
        /* Nothing to do in debug mode */
    }
    else
    {
        /* Local variable declaration */
		ICS8580_CHAR_T                pcieName[]     = PCI_NAME;
        ICS8580_USER_PCIE_CONFIGS     sPcieConfig;
        ICS8580_INT32_T               enableConfig   = ENABLE_CONFIG;
        ICS8580_ULONG_T               uBufferSize;

        memset(&sPcieConfig,   0, sizeof(ICS8580_USER_PCIE_CONFIGS));
		g_VideoW=ResolutionTable[(int)args.video_out.videoOutputResolution].width;
		g_VideoH=ResolutionTable[(int)args.video_out.videoOutputResolution].height;

        /* Put selected values into the PCIe configuration structure */
        sPcieConfig.pcieChannelId        = args.channel;
        sPcieConfig.pcieChannelEnable    = 1;
        sPcieConfig.pcieMode             = ICS8580_PCIE_MODE_RECEIVE;
        sPcieConfig.pcieType             = ICS8580_PCIE_TYPE_VIDEO;

		sPcieConfig.videoInput           = (ICS8580_VIDEO_INPUT)args.video_in.videoInput;
        sPcieConfig.videoInputType       = (ICS8580_VIDEO_TYPE)args.video_in.videoInputType;
        sPcieConfig.videoInputResolution = (ICS8580_VIDEO_RESOLUTION)args.video_in.videoInputResolution;

		sPcieConfig.videoOutput           = (ICS8580_VIDEO_OUTPUT)args.video_out.videoOutput;
        sPcieConfig.videoOutputType       = (ICS8580_VIDEO_TYPE)args.video_out.videoOutputType;
        sPcieConfig.videoOutputResolution = (ICS8580_VIDEO_RESOLUTION)args.video_out.videoOutputResolution;

  		uBufferSize  = (ResolutionTable[(int)sPcieConfig.videoOutputResolution].width * 
                                      ResolutionTable[(int)sPcieConfig.videoOutputResolution].height  ) * 2; /* Buffer size for YUV422 data */

        TRACE("\n---- User configurations for all channels---- \n");
        TRACE("\tX Resoltution: %d\n", g_VideoW);
        TRACE("\tY Resoltution: %d\n", g_VideoH);
        TRACE("\tY Video Format: YUV422\n");
        TRACE("\tPCIe Configuration: %s\n", ((1 == enableConfig)?"enabled":"disabled"));

        if(1 == enableConfig) {
            TRACE("\n---- PCIe configurations for channel %d ---- \n", sPcieConfig.pcieChannelId);
			TRACE("\tOutput ADV Channel: %d\n", sPcieConfig.videoOutput );
			TRACE("\tOutput Format: %d\n",      sPcieConfig.videoOutputType );
			TRACE("\tOutput Std: %d (%d,%d)\n", sPcieConfig.videoOutputResolution, g_VideoW, g_VideoH );
            TRACE("\n");

            /* Check for HD output std on channel 2 & 3 */
            if((2 == sPcieConfig.pcieChannelId) || (3 == sPcieConfig.pcieChannelId)){
                if((sPcieConfig.videoOutputResolution >= ICS8580_VIDEO_RESOLUTION_720P_50) && (sPcieConfig.videoOutputResolution <= ICS8580_VIDEO_RESOLUTION_1080P_60)){
                    TRACE_ERROR("HD resolution not supported on PCIe channel 2 & 3 \n");
                    Finalize8580Channels();
                    return ICS8580_ERROR;
                }
            }

#if OUTPUT_ENABLED
			/* Call ics8580ConfigurePcieDevice for PCIe output configuration over socket */
            TRACE("Configuring PCIe channel --> \n");
            if(ICS8580_OK != ics8580ConfigurePcieDevice(hChannelHandle, &sPcieConfig)) {
                TRACE_ERROR ("failed to open\n");
                Finalize8580Channels();
                return ICS8580_ERROR;
            }
#endif
        }

        /* Create PCIe interface handles for data transfer */
        TRACE("Opening data device %s --> \n", (char*)pcieName);
        hPcieHandle = ics8580DataDeviceOpen (pcieName);
        if((NULL == hPcieHandle)||(ICS8580_INVALID_COMMUNICATION_CHANNEL_HANDLE_VALUE == hPcieHandle)) {
            TRACE_ERROR("failed\n");
            Finalize8580Channels();
            return ICS8580_INPUT_ERROR;
        }
	
		TRACE("Opened all channels success\n");


#if OUTPUT_ENABLED
		/* Create Data Channel with the user parameters */
		TRACE_ERROR("Test 1 %d %d 0x%d 0x%d\n", args.channel, NUMBER_OF_BUFFERS, pBuffer[0], pBuffer[1]);
		if(ICS8580_OK != ics8580DataChannelCreate (&hDataTransfer, 
				                                  hPcieHandle, 
				                                  args.channel, 
				                                  pBuffer, 
				                                  uBufferSize, 
				                                  ICS8580_PCIE_TYPE_VIDEO, 
				                                  NUMBER_OF_BUFFERS)) {
		TRACE_ERROR("ics8580DataChannelCreate failed\n");
		}
		TRACE_ERROR("Test 2 %d %d 0x%d 0x%d\n", args.channel, NUMBER_OF_BUFFERS, pBuffer[0], pBuffer[1]);

		/* Queue all the buffers */
		if(ICS8580_OK != ics8580StartWriteQueue (hDataTransfer)) {
    		TRACE_ERROR("Failed to start the queue\n");
		}

#endif
            nCount = 0;
            TRACE("Please wait while the application is receiving frames...\n");  
        }
    return ICS8580_OK;
    }

/*******************************************************************
* NAME :            int GetFrame8580(char **buf)
*
* DESCRIPTION :     Wait for one frame of video and return the buffer.
*
* INPUTS :
*       PARAMETERS:
*           none
* OUTPUTS :
*       PARAMETERS:
*           char     **buf             The video buffer.
*       RETURN :
*            Type:   int                   Result of update
*            Values: 1                     success
*
* NOTES :           none.
*/
#define STRIDE 1
int GetPut8580(char *buf)
    {
	int width, height;
	width = ResolutionTable[(int)params.video_out.videoOutputResolution].width;
	height = ResolutionTable[(int)params.video_out.videoOutputResolution].height;

    ICS8580_ULONG_T nIndex = nCount%NUMBER_OF_BUFFERS;

    TRACE_ERROR("Processing frame %d %d...\r", nCount, width);
    nCount++;

#if OUTPUT_ENABLED
    if(ICS8580_OK != ics8580DataDequeue(hDataTransfer, nIndex, 7000 /*mseconds*/)) {                  /* Wait for completion for oldest pBuffer in the queue */

      ics8580DataDequeueGetReturn(hDataTransfer, nIndex, &nStatus);                                   /* Get PCIe transfer status */
      if(ICS8580_AIO_EINPROGRESS == nStatus)                                                          /* The transfer is in progress, must to wait until it will be released */
        return ICS8580_OK;                                                                                                                

      uSkipFrames++;
      printf("Skipped %d of %d with error %d \n", uSkipFrames, nCount, nStatus);
    }
    else {
  	  /* No colour conversion required */
#if 0
	  memcpy((byte*)pBuffer[nIndex], (byte *)buf, width*height*2);
#else	  
    {
      int i;
      byte * pbuffo, * pbuffe;
      
      pbuffo = (byte*)pBuffer[nIndex];
 //     pbuffe = (byte*)pBuffer[1];
      
  	  for (i=0;i<height/2;i++)
	    {
	      memcpy(&pbuffo[(i*2)*(width*2)], (byte *)&buf[(i*2)*(width*2)], (width*2));
//	      memcpy(&pbuffe[(i*STRIDE)*width*2], (byte *)&buf[(i)*width], width*2);
	    }
#if 0
  	  for (i=1;i<height/2;i++)
	    {
	      memcpy(&pbuff2[(width/2)+i*width], (byte *)&buf[(i*2)*width], width);
	    }
#endif
	  }
#endif

      if(ICS8580_OK != ics8580DataWriteQueue (hDataTransfer, nIndex)) {                               /* Re-submit the pBuffer */
        printf("Write Queue failed for channel %d\n", DMA_CHANNEL);
      }
    }
#endif

    uTxFrames++;
    return ICS8580_OK;
}

/*******************************************************************
* NAME :            int Finalize8580 ( void )
*
* DESCRIPTION :     Finalize the video
*
* INPUTS :
* OUTPUTS :
*       PARAMETERS:
*           none
*       RETURN :
*            Type:   int                  On error the return value is false.
*
* NOTES :           none.
*/
int Finalize8580(void)
{
    /* Close PCIe communication channel 0 */
    if(ICS8580_INVALID_COMMUNICATION_CHANNEL_HANDLE_VALUE != hChannelHandle)
    {
        ics8580DeviceClose(hChannelHandle);
        hChannelHandle = ICS8580_INVALID_COMMUNICATION_CHANNEL_HANDLE_VALUE;
    }
	return ICS8580_OK;
}

int Finalize8580Channels(void)
{
	if (!params.debug)
    {
		/* Close Data channel 0 */
		if(NULL != hDataTransfer) 
		{
			ics8580DataChannelClose(hDataTransfer);
			hDataTransfer = NULL;
		}
		/* Close PCIe device 0 */
		if(ICS8580_INVALID_COMMUNICATION_CHANNEL_HANDLE_VALUE != hPcieHandle) 
		{
			ics8580DataDeviceClose(hPcieHandle);
			hPcieHandle = ICS8580_INVALID_COMMUNICATION_CHANNEL_HANDLE_VALUE;
		}

#if OUTPUT_ENABLED
		if (params.output)
		{
			/* Queue all the buffers */
			if(ICS8580_OK != ics8580StopWriteQueue (hDataTransfer))
			{
				TRACE_ERROR("Failed to stop the queue\n");
			}

			/* Close Data channel 1 */
			if(NULL != hDataTransfer) 
			{
				ics8580DataChannelClose(hDataTransfer);
				hDataTransfer = NULL;
			}
			/* Close PCIe device 1 */
			if(ICS8580_INVALID_COMMUNICATION_CHANNEL_HANDLE_VALUE != hPcieHandle) 
			{
				ics8580DataDeviceClose(hPcieHandle);
				hPcieHandle = ICS8580_INVALID_COMMUNICATION_CHANNEL_HANDLE_VALUE;
			}
        }
#endif
    }
	return ICS8580_OK;
}

static  ICS8580_USER_CONFIGS          sUserConfigs;
int ics8580_encode(ICS8580_COMMUNICATION_CHANNEL_HANDLE_T *handle, ICS8580_USER_ARGS *video_args)
{
	/* Local variable declaration */
	params.streamId = 0;

	/* Clear data to zero */
	memset(&sUserConfigs, 0, sizeof(ICS8580_USER_CONFIGS));

	/* Configure the encoder stream 1 */
	sUserConfigs.streamConfigs[params.streamId].streamId                           = (ICS8580_STREAM_ID)0;
	sUserConfigs.streamConfigs[params.streamId].streamEnable                       = ICS8580_STREAM_STATE_ENABLE;
	sUserConfigs.streamConfigs[params.streamId].streamPlay                         = 0;
	sUserConfigs.streamConfigs[params.streamId].streamType                         = ICS8580_STREAM_TYPE_ENCODE;

	sUserConfigs.streamConfigs[params.streamId].videoInput                         = (ICS8580_VIDEO_INPUT)video_args->video_in.videoInput;
	sUserConfigs.streamConfigs[params.streamId].videoInputType                     = (ICS8580_VIDEO_TYPE)video_args->video_in.videoInputType;
	sUserConfigs.streamConfigs[params.streamId].videoInputResolution               = (ICS8580_VIDEO_RESOLUTION)video_args->video_in.videoInputResolution;

	sUserConfigs.streamConfigs[params.streamId].encodeVideoOutput                  = (ICS8580_VIDEO_OUTPUT)video_args->video_out.videoOutput;
	sUserConfigs.streamConfigs[params.streamId].encodeVideoOutputType              = (ICS8580_VIDEO_TYPE)video_args->video_out.videoOutputType;
	sUserConfigs.streamConfigs[params.streamId].encodeVideoOutputResolution        = (ICS8580_VIDEO_RESOLUTION)video_args->video_out.videoOutputResolution;

	sUserConfigs.streamConfigs[params.streamId].audioInputMode                     = ICS8580_AUDIO_MODE_DISABLED;
	sUserConfigs.streamConfigs[params.streamId].metadataMode                       = ICS8580_METADATA_DISABLED;

	sUserConfigs.streamConfigs[params.streamId].videoCodecType                     = ICS8580_VIDEO_CODEC_H264;
	sUserConfigs.streamConfigs[params.streamId].videoCodecProfileIDC               = ICS8580_VIDEO_MAIN_PROFILE;
	sUserConfigs.streamConfigs[params.streamId].videoCodecScanMode                 = ICS8580_SCAN_MODE_PROGRESSIVE;
	sUserConfigs.streamConfigs[params.streamId].videoCodecBitRateControlMode       = ICS8580_BITRATE_CONTROL_CBR;
	sUserConfigs.streamConfigs[params.streamId].videoCodecBitRate                  = 4000000;
	sUserConfigs.streamConfigs[params.streamId].videoCodecTargetFrameRate          = 30;
	sUserConfigs.streamConfigs[params.streamId].videoCodecIFrameInterval           = 30;
	sUserConfigs.streamConfigs[params.streamId].videoCodecVbrQualityParameter      = 25;

	sUserConfigs.streamConfigs[params.streamId].audioCodecType                     = ICS8580_AUDIO_CODEC_DISABLED;
	sUserConfigs.streamConfigs[params.streamId].audioCodecBitRate                  = 0;
	sUserConfigs.streamConfigs[params.streamId].audioSampleFrequency               = ICS8580_AUDIO_SAMPLE_FREQUENCY_48000HZ;

	sUserConfigs.streamConfigs[params.streamId].transportEncMode                   = ICS8580_TS_ETHERNET;
	sUserConfigs.streamConfigs[params.streamId].transportMuxDemuxMethod            = ICS8580_ENCAP_METHOD_MPEG2TS_UDP;
	sUserConfigs.streamConfigs[params.streamId].transportUdpMode                   = ICS8580_UDP_MODE_UNICAST;
	sUserConfigs.streamConfigs[params.streamId].transportTotalDestinationCnt       = 1;

	strcpy ((char*)sUserConfigs.streamConfigs[params.streamId].transportIPAddress[0], "0.0.0.0");
	sUserConfigs.streamConfigs[params.streamId].transportIPAddressPort[0]          = 0;
	strcpy ((char*)sUserConfigs.streamConfigs[params.streamId].transportIPAddress[1], "0.0.0.0");
	sUserConfigs.streamConfigs[params.streamId].transportIPAddressPort[1]          = 0;
	strcpy ((char*)sUserConfigs.streamConfigs[params.streamId].transportIPAddress[2], "0.0.0.0");
	sUserConfigs.streamConfigs[params.streamId].transportIPAddressPort[2]          = 0;
	strcpy ((char*)sUserConfigs.streamConfigs[params.streamId].transportIPAddress[3], "0.0.0.0");
	sUserConfigs.streamConfigs[params.streamId].transportIPAddressPort[3]          = 0;
	strcpy ((char*)sUserConfigs.streamConfigs[params.streamId].transportIPAddress[4], "0.0.0.0");
	sUserConfigs.streamConfigs[params.streamId].transportIPAddressPort[4]          = 0;
  
	/* Set cameralink params if cameralink is the input */
	if (sUserConfigs.streamConfigs[params.streamId].videoInput == ICS8580_VIDEO_INPUT7_CAMERALINK) {
		sUserConfigs.streamConfigs[params.streamId].cameralinkOffsetX                = 100;
		sUserConfigs.streamConfigs[params.streamId].cameralinkOffsetY                = 10;
		sUserConfigs.streamConfigs[params.streamId].cameralinkBitMode                = ICS8580_CAMERALINK_BIT_MODE_24BIT_RGB;
		sUserConfigs.streamConfigs[params.streamId].cameralinkTapMode                = ICS8580_CAMERALINK_TAP_MODE_NONE;
	}

	if(ICS8580_OK != ics8580StreamConfigureAll(*handle, sUserConfigs.streamConfigs)) {
		TRACE_ERROR("Configure Stream %d failed for encoder IP address <%s> over port <%d>\n",          
             sUserConfigs.streamConfigs[params.streamId].streamId, 
             sUserConfigs.streamConfigs[params.streamId].transportIPAddress[0], 
             sUserConfigs.streamConfigs[params.streamId].transportIPAddressPort[0]);
		return -1;
	}

	TRACE("Encoder setup for stream%d to IP %s:%d\n", params.streamId, 
      sUserConfigs.streamConfigs[params.streamId].transportIPAddress[0], 
      sUserConfigs.streamConfigs[params.streamId].transportIPAddressPort[0]);
	return ICS8580_OK;
}

int ics8580_encode_start(ICS8580_COMMUNICATION_CHANNEL_HANDLE_T *handle)
{  
	/* Start the streams */
	if(ICS8580_OK != ics8580StreamStartAll(*handle)) {
		TRACE_ERROR("ics8580StreamStartAll failed\n");
		return ICS8580_STREAM_ERROR;
	}
	TRACE("Stream started ...\n");
	return ICS8580_OK;
}

int ics8580_encode_stop(ICS8580_COMMUNICATION_CHANNEL_HANDLE_T *handle)
{
	  /* Stop the streams */
	if(ICS8580_OK != ics8580StreamStopAll(*handle)) {
		TRACE("ics8580StreamStopAll failed\n");
		return ICS8580_STREAM_ERROR;
	}
	TRACE("Stream stopped ...\n");
	return ICS8580_OK;
}

int paused = 0;
int ics8580_encode_pause(ICS8580_COMMUNICATION_CHANNEL_HANDLE_T *handle)
{
	ICS8580_STREAM_PAUSE_PARAMS pause;
	  /* Pause the streams */
	pause.streamId = (ICS8580_STREAM_ID)params.streamId;

	if (paused == 0)
	{
		if(ICS8580_OK != ics8580StreamPause(*handle, &pause)) {
			TRACE_ERROR("ics8580StreamPause failed\n");
			return ICS8580_STREAM_ERROR;
		}
		TRACE("Stream paused...\n");
		paused = 1;
	}
	else
	{
		ICS8580_STREAM_RESUME_PARAMS  resume;
		resume.streamId = (ICS8580_STREAM_ID)params.streamId;
		if(ICS8580_OK != ics8580StreamResume(*handle, &resume)) {
			TRACE_ERROR("ics8580StreamResume failed\n");
			return ICS8580_STREAM_ERROR;
		}
		TRACE("Stream unpaused...\n");
		paused = 0;
	}
	return ICS8580_OK;
}



