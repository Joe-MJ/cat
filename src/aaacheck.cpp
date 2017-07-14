#include "cat.h"


#define CROP_W		180
#define CROP_H		250
#define	VIDEO_WIDTH	480
#define BM_ROI_W    150
#define BM_ROI_H	50	
#define OF_X		3
#define OF_Y		3
#define AWB_TH_1	15.0
#define AWB_TH_2	12.0
#define AE_TH		40.0
#define AF_TH		2.1
#define BLACK_TH	15
#define FLICK_TH	30
#define FRAMERATE	15
#define LAST_FRAME	5

enum
{
	IMAGE_MODE = 0,
	VIDEO_MODE,
};

// > kibo+ support new debugparser
#define NUM_CHIP	13
u8 chipName[] = {35,37,53,70,95,52,57,97,55,50,99};// this is old debugParser support.

//for debug;
char gBuf[TEMP_LEN];
FILE *gFp = NULL;

typedef struct _labInfo
{
	MU_64F l,a,b;
}labInfo_t;

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

typedef struct _previewScene
{
	int pNum;
	int startFrameCount;
	int endFrameCount;
	int totalFrame;
	int totalSecond;
}previewScene_t;


static int saveYImg(char *name, muImage_t *yImg)
{
	FILE *img;	
	img = fopen(name, "wb");
	fwrite(yImg->imagedata, 1,  yImg->width*yImg->height*sizeof(MU_8U), img);
	fclose(img);
	return 0;
}

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

	if(his)
		free(his);

	info.avgY = avgY;
	info.hisSD = sqrt(variance);

	return (info);
}

