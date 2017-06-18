#include "cat.h"

static u8 gSendBuf[SEND_DATA_LEN];
static u8 gReadBuf[READ_DATA_LEN];

static Serial* gSp = NULL;
//time out unit: second
static u8 timeOutTbl[14] = {0, 120, 10, 10, 10, 10, 3, 13, 5, 3, 3, 120, 3, 0}; 



static camBoxInfo_t gCamBoxInfo;

//32cm
static double cm1[3][3] = {{2.92616, -7.35237, 3.83898}, 
							{1.84299, -3.38579, 2.6661},
							{0.99752, -10.02967, 9.95196}};


//318mm lux 20~100, D65~A 
static double cm2[3][3] = {{3.00059, -13.73123, 8.39512},
							 {2.14345, -8.94551, 6.30941},
							{-0.80501, 2.94886, 2.37450}};

//318mm lux 20~1000 D65~A RGB LED XYZ
static double cm3[3][3] = {{2.71483, -10.45837, 6.0558},
							{1.93148, -4.57696, 3.03004},
							{-1.51323, 9.50851, -1.71343}};

//318mm lux 20~1000 2300~10000
static double cm4[3][3] = {{1.57611, 1.79912, -1.5127},
							{0.86927, 3.96509, -1.86646},
							{-0.7482, 1.28268, 2.76412}};

//585mm lux 20~550 2300~8000
static double cm5[3][3] = {{0.97773, 0.1736, -0.55704},
							{0.60195, 1.68652, -1.2221},
							{0.03858, 0.12519, 0.9182}};


//255mm lux 20~500 2300~6500
static double cm6[3][3] = {{2.1295, -2.4713, 1.06151},
							 {0.90166, -2.0155, 0.09866},
							 {1.55344, -7.03093, 4.26652}};

//255, 255, 255 - CL-200 - 255mm
static double cm7[3][3] = {{6.679509, 1.8970633, 4.342562},
							 {2.789014, 19.869798, -3.34689},
							 {-2.666435, -10.68905, 31.80875}};

//255, 255, 255 - CL-200 - 318mm
static double cm8[3][3] = {{4.930212, 1.5907046, 3.4238866},
							 {2.006961, 15.547711, -2.581033},
							 {-2.11539, -8.480372, 25.235584}};

//255, 255, 255 - CL-200 - 362mm
static double cm9[3][3] = {{4.789017, 1.3536197, 3.1135032},
							 {1.976266, 14.011146, -2.346842},
							 {-1.91130, -7.651287, 22.80044}};

//255, 255, 255 - CL-200 - 585mm
static double cm10[3][3] = {{2.540652, 0.7029146, 1.6284819},
							 {1.043436, 7.2903422, -1.223320},
							 {-0.99549, -3.985685, 11.884070}};

//RGB LED - 570
static double cm570[3][3] = {{1.76866, 0.40048, 1.11096},
							 {0.72413, 4.71856, -0.75041},
							 {-0.62571, -2.87466, 8.05153}};

//RGB LED - 315
static double cm315[3][3] = {{5.26443, 0.95385, 2.98806},
							 {2.15996, 12.55696, -2.00090},
							 {-1.67053, -7.67694, 21.50101}};

//RGB LED - 343
static double cm343[3][3] = {{4.19623, 0.90735, 2.61420},
							 {1.72008, 11.02884, -1.74668},
							 {-1.46752, -6.74935, 18.89464}};


//RGBW LED - 570
static double cmw570[3][3] = {{1.81113, 0.53624, 1.21757},
							 {0.73233, 4.56371, -0.60821},
							 {-0.67264, -2.41339, 8.11539}};

//RGBW LED - 315
static double cmw315[3][3] = {{4.48309, 1.29430, 3.03152},
							 {1.81676, 11.25199, -1.49848},
							 {-1.67169, -5.99656, 20.14855}};

//RGBW LED - 343
static double cmw343[3][3] = {{4.09517, 1.15818, 2.72797},
							 {1.65877, 10.14232, -1.34990},
							 {-1.50681, -5.40392, 18.16374}};



/* check sum 0xFF-(0xAA + CMD + DATA)+1 */
static u8 checkSum(u8 *buf)
{
	u8 ckSum;
	ckSum = 0xFF - (buf[0]+buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]) + 1;
	return ckSum;
}

