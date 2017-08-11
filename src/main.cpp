#include "cat.h"


static int argToInt(const char* arg, int min, int max, int defalt, const char *opt)
{
	int i = defalt;
	int rv;

	if(!arg) goto done;

	rv = sscanf(arg, "%d", &i);
	if(rv != 1)
	{
		fprintf(stderr, "%s: Integer argument required. \n", opt);
		i = defalt;
		goto done;
	}
	if(i < min || max < i)
	{
		fprintf(stderr, "%s: argument out of integer range. \n", opt);
		i = defalt;
		goto done;
	}

done:
	return i;
}


static void RGBParser(char *rgbArg, catArg_t *catArg)
{
	char *pch;
	int i=0;
	pch = strtok(rgbArg,",");
	while(pch != NULL)
	{	
		if(i==0)
			catArg->setR =atoi(pch);
		else if(i==1)
			catArg->setG = atoi(pch);
		else
			catArg->setB = atoi(pch);

		pch = strtok(NULL,",");
		i++;
	}
	
}

static void RGBWParser(char *rgbwArg, catArg_t *catArg)
{
	char *pch;
	int i=0;
	pch = strtok(rgbwArg,",");
	while(pch != NULL)
	{	
		if(i==0)
			catArg->setR =atoi(pch);
		else if(i==1)
			catArg->setG = atoi(pch);
		else if(i==2)
			catArg->setB = atoi(pch);
		else if(i==3)
			catArg->setW = atoi(pch);
		else
		{
			if(!strcmp(pch, "ww"))
				catArg->wct = WARMWHITE;
			if(!strcmp(pch, "nw"))
				catArg->wct = NATUREWHITE;
			if(!strcmp(pch, "bw"))
				catArg->wct = BLUEWHITE;
		}

		pch = strtok(NULL,",");
		i++;
	}
	
}


static void help()
{
	printf("cat - camera autotest tool: %s Author: MTK06068, Debug:%d \n", CAT_VERSION, DEBUG);
	printf(
"Usage: cat.exe [OPTION]\n"
"    control camBox must give a comport number, e.g. cat.exe --com 4\n"
"    --help[-h]\thelp menu\n"
"    --com[-u]\tcom port number e.g. --cd 40 --com 4\n"
"    --ctl[-t]\tcontrol color temperature --ctl 1-13, e.g. --ctl 7 --com 4. [1]:315mm,4000K+20Lux [2]:315mm,4000K+500Lux [3]:315mm,5300K+20Lux\n"
"             \t[4]:315mm,5300K+100Lux [5]:315mm,5300K+500Lux [6]:315mm,5300K+700Lux [7]:315mm,2700K+300Lux [8]:343mm,4000K+500Lux [9]:343mm,5300K+500Lux\n"
"             \t[10]:343mm,2700K+300Lux [11]:570mm,4000K+200Lux [12]:570mm,5300K+200Lux [13]:570mm,2700K+150Lux [14-20]:315mm,2700K+0,50,100,200,400,700,1000Lux\n"
"             \t[21-27]:315mm,4000K+0,50,100,200,400,700,1000Lux [28-34]:315mm, 5300K+0,50,100,200,400,700,1000Lux [35]:343mm,2700K+700Lux [36]:343mm,4000K+700Lux\n"
"             \t[37]:343mm,5300K+700Lux [38-40]:570mm,2700K+100,200,600Lux [41-43]:570mm,4000K+100,200,600Lux [44-46]:570mm,5300K+100,200,600Lux\n"
"    --cd[-d]\tcontrol chart distance to the specific length e.g. --cd 343 unit:mm 3 supported distance :(RL2188) 1~2m(315mm), infinity(343mm), 570mm\n"
"    --oa[-j]\tcontrol light sourece to D:315, CT:4000K, LUX:500 e.g. --com 4 --oa 2. option: 1:Dark(Lux:20), 2:Bright (only support executing alone)\n"
"    --gd[-s]\tget cambox ultrasonic distance\n"
"    --gr[-r]\tget R value from color sensor\n"
"    --gg[-g]\tget G value from color sensor\n"
"    --gb[-b]\tget B value from color sensor\n"
"    --gi[-i]\tget I value from color sensor\n"
"    --cl[-e]\tctrl LED of color sensor 1:ON 0:OFF e.g. --cl 1 --com 4\n"
"    --cs[-c]\tcolor sensor get data (only support executing alone)  e.g. --csc --com 4 \n"
"    --rgb[-m]\tset RGB led value e.g. --com 4 --rgb 55,88,230\n"
"    --rgbw[-w]\tset RGBW led value e.g. --com 4 --rgbw 55,88,220,255,nw three type white including ww,nw,bw\n"
"    --pi[-p]\tget photo interrupt signal 0~1023 e.g. --com 4 --pi\n"
"    --fr[-f]\tlet chart to the reset position e.g. --com 4 --fr\n"
"    --cal[-l]\tcalibration the RGBW LED meet the target Lux e.g. --com 4 --cal\n"
"    --video[-v]\t auto check 3A for case video e.g. cat.exe --chip 95 --video d:\\testVideo.mp4 (only execute without cam box controlling)\n"
"    --image[-a]\t auto check 3A for image e.g. cat.exe --chip 55 --image d:\\testCapture.jpg (only execute without cam box controlling)\n"
	);
}

