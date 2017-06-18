#include "cat.h"

extern char gAbsPath[MAX_LEN];

#define   CROP_W	180
#define   CROP_H	250

// > kibo+ support new debugparser
#define NUM_CHIP	13
u8 chipName[] = {35,37,53,70,95,52,57,97,55,50,99};// this is old debugParser support.

typedef struct _locateImage
{
	muImage_t *subYImg;
	muImage_t *subRGBImg;
	muImage_t *yImg;
}locateImage_t;

typedef struct _hsvData
{
	MU_64F avgH, avgS, avgV;
}hsvData_t;

typedef struct _aeInfo
{
	MU_64F avgY;
	MU_64F hisSD;
}aeInfo_t;

static aeInfo_t aeCheck(muImage_t *yImg)
{
	MU_64F avgY, sum=0;
	MU_64F variance;
	MU_32U *his;
	aeInfo_t info;
	for(int i=0; i<yImg->width*yImg->height; i++)
	{
		sum+=yImg->imagedata[i];
	}

	avgY = sum/(MU_64F)(yImg->width*yImg->height);

	his = (MU_32U *)malloc(256*sizeof(MU_32U));
	memset(his, 0, 256*sizeof(MU_32U));

	muHistogram( yImg, his);
	
	// find variance
	sum = 0;
	for(int i=0; i<256; i++)
	{
		if(his[i])
			sum+=(his[i]*(i-avgY)*(i-avgY));
	}
	variance = sum/(MU_64F)(yImg->width*yImg->height);
	printf("ae sigma = %f \n", sqrt(variance));

	if(his)
		free(his);

	info.avgY = avgY;
	info.hisSD = sqrt(variance);

	return (info);
}

static MU_64F afCheck(muImage_t *yImg)
{
	MU_64F bm;
	muNoRefBlurMetric(yImg, &bm);
	return bm;
}

static hsvData_t awbCheck(muImage_t *rgbImg)
{
	muImage_t *hsvImg;
	muSize_t size;
	hsvData_t hsv;
	MU_16S *buf;
	size = muSize(rgbImg->width, rgbImg->height);
	hsvImg = muCreateImage(size, MU_IMG_DEPTH_16U, 3);
	muRGB2HSV(rgbImg, hsvImg);
	memset(&hsv, 0, sizeof(hsvData_t));

	buf = (MU_16S *)hsvImg->imagedata;
	for(int i=0; i<size.width*size.height*3; i+=3)
	{
		hsv.avgH += buf[i];
		hsv.avgS += buf[i+1];
		hsv.avgV += buf[i+2];
	}
	hsv.avgH = (hsv.avgH)/(MU_64F)(size.width*size.height);
	hsv.avgS = (hsv.avgS)/(MU_64F)(size.width*size.height);
	hsv.avgV = (hsv.avgV)/(MU_64F)(size.width*size.height);

	if(hsvImg)
		muReleaseImage(&hsvImg);

	return hsv;
}


static locateImage_t *locatePattern(muImage_t *in)
{
	muImage_t *yImg, *edgeImg, *otsuImage;
	muImage_t *subYImg, *subRGBImg;
	muSize_t size,subSize;
	FILE *img;
	muRect_t cropRect;
	muPoint_t gravity;
	locateImage_t *locateImg;
	
	locateImg = (locateImage_t *)malloc(sizeof(locateImage_t));
	subSize = muSize(CROP_W, CROP_H);
	size = muSize(in->width, in->height);

	yImg = muCreateImage(size, MU_IMG_DEPTH_8U, 1);
	edgeImg = muCreateImage(size, MU_IMG_DEPTH_8U, 1);
	otsuImage = muCreateImage(size, MU_IMG_DEPTH_8U, 1);
	subYImg = muCreateImage(subSize, MU_IMG_DEPTH_8U, 1);
	subRGBImg = muCreateImage(subSize, MU_IMG_DEPTH_8U, 3);
	muRGB2GrayLevel(in, yImg);
	muSobel(yImg, edgeImg);

	muOtsuThresholding(edgeImg, otsuImage);
	gravity = muFindGravityCenter(otsuImage);
	logInfo("gx= %d, gy=%d \n", gravity.x, gravity.y);
	cropRect.width = CROP_W;
	cropRect.height = CROP_H;
	cropRect.x = gravity.x-(CROP_W/2);
	if(cropRect.x < 0)
		cropRect.x = 0;
	cropRect.y = gravity.y-(CROP_H/2);
	if(cropRect.y < 0)
		cropRect.y = 0;

	muGetSubImage(yImg, subYImg, cropRect);
	muGetRGBSubImage(in, subRGBImg, cropRect);
	

	muSaveBMP( "123.bmp", subRGBImg);
	img = fopen("123.yuv", "wb");
	fwrite(subYImg->imagedata, 1, cropRect.width*cropRect.height, img);
	fclose(img);

	muReleaseImage(&edgeImg);
	muReleaseImage(&otsuImage);
	locateImg->subYImg = subYImg;
	locateImg->subRGBImg = subRGBImg;
	locateImg->yImg = yImg;

	return locateImg;
}