static int flag2cmdId(int cmd)
{
	int index = 0;

	while((cmd>>=1))
	{
		index++;
	}
	
	return (index+1);

}

static int getIdFromModeFlag(int mode)
{
	int readBit = 1;
	int i;
	for(i=0; i<32; i++)
	{
		if(mode & readBit)
			return readBit;

		readBit<<=1;
	}
	return 0;
}

static int fillSendBuf(u8 *sendBuf, camBoxProtocol_t *info)
{
	sendBuf[HEADER] = info->header;
	sendBuf[CMDID] = info->cmdId;
	sendBuf[DATA1] = info->data1;
	sendBuf[DATA2] = info->data2;
	sendBuf[DATA3] = info->data3;
	sendBuf[DATA4] = info->data4;
	sendBuf[DATA5] = info->data5;
	info->chkSum = checkSum(sendBuf);
	sendBuf[CHKSUM] = info->chkSum;
	return 0;
}

static int processReadBuf(u8 *readBuf, camBoxProtocol_t info)
{
		u8 cmdIdAck;
		u16 getDistance = 0;
		u16 getR = 0;
		u16 getG = 0;
		u16 getB = 0;
		u16 getI = 0;
		u16 irupt = 0;

		logDebug("[HEADER]:%x [CMDID]:%x [DATA1]:%x [DATA2]:%x [DATA3]:%x [DATA4]:%x [DATA5]:%x [CHKSUM]:%x\n",  readBuf[HEADER],  readBuf[CMDID],  readBuf[DATA1],  readBuf[DATA2],  readBuf[DATA3], readBuf[DATA4], readBuf[DATA5], readBuf[CHKSUM]);
		cmdIdAck = (info.cmdId | 0x80);
		//analyze ack
		if(readBuf[0] != 0xAA)
		{
			if(readBuf[0] == 0xEE)
				logError("sending data error\n");
			else if(readBuf[0] == 0xBB)
				logError("cambox chart distance error\n");
			else if(readBuf[0] == 0xCC)
				logError("cmdid not support\n");
			
			return -1;
		}
		//analyze cmdid ack
		if(cmdIdAck != readBuf[CMDID])
		{
			logError("camBox ack error\n");
			return -1;
		}
		//check sum check;
		if(checkSum(readBuf) != readBuf[CHKSUM])
		{
			logError("chk sum error\n");
			return -1;
		}

		//parsing command
		switch (info.cmdFlag)
		{
			case FLAG_CAMBOX_GET_DISTANCE:
				getDistance = (readBuf[DATA2]<<8) | readBuf[DATA1];
				logInfo("distance = %d\n", getDistance);
				gCamBoxInfo.getDistance = getDistance;
				break;
			case FLAG_CAMBOX_GET_R:
				getR = (readBuf[DATA2]<<8) | readBuf[DATA1];
				//logInfo("get R = %d\n", getR);
				gCamBoxInfo.getR = getR;
				break;
			case FLAG_CAMBOX_GET_G:
				getG = (readBuf[DATA2]<<8) | readBuf[DATA1];
				//logInfo("get G = %d\n", getG);
				gCamBoxInfo.getG = getG;
				break;
			case FLAG_CAMBOX_GET_B:
				getB = (readBuf[DATA2]<<8) | readBuf[DATA1];
				//logInfo("get B = %d\n", getB);
				gCamBoxInfo.getB = getB;
				break;
			case FLAG_CAMBOX_GET_I:
				getI = (readBuf[DATA2]<<8) | readBuf[DATA1];
				//logInfo("get I = %d\n", getI);
				gCamBoxInfo.getI = getI;
				break;
			case FLAG_CAMBOX_COLOR_SENSOR:
				break;
			case FLAG_CAMBOX_GET_PI:
				irupt = (readBuf[DATA2]<<8) | readBuf[DATA1];
				logInfo("get photo interrupt = %d\n", irupt);
				break;
			default:
				logDebug("cmdFlag=%x cmdId:%x write command finished\n", info.cmdId, info.cmdFlag);
				break;
		}
}

