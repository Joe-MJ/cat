#ifndef __CAT_H__
#define __CAT_H__


#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <tchar.h>
#include <time.h>
#include <math.h>
#include "serialClass.h"
#include "getopt.h"
#include "logger.h"
#include "muGadget.h"

#define CAT_VERSION	"0.0.1"
#define CAMBOX_ENV_FILE	".camBoxEnv.cbe"
#define DEBUG						0
#define DEBUG_OUTPUT				0
#define DEBUG_OUTPUT_PREVIEW_BMP	0
#define DEBUG_OUTPUT_PREVIEW_YUV	0
#define DEBUG_OUTPUT_RAW_FILE		0
#define DEBUG_OUTPUT_TEST_FILE		1
#define __RGBWLED__

#define FLAG_CAMBOX_CD					0x00000001  // 0x01
#define FLAG_CAMBOX_GET_DISTANCE		0x00000002	// 0x02
#define FLAG_CAMBOX_GET_R				0x00000004  // 0x03
#define FLAG_CAMBOX_GET_G				0x00000008  // 0x04
#define FLAG_CAMBOX_GET_B				0x00000010  // 0x05
#define FLAG_CAMBOX_GET_I				0x00000020  // 0x06
#define FLAG_CAMBOX_LED_CTRL			0x00000040  // 0x07
#define FLAG_CAMBOX_COLOR_SENSOR		0x00000080  // 0x08
#define FLAG_CAMBOX_SET_RGB				0x00000100  // 0x09
#define FLAG_CAMBOX_GET_PI				0x00000200	// 0x0a
#define FLAG_CAMBOX_RESET_CD			0x00000400  // 0x0b
#define FLAG_CAMBOX_SET_RGBW			0x00000800	// 0x0c
#define FLAG_CAMBOX_COM					0x00001000
#define FLAG_CAMBOX_CT					0x00002000
#define FLAG_CAMBOX_RGBW_CAL			0x00004000  

#define FLAG_3A_VIDEO					0x00008000
#define FLAG_3A_IMAGE					0x00010000
#define FLAG_MCU_UPGRADE				0x00020000

#define MAX_LEN			512
#define TEMP_LEN		128
#define SEND_DATA_LEN	8
#define READ_DATA_LEN	8
#define NUM_LIGHT_SOURCE	47

// -----------------------------------------------
// Genreic Type Definition
// -----------------------------------------------

#define u8	unsigned char
#define u16 unsigned short
#define u32 unsigned int

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

// -----------------------------------------------

typedef struct _catArg
{
	int colorTemperature;
	int lux;
	int distance;
	int setLED;
	int setR, setG, setB, setW;
	int ctlEnv;
	int wct;
	int chipName;
	char *imageName;
	char *videoName;
	char *upgradeName;
}catArg_t;

typedef struct _ctlArg
{
	u8 r,g,b,w;	 // Pre-define RGBW value;
	int ct;
	int lux;
	int distance;

}ctlArg_t;

typedef struct _colorSensorInfo
{
	int r,g,b,i;
	double x,y,z;
	double ct, lux;
	int distance;
}colorSensorInfo_t;


typedef struct _camBoxInfo
{
	u16 getR, getG, getB, getI;
	u16 getDistance;
	int wct;
	double ct, lux;
}camBoxInfo_t;

enum
{
	HEADER = 0,
	CMDID,
	DATA1,
	DATA2,
	DATA3,
	DATA4,
	DATA5,
	CHKSUM,
};

enum
{
	WARMWHITE = 1,
	NATUREWHITE,
	BLUEWHITE,
};

typedef struct _camBoxProtocol
{
	u8 header, cmdId;
	u8 data1, data2, data3, data4, data5;
	u8 chkSum;
	u32 cmdFlag;
}camBoxProtocol_t;

extern char gAbsPath[MAX_LEN];
extern char gFailPath[MAX_LEN];
extern char gTestFilePath[MAX_LEN];

extern int camBoxCtrl(int mode, catArg_t arg, const char *comPort, int specFlag);
extern char* getAbsolutedPath(char *cmdPath, char *exeName);
extern void catInit(char *path);
extern colorSensorInfo_t colorSensor(catArg_t catArg, const char *comPort);
extern void deleteSerialPort(int specFlag);
extern void ctlEnv(catArg_t catArg, const char *comPort);
extern ctlArg_t lightCtrlParser(int ctrlEnv);
extern int runCommand(char *cmd, char *delima);
extern int aaacheckImage(catArg_t arg);
extern int checkExtension(char *input, char *extension);
extern char *getRealFileName(char *fileName, char * extension);
extern int fileExist(char *fileName);
extern muSize_t getResolution(char *cmd);
extern void removeFile(char *file);
extern int aaacheckVideo(catArg_t arg);
extern FILE *resultReport(char *path);
extern void reportFinish(FILE *fp);
extern void genInitFolder(char *path);
extern void genTestFileFolder(char *path);
extern void copyFile(char *src, char *dst);
extern char *getTimeName();
extern void mucUpgrade(int com, char *path);
#endif