static muSize_t getNewResolution(muSize_t oSize)
{
	muSize_t nSize;
	double ratio;

	printf("w :%d h: %d\n", oSize.width, oSize.height);

	if(oSize.width > oSize.height) // horizon image
	{
		ratio = (oSize.width/(double)oSize.height);
		if( ratio > 1.7) //16:9
		{
			nSize.width = 640;
			nSize.height = 360;
		}
		else // 4:3
		{
			nSize.width = 640;
			nSize.height = 480;
		}
	}
	else // vertical
	{
		ratio = (oSize.height/(double)oSize.width);
		if(ratio > 1.7) //16:9
		{
			nSize.width = 360;
			nSize.height = 640;
		}
		else // 4:3
		{
			nSize.width = 480;
			nSize.height = 640;
		}
	}

	return nSize;
}

int aaacheckImage(catArg_t arg)
{
	int ret;
	char buf[MAX_LEN];
	char tempBuf[TEMP_LEN];
	char *tempName;
	muImage_t *inImage;
	muSize_t size, nSize;
	locateImage_t *locateImg;
	FILE *img;
	MU_64F bm, avgY;
	aeInfo_t info;
	hsvData_t hsv;

	if(arg.imageName == NULL)
		return -1;
	ret = checkExtension(arg.imageName, ".jpg");
	if(ret)
	{
		logError("error input image :%s must a jpg format\n", arg.imageName);
		return -1;
	}
	ret = fileExist(arg.imageName);
	if(ret)
		return -1;

	memset(buf, 0, sizeof(char)*MAX_LEN);
	sprintf(buf, "%s.\\prebuilt\\identify.exe %s 2>&1", gAbsPath, arg.imageName);
	size = getResolution(buf);
	
	if(size.width == -1 || size.height == -1)
	{
		logError("cannot identify image\n");
		return -1;
	}

	nSize = getNewResolution(size);

	//transfer the original image to bmp
	memset(buf, 0, sizeof(char)*MAX_LEN);
	memset(tempBuf, 0, sizeof(char)*TEMP_LEN);
	
	tempName = getRealFileName(arg.imageName, ".jpg");
	sprintf(tempBuf, "%s.bmp", tempName);

	sprintf(buf, "%s.\\prebuilt\\magick.exe %s -type truecolor -resize %d!x%d! %s 2>&1", gAbsPath, arg.imageName, nSize.width, nSize.height, tempBuf);
	ret = runCommand(buf, "error");
	if(ret)
	{
		logInfo("transfer bmp failed\n");
		return -1;
	}
	printf("%s\n", buf);
#if 0
	//get AF windows
	memset(buf, 0, sizeof(char)*MAX_LEN);
	for(int j=0; j<NUM_CHIP; j++)
	{
		if(arg.chipName == chipName[j] )
		{
			if(j > 3) // new-dp
				sprintf(buf, "%s.\\prebuilt\\dp\\dp_new\\Project_DP.exe --dump %s 2>&1", gAbsPath, arg.imageName);	
			else // old dp
				sprintf(buf, "%s.\\prebuilt\\dp\\dp_old\\Project_DP.exe --dump %s 2>&1", gAbsPath, arg.imageName);	
		}
	}
	ret = runCommand(buf, NULL);
	if(ret)
	{
		logInfo("dump dp error\n");
		return -1;
	}
#endif

	//locate af image
	inImage = muLoadBMP(tempBuf);
	locateImg = locatePattern(inImage);
	bm = afCheck(locateImg->subYImg);
	info = aeCheck(locateImg->yImg);
	muSaveBMP("locateImg.bmp", locateImg->subRGBImg);
	hsv = awbCheck(locateImg->subRGBImg);
	logInfo("bm=%f  avgY=%f std=%f avgH=%f  avgS=%f  avgV=%f\n", bm, info.avgY, info.hisSD, hsv.avgH, hsv.avgS, hsv.avgV);

	removeFile(tempBuf);
	if(inImage)
		muReleaseImage(&inImage);
	if(tempName)
		free(tempName);
	if(locateImg->subRGBImg)
		muReleaseImage(&locateImg->subRGBImg);
	if(locateImg->subYImg)
		muReleaseImage(&locateImg->subYImg);
	if(locateImg->yImg)
		muReleaseImage(&locateImg->yImg);
	if(locateImg)
		free(locateImg);

}