static u8 *writeCmd(u8 *sendBuf, const char *comPort, int cmdId)
{

	int readResult;
	time_t start, end;
	double cost;
	//doing write
	if(!gSp)
	{
		gSp = new Serial(comPort);
		if (gSp->IsConnected())
			logInfo("camBox connected\n");
	}
	
	gSp->WriteData(sendBuf, SEND_DATA_LEN);

	//get timer
	time(&start);
	while(gSp->IsConnected())
	{
		memset(gReadBuf, 0, READ_DATA_LEN*sizeof(char));
		readResult = gSp->ReadData(gReadBuf, READ_DATA_LEN);
		// printf("Bytes read: (0 means no data available) %i\n",readResult);
		if(readResult)
		{
			logDebug("Read Ack: data length: %d\n", readResult);
			break;
		}
		//diff timer
		Sleep(100);
		time(&end);
		cost = difftime(end, start);
		if(cost > timeOutTbl[cmdId])
		{
			logError("camBox Time out: cmdID:%d \n", cmdId);
			break;
		}
	}

	return gReadBuf;
}

static void processCmd(int cmdFlag, catArg_t arg, const char *comPort)
{
	camBoxProtocol_t info;
	u8 *readBuf;
	u8 cmdIdAck;
	info.header = 0xAA;
	info.cmdId = flag2cmdId(cmdFlag);
	info.cmdFlag = cmdFlag;
	logDebug("host cmdID = %x\n", info.cmdId);
	info.data1 = 0;
	info.data2 = 0;
	info.data3 = 0;
	info.data4 = 0;
	info.data5 = 0;


	switch (cmdFlag)
	{
	case FLAG_CAMBOX_CD:
		info.data1 = (arg.distance & 0x000000FF);
		info.data2 = (arg.distance & 0x0000FF00) >> 8;
		//logInfo("data1:%x  data2:%x\n", info.data1, info.data2);
		break;
	case FLAG_CAMBOX_GET_DISTANCE:
		break;
	case FLAG_CAMBOX_GET_R:
		break;
	case FLAG_CAMBOX_GET_G:
		break;
	case FLAG_CAMBOX_GET_B:
		break;
	case FLAG_CAMBOX_GET_I:
		break;
	case FLAG_CAMBOX_LED_CTRL:
		if(arg.setLED == 1)
			info.data1 = 0xFF;
		else
			info.data1 = 0x00;
		break;
	case FLAG_CAMBOX_SET_RGB:
		info.data1 = arg.setR;
		info.data2 = arg.setG;
		info.data3 = arg.setB;
		printf("rgb: %d %d %d\n", info.data1, info.data2, info.data3);
		break;
	case FLAG_CAMBOX_SET_RGBW:
		info.data1 = arg.setR;
		info.data2 = arg.setG;
		info.data3 = arg.setB;
		info.data4 = arg.setW;
		info.data5 = arg.wct;
		printf("rgbw: %d %d %d %d %d\n", info.data1, info.data2, info.data3, info.data4, info.data5);
		break;
	case FLAG_CAMBOX_COLOR_SENSOR:
		break;
	case FLAG_CAMBOX_GET_PI:
		break;
	case FLAG_CAMBOX_RESET_CD:
		break;
	default:
		break;
	}
	fillSendBuf(gSendBuf, &info);
	//processRead
	readBuf = writeCmd(gSendBuf, comPort, info.cmdId);
	if(readBuf)
	{
		processReadBuf(readBuf, info);
	}
	else
	{
		logError("read camBox data error\n");
	}
	
}

void deleteSerialPort(int specFlag)
{
	if(gSp && (!specFlag))
	{
		delete gSp;
		gSp = NULL;
		logInfo("delete gSp ok!\n");
	}
}


int camBoxCtrl(int mode, catArg_t arg, const char *comPort, int specFlag)
{
	int readResult;
	u8 cmdIdAck;
	u8 chkSumAck;
	int cmdFlag;

	// disable the com port bit
	mode &= 0xFFFFEFFF;

	while(mode)
	{
		//decode mode
		cmdFlag = getIdFromModeFlag(mode);
		logDebug("cmdFlag = %x\n", cmdFlag);
		//processCmd
		processCmd(cmdFlag, arg, comPort);
		//done the command
		mode ^= cmdFlag;
	}
	deleteSerialPort(specFlag);
	return 1;
}