// --------------------------------------------------------
// long name usage > --lux 400
// short name usage > -l 400
// --------------------------------------------------------

int _tmain(int argc, TCHAR** argv)
{
	catArg_t catArg;
	int modeFlag = 0;
	int specFlag = 0;
	int temp;
	char comPort[16];
	char buf[TEMP_LEN];
	int com;
	char *absPath;
	int idx;

	// -- for debugging
	/*
	argc = 3;
	argv[0] = "cat";
	argv[1] = "--video";
	argv[2] = "D:\\Project\\Windows\\cat\\Debug\\FalseAlarm\\Bianco-camera067.mp4";
	*/

	if(argc < 2)
	{
		help();
		return -1;
	}

	memset(&catArg, 0, sizeof(catArg_t));
	memset(comPort, 0, sizeof(char)*16);

	while(1)
	{
		int optionIndex = 0;
		static struct option longOptions[] = {
			{_T("ctl"), ARG_REQ, 0, 't'},
			{_T("oa"), ARG_REQ, 0, 'j'},
			{_T("cd"), ARG_REQ, 0, 'd'},
			{_T("com"), ARG_REQ, 0, 'u'},
			{_T("gd"), ARG_NULL, 0, 's'},
			{_T("gr"), ARG_NULL, 0, 'r'},
			{_T("gg"), ARG_NULL, 0, 'g'},
			{_T("gb"), ARG_NULL, 0, 'b'},
			{_T("gi"), ARG_NULL, 0, 'i'},
			{_T("cl"), ARG_REQ, 0, 'e'},
			{_T("rgb"), ARG_REQ, 0, 'm'},
			{_T("rgbw"), ARG_REQ, 0, 'w'},
			{_T("cs"), ARG_NULL, 0, 'c'},
			{_T("pi"), ARG_NULL, 0, 'p'},
			{_T("fr"), ARG_NULL, 0, 'f'},
			{_T("cal"), ARG_NULL, 0, 'l'},
			{_T("video"), ARG_REQ, 0, 'v'},
			{_T("image"), ARG_REQ, 0, 'a'},
			{_T("help"), ARG_NULL, 0, 'h'},
			{ARG_NULL, ARG_NULL, ARG_NULL, ARG_NULL}
		};

		int c = getopt_long(argc, argv, _T("-t:j:d:u:srgbie:m:w:cpflv:a:h"), longOptions, &optionIndex);
		if(c == -1)
			break;

		switch(c)
		{
		case 's':
			modeFlag |= FLAG_CAMBOX_GET_DISTANCE;
			break;
		case 'r':
			modeFlag |= FLAG_CAMBOX_GET_R;
			break;
		case 'g':
			modeFlag |= FLAG_CAMBOX_GET_G;
			break;
		case 'b':
			modeFlag |= FLAG_CAMBOX_GET_B;
			break;
		case 'i':
			modeFlag |= FLAG_CAMBOX_GET_I;
			break;
		case 'e':
			catArg.setLED = atoi(optarg);
			modeFlag |= FLAG_CAMBOX_LED_CTRL;
			break;
		case 'm':
			RGBParser(optarg, &catArg);
			modeFlag |= FLAG_CAMBOX_SET_RGB;
			break;
		case 'w':
			RGBWParser(optarg, &catArg);
			modeFlag |= FLAG_CAMBOX_SET_RGBW;
			if((catArg.wct != WARMWHITE && catArg.wct != NATUREWHITE) && (catArg.wct != BLUEWHITE))
			{
				logError("rgbw only support three types color temperature ww,nw,bw\n");
				return 1;
			}
			break;
		case 'l':
			modeFlag |= FLAG_CAMBOX_RGBW_CAL;
			break;
		case 'd':
			catArg.distance = atoi(optarg);
			if((catArg.distance != 315) && (catArg.distance != 343) && (catArg.distance != 570))
			{
				logError("chart distance must in range 315,343,570 mm\n");
				return 1;
			}
			modeFlag |= FLAG_CAMBOX_CD;
			break;
		case 'u':
			com = atoi(optarg);
			sprintf(comPort, "\\\\.\\COM%d", com);
			modeFlag |= FLAG_CAMBOX_COM;
			break;
		case 'c':
			modeFlag |= FLAG_CAMBOX_COLOR_SENSOR;
			break;
		case 'p':
			modeFlag |= FLAG_CAMBOX_GET_PI;
			break;
		case 'f':
			modeFlag |= FLAG_CAMBOX_RESET_CD;
			break;
		case 'j':
		case 't':
			modeFlag |= FLAG_CAMBOX_CT;
			catArg.ctlEnv = atoi(optarg);
			if(catArg.ctlEnv > 46 || catArg.ctlEnv < 1)
			{
				logError("CT Type range is 1~46\n");
				return 1;
			}
			break;
		case 'v':
			modeFlag |= FLAG_3A_VIDEO;
			catArg.videoName = optarg;
			break;
		case 'a':
			modeFlag |= FLAG_3A_IMAGE;
			catArg.imageName = optarg;
			break;
		case 'h':
		default:
			help();
			return 1;
		}
	}
	
	if(modeFlag)
	{
		absPath = getAbsolutedPath(argv[0], "cat.exe");
		catInit(absPath);
	}

	memset(buf, 0, sizeof(char)*TEMP_LEN);
	for(int i=0; i<argc; i++)
	{
		strcat(buf, argv[i]);
		strcat(buf, " ");
	}
	logInfo("%s\n", buf);



	if((modeFlag & FLAG_CAMBOX_CD) || (modeFlag & FLAG_CAMBOX_RGBW_CAL) || (modeFlag & FLAG_CAMBOX_CT) || (modeFlag & FLAG_CAMBOX_GET_DISTANCE) 
		|| (modeFlag & FLAG_CAMBOX_GET_R) || (modeFlag & FLAG_CAMBOX_GET_G) || (modeFlag & FLAG_CAMBOX_GET_B) || (modeFlag & FLAG_CAMBOX_GET_I)
		|| (modeFlag & FLAG_CAMBOX_LED_CTRL) || (modeFlag & FLAG_CAMBOX_SET_RGB) || (modeFlag & FLAG_CAMBOX_COLOR_SENSOR) || (modeFlag & FLAG_CAMBOX_GET_PI)
		|| (modeFlag & FLAG_CAMBOX_RESET_CD) || (modeFlag & FLAG_CAMBOX_SET_RGBW))
	{
		if(modeFlag & FLAG_CAMBOX_COM)
		{
			if(modeFlag & FLAG_CAMBOX_COLOR_SENSOR)
			{
				colorSensor(catArg, comPort);
				deleteSerialPort(0);
			}
			else if(modeFlag & FLAG_CAMBOX_CT)
			{
				ctlEnv(catArg, comPort);
				deleteSerialPort(0);
			}
			else if(modeFlag & FLAG_CAMBOX_RGBW_CAL)
			{
				deleteSerialPort(0);
			}
			else
			{
				camBoxCtrl(modeFlag, catArg, comPort, specFlag);
			}
		}
		else
		{
			help();
			logExit();
			return 1;
		}
	}

	if( (modeFlag & FLAG_3A_VIDEO) || (modeFlag & FLAG_3A_IMAGE) )
	{
		if(modeFlag & FLAG_3A_VIDEO)
		{
			aaacheckVideo(catArg);
		}
		else if(modeFlag & FLAG_3A_IMAGE)
		{
			aaacheckImage(catArg);
		}
	}
	logInfo("log Finish\n");
	logExit();
	return 0;
}