static MU_64F afCheck(muImage_t *yImg, int mode, int frameCount)
{
	MU_64F bm;
	muRect_t rect;
	muImage_t *cropImg;
	muImage_t *rImg = NULL;
	int horiFlag = 0;

	if(mode == IMAGE_MODE)
	{
		rect.width = BM_ROI_W;
		rect.height = BM_ROI_H;
	}
	else
	{
		if(yImg->width > yImg->height) //hori
		{
			horiFlag = 1;
			rImg = muImageRotate(yImg, 270);
			if(rImg->width < (BM_ROI_W - OF_X))
				rect.width = rImg->width - 5;
			else
				rect.width = BM_ROI_W;
			if(rImg->height < (BM_ROI_H - OF_Y))
				rect.height = rImg->height - 5;
			else
				rect.height = BM_ROI_H;
		}
		else
		{
			if(yImg->width < (BM_ROI_W - OF_X))
				rect.width = yImg->width - 5;
			else
				rect.width = BM_ROI_W;
			if(yImg->height < (BM_ROI_H - OF_Y))
				rect.height = yImg->height - 5;
			else
				rect.height = BM_ROI_H;
		}
	}

	rect.x = OF_X;
	rect.y = OF_Y;
	cropImg = muCreateImage(muSize(rect.width, rect.height), MU_IMG_DEPTH_8U, 1);
	if(horiFlag)
		muGetSubImage(rImg, cropImg, rect);
	else
		muGetSubImage(yImg, cropImg, rect);

#if DEBUG_OUTPUT
	memset(gBuf, 0, sizeof(char)*TEMP_LEN);
	sprintf(gBuf, "bm_%d_%dx%d.yuv", frameCount, cropImg->width, cropImg->height);
	saveYImg(gBuf, cropImg);
#endif

	muNoRefBlurMetric(cropImg, &bm);

	if(cropImg)
		muReleaseImage(&cropImg);
	if(rImg)
		muReleaseImage(&rImg);
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

//input a rgb image
static locateImage_t *locatePattern(muImage_t *in, int mode)
{
	muImage_t *yImg, *edgeImg, *otsuImage;
	muImage_t *subYImg, *subRGBImg;
	muSize_t size,subSize;
	FILE *img;
	muRect_t cropRect;
	muPoint_t gravity;
	locateImage_t *locateImg;
	muError_t ret;
	
	locateImg = (locateImage_t *)malloc(sizeof(locateImage_t));
	size = muSize(in->width, in->height);

	yImg = muCreateImage(size, MU_IMG_DEPTH_8U, 1);
	edgeImg = muCreateImage(size, MU_IMG_DEPTH_8U, 1);
	otsuImage = muCreateImage(size, MU_IMG_DEPTH_8U, 1);

	muRGB2GrayLevel(in, yImg);
	muSobel(yImg, edgeImg);

	muOtsuThresholding(edgeImg, otsuImage);

	if(mode == IMAGE_MODE)
	{
		subSize = muSize(CROP_W, CROP_H);
		subYImg = muCreateImage(subSize, MU_IMG_DEPTH_8U, 1);
		subRGBImg = muCreateImage(subSize, MU_IMG_DEPTH_8U, 3);
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
	}
	else
	{
		muImage_t *eroImg, *diImg, *labImg;
		muImage_t *scaleImg;
		muSize_t nSize;
		int w = 0, h = 0;
		int x = 0, y = 0;
		MU_8U numLabel = 0;
		nSize.width = size.width/4;
		nSize.height = size.height/4;
		eroImg = muCreateImage(nSize, MU_IMG_DEPTH_8U, 1);
		diImg = muCreateImage(nSize, MU_IMG_DEPTH_8U, 1);
		labImg = muCreateImage(nSize, MU_IMG_DEPTH_8U, 1);
		scaleImg = muCreateImage(nSize, MU_IMG_DEPTH_8U, 1);
		muSetZero(scaleImg);
		ret = muDownScale(otsuImage, scaleImg, 4, 4);
		if(ret != MU_ERR_SUCCESS)
		{
			muDebugError(ret);
		}
		muSetZero(eroImg);
		muSetZero(diImg);
		muSetZero(labImg);
		muDilate55(scaleImg, diImg);
		muErode55(diImg, eroImg);

		//memset(gBuf, 0, sizeof(char)*TEMP_LEN);
		//sprintf(gBuf, "k99-h-dia_%d_%dx%d.yuv", count, eroImg->width, eroImg->height);
		//saveYImg(gBuf, eroImg);
		mu4ConnectedComponent8u(eroImg, labImg, &numLabel);
		if(numLabel > 1)
		{
			muSeq_t *seq = NULL;
			muSeqBlock_t *current;
			muBoundingBox_t *bp;
			muDoubleThreshold_t th;
			muPoint_t rpMin, rpMax;
			int maxArea = 0;
			th.min = ((VIDEO_WIDTH/4)*5);
			th.max = 0x3FFFFFFF;
			seq = muFindBoundingBox(labImg, numLabel, th);
			current = seq->first;
			while(current != NULL)
			{
				bp = (muBoundingBox_t *) current->data;
				if(bp->area > maxArea)
				{
					maxArea = bp->area;
					w = bp->width;
					h = bp->height;
					x = bp->minx;
					y = bp->miny;
				}
				current = current->next;
			}
			if(seq)
				muClearSeq(&seq);
		}
		else
		{
			w = 2; h = 1;
			
			memset(gBuf, 0, sizeof(char)*TEMP_LEN);
			sprintf(gBuf, "QQero_%dx%d.yuv", yImg->width, yImg->height);
			saveYImg(gBuf, yImg);
			logError("locate Image error!!\n");
		}

		cropRect.x = x << 2; cropRect.y = y << 2;
		cropRect.width = w << 2; cropRect.height = h << 2;
		subYImg = muCreateImage(muSize(cropRect.width, cropRect.height), MU_IMG_DEPTH_8U, 1);
		subRGBImg = muCreateImage(muSize(cropRect.width, cropRect.height), MU_IMG_DEPTH_8U, 3);

		if(eroImg)
			muReleaseImage(&eroImg);
		if(diImg)
			muReleaseImage(&diImg);
		if(labImg)
			muReleaseImage(&labImg);
		if(scaleImg)
			muReleaseImage(&scaleImg);
	}

	muGetSubImage(yImg, subYImg, cropRect);
	muGetRGBSubImage(in, subRGBImg, cropRect);

	if(edgeImg)
		muReleaseImage(&edgeImg);
	if(otsuImage)
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
	int ret, bmpFlag = 0;
	char buf[MAX_LEN];
	char tempBuf[TEMP_LEN];
	char *tempName;
	char aeTestResult[16], awbTestResult[16], afTestResult[16];
	muImage_t *inImage;
	muSize_t size, nSize;
	locateImage_t *locateImg;
	MU_64F bm, avgY;
	aeInfo_t info;
	hsvData_t hsv;
	int fail = 0;
	FILE *img, *report;
	
	if(arg.imageName == NULL)
		return -1;

	ret = checkExtension(arg.imageName, ".jpg");
	if(ret)
	{
		ret = checkExtension(arg.imageName, ".bmp");
		if(ret)
		{
			logError("error input image :%s must a jpg or bmp format\n", arg.imageName);
			return -1;
		}
		bmpFlag = 1;
	}
	ret = fileExist(arg.imageName);
	if(ret)
		return -1;

	memset(aeTestResult, 0, sizeof(char)*16);
	memset(afTestResult, 0, sizeof(char)*16);
	memset(awbTestResult, 0, sizeof(char)*16);
	memset(buf, 0, sizeof(char)*MAX_LEN);
	sprintf(buf, "%s.\\prebuilt\\identify.exe %s 2>&1", gAbsPath, arg.imageName);
	size = getResolution(buf);
	
	if(size.width == -1 || size.height == -1)
	{
		logError("cannot identify image\n");
		return -1;
	}

	//record csv init
	report = resultReport(gAbsPath);
	genInitFolder(gAbsPath);
	nSize = getNewResolution(size);

	//transfer the original image to bmp
	memset(buf, 0, sizeof(char)*MAX_LEN);
	memset(tempBuf, 0, sizeof(char)*TEMP_LEN);
	if(!bmpFlag)
		tempName = getRealFileName(arg.imageName, ".jpg");
	else
		tempName = getRealFileName(arg.imageName, ".bmp");

	sprintf(tempBuf, "%s.bmp", tempName);
	sprintf(buf, "%s.\\prebuilt\\magick.exe %s -type truecolor -resize %d!x%d! %s 2>&1", gAbsPath, arg.imageName, nSize.width, nSize.height, tempBuf);
	ret = runCommand(buf, "error");
	if(ret)
	{
		logInfo("transfer bmp failed\n");
		return -1;
	}

	//locate af image
	inImage = muLoadBMP(tempBuf);
	locateImg = locatePattern(inImage, IMAGE_MODE);
	sprintf(awbTestResult, "PASS");
	sprintf(aeTestResult, "PASS");
	sprintf(afTestResult, "PASS");

	bm = afCheck(locateImg->subYImg, IMAGE_MODE, 1);
	if(bm > AF_TH)
	{
		fail = 1;
		logError("AF:FAIL");
		sprintf(afTestResult, "FAIL");
	}

	info = aeCheck(locateImg->yImg);
	if(info.hisSD < AE_TH)
	{
		fail = 1;
		logError("AE:FAIL");
		sprintf(aeTestResult, "FAIL");
	}

	hsv = awbCheck(locateImg->subRGBImg);
	if(hsv.avgS > AWB_TH_2)
	{
		fail = 1;
		logError("AWB:FAIL");
		sprintf(awbTestResult, "FAIL");
	}

	fprintf(report, "%s,NA,%f,%f,%f,%s,%s,%s,NA\n", arg.imageName, bm, info.hisSD, hsv.avgS, afTestResult, aeTestResult, awbTestResult);
	logInfo("%s: bm:%f std:%f s:%f\n", arg.imageName, bm, info.hisSD, hsv.avgS);

	if(fail)
		copyFile(arg.imageName, gFailPath);
	

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
	if(report)
		reportFinish(report);
	return 0;
}



static labInfo_t getAvgLab(muImage_t *labImg)
{
	labInfo_t info;
	MU_32F *buf;
	MU_32F area;
	MU_32S i;

	info.a = 0; info.l = 0; info.b = 0;
	buf = (MU_32F *)labImg->imagedata;
	area = labImg->width*labImg->height;
	for(i=0; i<labImg->width*labImg->height*labImg->channels; i+=labImg->channels)
	{
		info.l+=buf[i]; info.a+=buf[i+1]; info.b+=buf[i+2];
	}
	info.l = (info.l/area);
	info.a = (info.a/area);
	info.b = (info.b/area);

	return info;
}

static MU_64F calDeltaE(labInfo_t pInfo, labInfo_t cInfo)
{
	MU_64F l1,l2,a1,a2,b1,b2;
	MU_64F deltaE;
	l1 = pInfo.l; a1 = pInfo.a; b1 = pInfo.b;
	l2 = cInfo.l; a2 = cInfo.a; b2 = cInfo.b;
	deltaE = sqrt(pow((l2-l1),2) + pow((a2-a1),2) + pow((b2-b1),2));
	return deltaE;
}

static MU_64F getDeltaE(muImage_t *curImg, muImage_t *preImg)
{
	MU_64F deltaE;
	labInfo_t cInfo, pInfo;
	muImage_t *cXyzImg, *pXyzImg;
	muImage_t *cLabImg, *pLabImg;
	cXyzImg = muCreateImage(muSize(curImg->width, curImg->height), MU_IMG_DEPTH_32F, 3);
	cLabImg = muCreateImage(muSize(curImg->width, curImg->height), MU_IMG_DEPTH_32F, 3);
	pXyzImg = muCreateImage(muSize(preImg->width, preImg->height), MU_IMG_DEPTH_32F, 3);
	pLabImg = muCreateImage(muSize(preImg->width, preImg->height), MU_IMG_DEPTH_32F, 3);

	muRGB2XYZ(curImg, cXyzImg);
	muXYZ2LAB(cXyzImg, cLabImg);

	muRGB2XYZ(preImg, pXyzImg);
	muXYZ2LAB(pXyzImg, pLabImg);
	cInfo = getAvgLab(cLabImg);
	pInfo = getAvgLab(pLabImg);

	deltaE = calDeltaE(pInfo, cInfo);

	if(cXyzImg)
		muReleaseImage(&cXyzImg);
	if(cLabImg)
		muReleaseImage(&cLabImg);
	if(pXyzImg)
		muReleaseImage(&pXyzImg);
	if(pLabImg)
		muReleaseImage(&pLabImg);

	return deltaE;
}

static int motionDetection(muImage_t *preImg, muImage_t *curImg, int th)
{
	int i;
	int sum = 0;
	int magnitude;
	MU_8U *cur, *pre;
	pre = (MU_8U *)preImg->imagedata;
	cur = (MU_8U *)curImg->imagedata;
	for(i=0; i<preImg->width*preImg->height; i++)
		sum  += abs(pre[i]-cur[i]);

	magnitude = sum/(MU_64F)(preImg->width*preImg->height);
	if(magnitude > th)
		return 1;
	else
		return 0;
}

typedef struct _videoResult
{
	MU_64F bm, y, s;
}videoResult_t;

static muImage_t *getROI(muImage_t *yImg, muImage_t *rgbImg, muImage_t *bkImg)
{
	muImage_t *diffImg, *thImg, *eroImg, *diImg;
	muImage_t *labImg, *cropImg = NULL;
	muRect_t cropRect;
	muDoubleThreshold_t th;
	MU_8U numLabel;

	diffImg =  muCreateImage(muSize(yImg->width, yImg->height), MU_IMG_DEPTH_8U, 1);
	thImg = muCreateImage(muSize(yImg->width, yImg->height), MU_IMG_DEPTH_8U, 1);
	eroImg = muCreateImage(muSize(yImg->width, yImg->height), MU_IMG_DEPTH_8U, 1);
	diImg = muCreateImage(muSize(yImg->width, yImg->height), MU_IMG_DEPTH_8U, 1);
	labImg = muCreateImage(muSize(yImg->width, yImg->height), MU_IMG_DEPTH_8U, 1);
	muSetZero(diImg);

	muSub(yImg, bkImg, diffImg);
	th.min = 0; th.max = 255;
	muThresholding(diffImg, thImg, th);
	muErode33(thImg, eroImg);
	muDilate33(eroImg, diImg);

	mu4ConnectedComponent8u(diImg, labImg, &numLabel);
	if(numLabel > 1)
	{
		muSeq_t *seq = NULL;
		muSeqBlock_t *current;
		muBoundingBox_t *bp;
		muDoubleThreshold_t th;
		muPoint_t rpMin, rpMax;
		int maxArea = 0;

		th.min = 0;
		th.max = 0x3FFFFFFF;
		seq = muFindBoundingBox(labImg, numLabel, th);
		current = seq->first;
		while(current != NULL)
		{
			bp = (muBoundingBox_t *) current->data;
			if(bp->area > maxArea)
			{
				maxArea = bp->area;
				cropRect.x = bp->minx;
				cropRect.y = bp->miny;
				cropRect.width = bp->width;
				cropRect.height = bp->height;
			}
			current = current->next;
		}
		
		if((cropRect.width*cropRect.height) > (yImg->width*yImg->height*0.1))
		{
			hsvData_t avgHsv;
			if(cropRect.height > 80)
			{
				cropRect.height = cropRect.height - 80;
				cropRect.height = cropRect.height - (cropRect.height%4);
				cropRect.width = cropRect.width - (cropRect.width%4);
			}
			cropImg =  muCreateImage(muSize(cropRect.width, cropRect.height), MU_IMG_DEPTH_8U, 3);
			muSetZero(cropImg);
			muGetRGBSubImage(rgbImg, cropImg, cropRect);
			avgHsv = awbCheck(cropImg);
			if(avgHsv.avgV < BLACK_TH && avgHsv.avgS < BLACK_TH)
			{
				logInfo("black/abnormal scene detect!\n");
				muSaveBMP("debug_ab_bk_hsvChk_scene.bmp",rgbImg);
				cropImg = NULL;
			}
		}
		else
		{
			logInfo("black/abnormal scene detect!\n");
			muSaveBMP("debug_ab_bk_scene.bmp",rgbImg);
			cropImg = NULL;
		}

		if(seq)
			muClearSeq(&seq);

	}

	if(diffImg)
		muReleaseImage(&diffImg);
	if(thImg)
		muReleaseImage(&thImg);
	if(eroImg)
		muReleaseImage(&eroImg);
	if(diImg)
		muReleaseImage(&diImg);
	if(labImg)
		muReleaseImage(&labImg);

	return cropImg;
}

#define AE_CHECK	0x0001
#define AF_CHECK	0x0002
#define AWB_CHECK	0x0004
static videoResult_t video3aCheck(muImage_t *cropImg, int frameCount, int mode)
{
	MU_64F bm = 0;
	char buf[TEMP_LEN];
	locateImage_t *locateImg;
	aeInfo_t info;
	hsvData_t hsv;
	FILE *img;
	videoResult_t result;
	info.avgY = 0;
	info.hisSD = 0;
	hsv.avgH = 0; hsv.avgS = 0; hsv.avgV = 0;
	locateImg = locatePattern(cropImg, VIDEO_MODE);

	if(mode & AF_CHECK)
		bm = afCheck(locateImg->subYImg, VIDEO_MODE, frameCount);

	if(mode & AE_CHECK)
		info = aeCheck(locateImg->yImg);

	if(mode & AWB_CHECK)
		hsv = awbCheck(locateImg->subRGBImg);

	result.bm = bm; result.s = hsv.avgS; result.y = info.hisSD;

	if(locateImg->subRGBImg)
		muReleaseImage(&locateImg->subRGBImg);
	if(locateImg->subYImg)
		muReleaseImage(&locateImg->subYImg);
	if(locateImg->yImg)
		muReleaseImage(&locateImg->yImg);
	if(locateImg)
		free(locateImg);

	return result;
}


// only support yuv420p
static int videoCheck(char *rawFile, char *videoName, muSize_t size)
{
	MU_32U frameCount = 0;
	int previewCount = 1;
	previewScene_t psData, *ppsData;
	muSeq_t *psSeq = NULL;
	muSeqBlock_t *psCurrent;
	muPoint_t rpMin, rpMax;
	int fullFrameSize;
	int len, startFrameFlag = 0;
	char buf[TEMP_LEN];
	char aeTestResult[16], awbTestResult[16], afTestResult[16], flickTestResult[16];
	int firstFlag, motionFlag = 0, patternFlag = 0;
	char *tempName;
	FILE *fp, *img, *fw, *report;
	muImage_t *blackImg;
	muImage_t *yImg, *rgbImg, *rawImg;
	muImage_t *cMdImg, *pMdImg;
	muRect_t roi;
	muSearchMatching_t info;
	videoResult_t result, resultSum;
	long long seekidx;
	//locate the real stream for android camera preview
	psSeq = muCreateSeq(sizeof(previewScene_t));
	blackImg = muCreateImage(size, MU_IMG_DEPTH_8U, 1);
	yImg = muCreateImage(size, MU_IMG_DEPTH_8U, 1);
	rawImg = muCreateImage(size, MU_IMG_DEPTH_8U, 3);
	rgbImg  = muCreateImage(size, MU_IMG_DEPTH_8U, 3);
	roi.x = size.width/3;
	roi.y = (size.height/4)*3;
	roi.width = size.width/3;
	roi.height = size.height/4;
	cMdImg = muCreateImage(muSize(roi.width, roi.height), MU_IMG_DEPTH_8U, 1);
	pMdImg = muCreateImage(muSize(roi.width, roi.height), MU_IMG_DEPTH_8U, 1);
	memset(buf, 0, sizeof(char)*TEMP_LEN);
	tempName = getRealFileName(videoName, ".mp4");
	fullFrameSize = (size.width * size.height) * 1.5;
	fp = fopen(rawFile, "rb");
	report = resultReport(gAbsPath);
	genInitFolder(gAbsPath);
	while(1)
	{
		len = fread(rawImg->imagedata, 1, fullFrameSize, fp);
		if(feof(fp)) 
		{
			if(startFrameFlag == 1)
			{
				logInfo("preview last frame detection! last frameCount:%d\n",(frameCount+1));
				psData.endFrameCount = frameCount;
				psData.totalFrame = (psData.endFrameCount - psData.startFrameCount) + 1;
				psData.totalSecond = muRound(double(psData.totalFrame/(double)FRAMERATE));
				psData.pNum = previewCount;
				muPushSeq(psSeq, (MU_VOID *)&psData);
				previewCount++;
				startFrameFlag = 0;
			}
			break;
		}
		if(len != fullFrameSize)
		{
			logError("fread raw data error %d\n", len);
			fclose(fp);
			return -1;
		}
		//prepare Y data
		memcpy(yImg->imagedata, rawImg->imagedata, yImg->width*yImg->height*sizeof(MU_8U));
		
		muGetSubImage(yImg, cMdImg, roi);
		
		if(frameCount != 0)
		{
			//doing motion detection
			firstFlag = 0;
			motionFlag = motionDetection(pMdImg, cMdImg, 3);
		}
		else
		{
			memset(blackImg->imagedata, yImg->imagedata[0], blackImg->width*blackImg->height*sizeof(MU_8U));
			firstFlag = 1;
		}

		if(motionFlag || firstFlag || patternFlag )
		{
			muImage_t *hsvImg;
			muImage_t *isImg;
			muImage_t *eroImg, *diImg;
			muImage_t *subImg, *labImg;
			MU_16U *iBuf;
			MU_8U numLabel;
			int i,j;
			muYUV420toRGB(rawImg, rgbImg);
			hsvImg = muCreateImage(size, MU_IMG_DEPTH_16U, 3);
			isImg = muCreateImage(size, MU_IMG_DEPTH_8U, 1);
			eroImg = muCreateImage(size, MU_IMG_DEPTH_8U, 1);
			diImg = muCreateImage(size, MU_IMG_DEPTH_8U, 1);

			//image segmentation for preview icon detection 
			muRGB2HSV(rgbImg, hsvImg);
			iBuf = (MU_16U *)hsvImg->imagedata;
			memset(isImg->imagedata, 0, size.width*size.height*sizeof(MU_8U));
			memset(eroImg->imagedata, 0, size.width*size.height*sizeof(MU_8U));
			memset(diImg->imagedata, 0, size.width*size.height*sizeof(MU_8U));
			for(int i=0, j=0; i<size.width*size.height*3; i+=3, j++)
			{
				if((iBuf[i+2] >= 90) && (iBuf[i+1] <= 2))
					isImg->imagedata[j] = 255;
			}
			muErode33(isImg, eroImg);
			muDilate33(eroImg, diImg);
			subImg = muCreateImage(muSize(roi.width, roi.height), MU_IMG_DEPTH_8U, 1);
			muGetSubImage(diImg, subImg, roi);
			labImg = muCreateImage(muSize(roi.width, roi.height), MU_IMG_DEPTH_8U, 1);
			mu4ConnectedComponent8u(subImg, labImg, &numLabel);
			//maybe need to add more robust analysis label for criteria. 6+1=7 
			if(numLabel == 7)
			{
				muSeq_t *seq;
				muSeqBlock_t *current;
				muBoundingBox_t *bp;
				muDoubleThreshold_t th;
				int minx = 0x3FFFFFFF, miny=0x3FFFFFFF;
				int maxx = 0, maxy = 0;
				th.min = 0;
				th.max = 0x3FFFFFFF;
				seq = muFindBoundingBox(labImg, numLabel, th);
				current = seq->first;
				while(current != NULL)
				{
					bp = (muBoundingBox_t *) current->data;
					if(bp->minx < minx)
						minx = bp->minx;
					if(bp->miny < miny)
						miny = bp->miny;
					if(bp->maxx > maxx)
						maxx = bp->maxx;
					if(bp->maxy > maxy)
						maxy = bp->maxy;
					current = current->next;
				}
				muClearSeq(&seq);

				//find miny minx maxx maxy reconsturct the real position
				rpMin.x = minx+(size.width/3); rpMin.y = miny + ((size.height/4)*3);
				rpMax.x = maxx+(size.width/3); rpMax.y = maxy + ((size.height/4)*3);
				//logInfo("[%d] preview frame detection minx:%d miny:%d maxx:%d maxy:%d\n", (frameCount+1),rpMin.x, rpMin.y, rpMax.x, rpMax.y);
				
				if(patternFlag == 0)
				{
					logInfo("preview first frame detection! frameCount:%d\n", (frameCount+1));
					psData.startFrameCount = frameCount;
					startFrameFlag = 1;
				}
#if DEBUG_OUTPUT_PREVIEW_BMP
				muDrawRectangle(rgbImg, rpMin, rpMax, 'r');
#endif
				patternFlag = 1;		
			}
			else
			{
				if(patternFlag == 1)
				{
					logInfo("preview last frame detection! frameCount:%d\n",(frameCount+1));
					psData.endFrameCount = frameCount;
					psData.totalFrame = (psData.endFrameCount - psData.startFrameCount) + 1;
					psData.totalSecond = muRound(double(psData.totalFrame/(double)FRAMERATE));
					psData.pNum = previewCount;
					muPushSeq(psSeq, (MU_VOID *)&psData);
					previewCount++;
					startFrameFlag = 0;
				}
				patternFlag = 0;
			}

			if(hsvImg)
				muReleaseImage(&hsvImg);
			if(isImg)
				muReleaseImage(&isImg);
			if(eroImg)
				muReleaseImage(&eroImg);
			if(diImg)
				muReleaseImage(&diImg);
			if(subImg)
				muReleaseImage(&subImg);
			if(labImg)
				muReleaseImage(&labImg);
		}

		memcpy(pMdImg->imagedata, cMdImg->imagedata, pMdImg->width*pMdImg->height*sizeof(MU_8U));
#if DEBUG_OUTPUT_PREVIEW_BMP
		memset(buf, 0, sizeof(char)*TEMP_LEN);
		sprintf(buf, "device_%05d.bmp", frameCount);
		muSaveBMP(buf,rgbImg);
#endif
		frameCount++;
	}
	// close the stream
	fclose(fp);

	//analyze real video part
	psCurrent = psSeq->first;
	int totalFrame;
	int k = 0, count = 0;
	int startFlag;
	long long lenfp;
	int fail, everFail = 0;
	MU_64F deltaE;
	int deFlag, deCount, keepCount;
	muImage_t *cCropImg = NULL, *pCropImg = NULL;
	muSetZero(rgbImg);
	while(psCurrent != NULL)
	{
		ppsData = (previewScene_t *)psCurrent->data;
		logInfo("no:%d s-f: %d  e-f: %d total-f: %d total-s:%d\n",ppsData->pNum, ppsData->startFrameCount, ppsData->endFrameCount, ppsData->totalFrame, ppsData->totalSecond);
		totalFrame =  ppsData->totalFrame;
		fp = fopen(rawFile, "rb");
		seekidx = (long long)((long long)fullFrameSize*(long long)ppsData->startFrameCount);
		_fseeki64(fp, seekidx, SEEK_SET);
		lenfp = _ftelli64(fp);
		if(lenfp < 0)
		{
			logError("dump preview error :ftell:%ll\n", lenfp);
			exit(0);
		}
#if DEBUG_OUTPUT_PREVIEW_YUV
		memset(buf, 0, sizeof(char)*TEMP_LEN);
		sprintf(buf, "%d.yuv", ppsData->pNum);
		fw = fopen(buf, "wb");
#endif
		startFlag = 0;
		frameCount = ppsData->startFrameCount;
		resultSum.bm = 0;
		resultSum.s = 0;
		resultSum.y = 0;
		count = 0;
		deFlag = 0;
		keepCount = 0;
		deCount = 0;
		while(--totalFrame)
		{
			memset(aeTestResult, 0, sizeof(char)*16);
			memset(afTestResult, 0, sizeof(char)*16);
			memset(awbTestResult, 0, sizeof(char)*16);
			fail = 0;
			fread(rawImg->imagedata, 1, fullFrameSize, fp);
			memcpy(yImg->imagedata, rawImg->imagedata, yImg->width*yImg->height*sizeof(MU_8U));
			muYUV420toRGB(rawImg, rgbImg);
			cCropImg = getROI(yImg, rgbImg, blackImg);
			if(cCropImg == NULL)
			{
				if((ppsData->endFrameCount-1) == frameCount)
				{
					logError("Last preview frame is dark or abnormal\n");
					sprintf(afTestResult, "FAIL");
					sprintf(awbTestResult, "FAIL");
					sprintf(aeTestResult, "FAIL");
					logError("AWB:FAIL\n");
					logError("AF:FAIL\n");
					logError("AE:FAIL\n");
					fprintf(report, "%s,%d-final,%f,%f,%f,%s,%s,%s\n", videoName, (frameCount+1), result.bm, result.y, result.s, afTestResult, aeTestResult, awbTestResult);
					everFail = 1;
				}
				frameCount++;
				continue;
			}
			//af check by last FRAMERATE frames.
			if(startFlag)
			{
				//DeltaE-
				if((ppsData->endFrameCount - frameCount) > LAST_FRAME)
				{
					deltaE = getDeltaE(cCropImg, pCropImg);
					//AWB cast cehck
					if(deltaE > 2)
					{
						logInfo("deltaE = %f  fc = %d\n", deltaE, (frameCount+1));
						result = video3aCheck(cCropImg, (frameCount+1), AWB_CHECK);
						sprintf(afTestResult, "NA");
						sprintf(awbTestResult, "PASS");
						sprintf(aeTestResult, "PASS");
						sprintf(flickTestResult, "PASS");
						logInfo("AWB:%f\n", result.s);
						if(result.s > AWB_TH_1)
						{
							fail = 1;
							sprintf(awbTestResult, "FAIL");
							logError("AWB:FAIL\n");
							everFail = 1;
						}

						if(fail)
						{
							memset(buf, 0, sizeof(char)*TEMP_LEN);
							sprintf(buf, "%s\\%s_%d.bmp", gFailPath, tempName, (frameCount+1));
							logInfo("%s\n", buf);
							muSaveBMP(buf, rgbImg);
						}
						fprintf(report, "%s,%d,%f,%f,%f,%s,%s,%s,NA\n", videoName, (frameCount+1), result.bm, result.y, result.s, afTestResult, aeTestResult, awbTestResult);
					
						//flicker check
						deCount = 0;
						deFlag = 1;
						keepCount++;
						if(keepCount > FLICK_TH)
						{
							logError("FLICK:FAIL");
							sprintf(flickTestResult, "FAIL");
							everFail = 1;
						}
					}
					else
					{
						if(deFlag == 1)
							deCount++;
						if(deCount > 5)
						{
							deFlag = 0;
							keepCount = 0;
						}
					}
				}
				else
				{
					//avg
					result = video3aCheck(cCropImg, (frameCount+1), (AWB_CHECK|AE_CHECK|AF_CHECK));
					resultSum.bm += result.bm;
					resultSum.s += result.s;
					resultSum.y += result.y;
					count++;
					logInfo("last frames AWB:%f  AE:%f  AF:%f fc:%d\n",result.s, result.y, result.bm, (frameCount+1));
					if(frameCount == (ppsData->endFrameCount - 1))
					{
						logInfo("last Preview Frame!\n");
						sprintf(aeTestResult, "PASS");
						sprintf(afTestResult, "PASS");
						sprintf(awbTestResult, "PASS");
						result.bm = resultSum.bm/(double)count;
						result.s = resultSum.s/(double)count;
						result.y = resultSum.y/(double)count;
						if(result.bm > AF_TH)
						{
							fail = 1;
							logError("AF:FAIL\n");
							sprintf(afTestResult, "FAIL");
						}
						if(result.s > AWB_TH_2)
						{
							fail = 1;
							logError("AWB:FAIL\n");
							sprintf(awbTestResult, "FAIL");
						}
						if(result.y < AE_TH)
						{
							fail = 1;
							logError("AE:FAIL\n");
							sprintf(aeTestResult, "FAIL");
						}
						if(fail)
						{
							memset(buf, 0, sizeof(char)*TEMP_LEN);
							sprintf(buf, "%s\\%s_%d.bmp", gFailPath, tempName, (frameCount+1));
							logInfo("%s\n", buf);
							muSaveBMP(buf, rgbImg);
							everFail = 1;
						}

						fprintf(report, "%s,%d-final,%f,%f,%f,%s,%s,%s,%s\n", videoName, (frameCount+1), result.bm, result.y, result.s, afTestResult, aeTestResult, awbTestResult, flickTestResult);
					}
				}

				if(pCropImg)
					muReleaseImage(&pCropImg);
			}
			else // first frame
			{
				//AWB only
				sprintf(afTestResult, "NA");
				sprintf(awbTestResult, "PASS");
				sprintf(aeTestResult, "NA");
				result = video3aCheck(cCropImg, (frameCount+1), AWB_CHECK);
				logInfo("first frame AWB:%f\n", result.s);
				if(result.s > AWB_TH_1)
				{
					fail = 1;
					logError("AWB:FAIL\n");
					sprintf(awbTestResult, "FAIL");
				}
				
				if(fail)
				{
					memset(buf, 0, sizeof(char)*TEMP_LEN);
					sprintf(buf, "%s\\%s_%d.bmp", gFailPath, tempName, (frameCount+1));
					logInfo("%s\n", buf);
					muSaveBMP(buf, rgbImg);
					everFail = 1;
				}

				fprintf(report, "%s,%d,%f,%f,%f,%s,%s,%s,NA\n", videoName, (frameCount+1), result.bm, result.y, result.s, afTestResult, aeTestResult, awbTestResult);
			}

			startFlag = 1;
			pCropImg = muCreateImage(muSize(cCropImg->width,cCropImg->height), MU_IMG_DEPTH_8U, cCropImg->channels);
			memcpy(pCropImg->imagedata, cCropImg->imagedata, cCropImg->width*cCropImg->height*sizeof(MU_8U)*cCropImg->channels);
			if(cCropImg)
				muReleaseImage(&cCropImg);
			frameCount++;

#if DEBUG_OUTPUT_PREVIEW_YUV
			fwrite(rawImg->imagedata, 1, fullFrameSize, fw);
#endif
		} //while

#if DEBUG_OUTPUT_PREVIEW_YUV
		fclose(fw);
#endif
		fclose(fp);
		psCurrent = psCurrent->next;
	}

	char *timeName = getTimeName();
	char *tempRealName = getRealFileName(videoName, ".mp4");
	memset(buf, 0, sizeof(char)*TEMP_LEN);
	if(everFail)
	{
		sprintf(buf, "%s\\%s_%s.mp4", gFailPath, tempRealName, timeName);
		copyFile(videoName, buf);
	}
#if DEBUG_OUTPUT_TEST_FILE
	else
	{
		genTestFileFolder(gAbsPath);
		sprintf(buf, "%s\\%s_%s.mp4", gTestFilePath, tempRealName, timeName);
		copyFile(videoName, buf);
	}
#endif
	if(timeName)
		free(timeName);
	if(tempRealName)
		free(tempRealName);

	if(blackImg)
		muReleaseImage(&blackImg);
	if(yImg)
		muReleaseImage(&yImg);
	if(rawImg)
		muReleaseImage(&rawImg);
	if(rgbImg)
		muReleaseImage(&rgbImg);
	if(pMdImg)
		muReleaseImage(&pMdImg);
	if(cMdImg)
		muReleaseImage(&cMdImg);
	if(psSeq)
		muClearSeq(&psSeq);
	if(tempName)
		free(tempName);
	if(report)
		reportFinish(report);

	return 0;
}

int aaacheckVideo(catArg_t arg)
{
	int ret;
	char buf[MAX_LEN];
	char tempBuf[TEMP_LEN];
	char *tempName;
	muSize_t size;

	ret = fileExist(arg.videoName);
	if(ret)
		return -1;

	// get resolution and prepare the backgroud image to doing filter.
	memset(buf, 0, sizeof(char)*MAX_LEN);
	memset(tempBuf, 0, sizeof(char)*TEMP_LEN);
	
	tempName = getRealFileName(arg.videoName, ".mp4");
	sprintf(tempBuf, "%s.jpg", tempName);
	sprintf(buf, "%s.\\prebuilt\\ffmpeg.exe -ss 00:00:01 -i %s -frames:v 1 -y %s 2>&1", gAbsPath, arg.videoName, tempBuf);
	ret = runCommand(buf, "error");
	if(ret)
	{
		logInfo("transfer bmp failed\n");
		return -1;
	}
	if(tempName)
		free(tempName);

	ret = fileExist(tempBuf);
	if(ret)
		return -1;

	memset(buf, 0, sizeof(char)*MAX_LEN);
	sprintf(buf, "%s.\\prebuilt\\identify.exe %s", gAbsPath, tempBuf);
	size  = getResolution(buf);
	if(size.width == -1 || size.height == -1)
	{
		logError("get resolution error: %s\n", buf);
		return -1;
	}

	logInfo("Video Resolution: width:%d  height:%d\n", size.width, size.height);
	removeFile(tempBuf);

	// transfer the video clip to raw data as yuv420p
	memset(buf, 0, sizeof(char)*MAX_LEN);
	memset(tempBuf, 0, sizeof(char)*TEMP_LEN);
	
	tempName = getRealFileName(arg.videoName, ".mp4");
	sprintf(tempBuf, "%s.yuv", tempName);

	sprintf(buf, "%s.\\prebuilt\\ffmpeg.exe -i %s -c:v rawvideo -pix_fmt yuv420p -r %d -y %s 2>&1", gAbsPath, arg.videoName, FRAMERATE, tempBuf);
	ret = runCommand(buf, "error");
	if(ret)
	{
		logInfo("transfer mp4 to yuv420 failed\n");
		return -1;
	}

	if(tempName)
		free(tempName);

	ret = fileExist(tempBuf);
	if(ret)
		return -1;

	videoCheck(tempBuf, arg.videoName, size);

#if DEBUG_OUTPUT_RAW_FILE
#else
	removeFile(tempBuf);
#endif
	return 0;
}