static colorSensorInfo_t calCTLux(camBoxInfo_t *cbInfo)
{
	double r,g,b, in;
	double x, y, z;
	double sx, sy;
	double n;
	int i,j;
	double map[3][3];
	colorSensorInfo_t sensorInfo;


	r = (double)cbInfo->getR;
	g = (double)cbInfo->getG;
	b = (double)cbInfo->getB;
	in = (double)cbInfo->getI;
#if defined(__RGBWLED__)
	switch (cbInfo->getDistance)
	{
	case 570:
		memcpy(map, cmw570, sizeof(map));
		logDebug("cmw570 selected\n");
		break;
	case 343:
		memcpy(map, cmw343, sizeof(map));
		logDebug("cmw343 selected\n");
		break;
	case 315:
		memcpy(map, cmw315, sizeof(map));
		logDebug("cmw315 selected\n");
		break;
	default:
		logError("sensor corelation matrix not support this distance:%d", cbInfo->getDistance);
	}
#else
	switch (cbInfo->getDistance)
	{
	case 570:
		memcpy(map, cm570, sizeof(map));
		logDebug("cm570 selected\n");
		break;
	case 343:
		memcpy(map, cm343, sizeof(map));
		logDebug("cm343 selected\n");
		break;
	case 315:
		memcpy(map, cm315, sizeof(map));
		logDebug("cm315 selected\n");
		break;
	default:
		logError("sensor corelation matrix not support this distance:%d", cbInfo->getDistance);
	}
#endif

	for(j=0; j<3; j++)
	{
		if(j == 0)
			 x = (r * map[j][0]) + (g * map[j][1]) + (b * map[j][2]);
		else if(j == 1)
			 y = (r * map[j][0]) + (g * map[j][1]) + (b * map[j][2]);
		else
			 z = (r * map[j][0]) + (g * map[j][1]) + (b * map[j][2]);
				
	}

	cbInfo->lux = y;
	sx = x/(x+y+z);
	sy = y/(x+y+z);

	//mccammy formular
	n = (sx - 0.332)/(0.1858 - sy);

	cbInfo->ct = (449)*(n*n*n)+(3525)*(n*n)+(6823.3)*(n)+5520.33;

	logInfo("AVG: [R]:%.0f [G]:%.0f [B]:%.0f [I]:%.0f\n",r, g, b, in);
	logInfo("AVG: [X]:%f [Y]:%f [Z]:%f [LUX]:%f, [CT]:%f\n",x, y, z, cbInfo->lux, cbInfo->ct);

	sensorInfo.r = cbInfo->getR; sensorInfo.g = cbInfo->getG; sensorInfo.b = cbInfo->getB;
	sensorInfo.i = cbInfo->getI; sensorInfo.x = x; sensorInfo.y = y; sensorInfo.z = z;
	sensorInfo.ct = cbInfo->ct; sensorInfo.lux = cbInfo->lux;

	return sensorInfo;
}

colorSensorInfo_t colorSensor(catArg_t catArg, const char *comPort)
{
	int specFlag = 1;
	int times = 5;
	colorSensorInfo_t sensorInfo;
	camBoxInfo_t sumCamBoxInfo = {0};

	camBoxCtrl(FLAG_CAMBOX_GET_DISTANCE, catArg, comPort, specFlag);
	for(int i=0; i<times; i++)
	{
		// get R
		camBoxCtrl(FLAG_CAMBOX_GET_R, catArg, comPort, specFlag);
		// get G
		camBoxCtrl(FLAG_CAMBOX_GET_G, catArg, comPort, specFlag);
		// get B
		camBoxCtrl(FLAG_CAMBOX_GET_B, catArg, comPort, specFlag);
		// get I
		camBoxCtrl(FLAG_CAMBOX_GET_I, catArg, comPort, specFlag);

		sumCamBoxInfo.getR += gCamBoxInfo.getR;
		sumCamBoxInfo.getG += gCamBoxInfo.getG;
		sumCamBoxInfo.getB += gCamBoxInfo.getB;
		sumCamBoxInfo.getI += gCamBoxInfo.getI;

	}
	gCamBoxInfo.getR = sumCamBoxInfo.getR/(double)times;
	gCamBoxInfo.getG = sumCamBoxInfo.getG/(double)times;
	gCamBoxInfo.getB = sumCamBoxInfo.getB/(double)times;
	gCamBoxInfo.getI = sumCamBoxInfo.getI/(double)times;
	//cal CT & Lux
	sensorInfo = calCTLux(&gCamBoxInfo);
	return sensorInfo;
}

void calRGBWLed(catArg_t catArg, const char *comPort)
{
	int specFlag = 1;
	colorSensorInfo_t sensorInfo;
	ctlArg_t lightArg;
	
	for(int i; i<NUM_LIGHT_SOURCE; i++)
	{
		//parsing the predefine distance CT & Lux by LUT
		lightArg = lightCtrlParser(i);
		catArg.distance = lightArg.distance;
		camBoxCtrl(FLAG_CAMBOX_CD, catArg, comPort, specFlag);
		gCamBoxInfo.getDistance = lightArg.distance;
		catArg.setR = lightArg.r;
		catArg.setG = lightArg.g;
		catArg.setB = lightArg.b;
		catArg.setW = lightArg.w;
		switch (lightArg.ct)
		{
		case 2700:
			catArg.wct = WARMWHITE;
			break;
		case 4000:
			catArg.wct = NATUREWHITE;
			break;
		case 6500:
			catArg.wct = BLUEWHITE;
			break;
		default:
			logError("not support CT\n");
		}
		camBoxCtrl(FLAG_CAMBOX_SET_RGBW, catArg, comPort, specFlag);
		sensorInfo = colorSensor(catArg, comPort);
		if(lightArg.lux >= sensorInfo.lux)
		{
			;//luxErrorRate = ((lightArg.lux/(double)sensorInfo.lux)-1)*100;
		}
	}

}


void ctlEnv(catArg_t catArg, const char *comPort)
{
	int specFlag = 1;
	int type;
	ctlArg_t lightArg;
	colorSensorInfo_t sensorInfo;
	double ctErrorRate, luxErrorRate;

	//parsing the predefine distance CT & Lux by LUT
	lightArg = lightCtrlParser(catArg.ctlEnv);

	// move chart to 315mm
	catArg.distance = lightArg.distance;
	camBoxCtrl(FLAG_CAMBOX_CD, catArg, comPort, specFlag);
	gCamBoxInfo.getDistance = lightArg.distance;
	// control RGB LED
	catArg.setR = lightArg.r;
	catArg.setG = lightArg.g;
	catArg.setB = lightArg.b;
	catArg.setW = lightArg.w;
#if defined(__RGBLED__)
	camBoxCtrl(FLAG_CAMBOX_SET_RGB, catArg, comPort, specFlag);
#elif defined(__RGBWLED__)
	switch (lightArg.ct)
	{
	case 2700:
		catArg.wct = WARMWHITE;
		break;
	case 4000:
		catArg.wct = NATUREWHITE;
		break;
	case 6500:
		catArg.wct = BLUEWHITE;
		break;
	default:
		logError("not support CT\n");
	}
	camBoxCtrl(FLAG_CAMBOX_SET_RGBW, catArg, comPort, specFlag);
#elif defined(__LEDBULBS__)
	;
#endif
	Sleep(200);
	// PI control begin
	// get color sensor and fine-tune again

	sensorInfo = colorSensor(catArg, comPort);

	//lightArg.ct // target
	if(lightArg.ct >= sensorInfo.ct)
	{
		ctErrorRate = ((lightArg.ct/(double)sensorInfo.ct)-1)*100;
	}
	else
	{
		ctErrorRate = ((sensorInfo.ct/(double)lightArg.ct)-1)*100;
	}

	logInfo("CT ERR = %f%%\n", ctErrorRate);

	if(lightArg.lux >= sensorInfo.lux)
	{
		luxErrorRate = ((lightArg.lux/(double)sensorInfo.lux)-1)*100;
	}
	else
	{
		luxErrorRate = ((sensorInfo.lux/(double)lightArg.lux)-1)*100;
	}
	logInfo("Lux ERR = %f%%\n", luxErrorRate);

}