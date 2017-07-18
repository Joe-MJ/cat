#include "cat.h"

char gAbsPath[MAX_LEN];
char gFailPath[MAX_LEN];
char gTestFilePath[MAX_LEN];

char *getTimeName()
{
	time_t now;
	struct tm *ltm;
	char *timeName;
	time(&now);
	ltm = localtime(&now);
	timeName = (char *)malloc(sizeof(char)*TEMP_LEN);
	memset(timeName, 0, sizeof(char)*TEMP_LEN);

	sprintf(timeName, "%02d_%02d_%02d_%02d_%02d", (1 + ltm->tm_mon), ltm->tm_mday, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

	return timeName;
}

void copyFile(char *src, char *dst)
{
	char cmd[MAX_LEN];
	memset(cmd, 0, sizeof(char)*MAX_LEN);
	sprintf(cmd, "copy %s %s 2>&1", src, dst);
	runCommand(cmd, NULL);
	return;
}

void reportFinish(FILE *fp)
{
	if(fp)
	{
		fclose(fp);
	}
}

void genTestFileFolder(char *path)
{
	time_t now;
	char buf[TEMP_LEN];
	char timeName[TEMP_LEN];
	char cmd[MAX_LEN];
	struct tm *ltm;
	time(&now);
	ltm = localtime(&now);

	memset(buf, 0, sizeof(char)*TEMP_LEN);
	memset(timeName, 0, sizeof(char)*TEMP_LEN);
	memset(cmd, 0, sizeof(char)*MAX_LEN);
	memset(gTestFilePath, 0, sizeof(char)*MAX_LEN);

	sprintf(timeName, "%d%02d%02d", (1900 + ltm->tm_year), (1 + ltm->tm_mon), ltm->tm_mday);
	sprintf(buf, "%sTestFile_%s", path, timeName);
	logInfo("Test File path: %s\n", buf);
	strcpy(gTestFilePath, buf);

	sprintf(cmd, "md %s 2>&1", buf);
	runCommand(cmd, NULL);

	return;
}


void genInitFolder(char *path)
{
	time_t now;
	char buf[TEMP_LEN];
	char timeName[TEMP_LEN];
	char cmd[MAX_LEN];
	struct tm *ltm;
	time(&now);
	ltm = localtime(&now);

	memset(buf, 0, sizeof(char)*TEMP_LEN);
	memset(timeName, 0, sizeof(char)*TEMP_LEN);
	memset(cmd, 0, sizeof(char)*MAX_LEN);
	memset(gFailPath, 0, sizeof(char)*MAX_LEN);

	sprintf(timeName, "%d%02d%02d", (1900 + ltm->tm_year), (1 + ltm->tm_mon), ltm->tm_mday);
	sprintf(buf, "%sFail_%s", path, timeName);
	logInfo("Fail Picture path: %s\n", buf);
	strcpy(gFailPath, buf);

	sprintf(cmd, "md %s 2>&1", buf);
	runCommand(cmd, NULL);

	return;
}


FILE *resultReport(char *path)
{
	FILE *fp, *rfp = NULL;
	time_t now;
	char *buf;
	char timeName[TEMP_LEN];
	struct tm *ltm;
	time(&now);
	ltm = localtime(&now);
	buf = (char *)malloc(MAX_LEN*sizeof(char));
	memset(timeName, 0, TEMP_LEN*sizeof(char));
	memset(buf, 0, MAX_LEN*sizeof(char));
	sprintf(timeName, "%d%02d%02d", (1900 + ltm->tm_year), (1 + ltm->tm_mon), ltm->tm_mday);
	sprintf(buf, "%scatResult_%s.csv", path, timeName);
	logInfo("report name: %s\n", buf);
	rfp = fopen(buf, "r");
	if(rfp == NULL)
	{
		//First time to create report
		fp = fopen(buf, "w");
		if(fp == NULL)
		{
			logError("report file open failed\n");
			exit(0);
		}
		fprintf(fp, "Test File, Frame Num., BM, Y, S, AF, AE, AWB, FLICK\n");
	}
	else
	{
		//File Exist
		fp = fopen(buf, "a");
		if(fp == NULL)
		{
			logError("report file open failed\n");
			fclose(rfp);
			exit(0);
		}
		logInfo("report file already exist, overwrite them\n");
	}

	if(rfp)
		fclose(rfp);
	free(buf);

	return fp;
}


void removeFile(char *file)
{
	char cmd[256];
	memset(cmd, 0, sizeof(char)*256);
	sprintf(cmd, "del /F %s 2>&1", file);
	runCommand(cmd, NULL);
	return;
}

muSize_t getResolution(char *cmd)
{
	char pBuf[MAX_LEN];
	char *pch, *ppch, *epch;
	FILE *pipe;
	muSize_t size;
	int count = 0, ccount = 0;
	size.width = -1;
	size.height = -1;
	pipe = _popen(cmd, "rt");
	if(pipe == NULL)
	{
		logError("_popen failed\n");
		exit(0);
	}

	while(fgets(pBuf, MAX_LEN, pipe))
	{
		logInfo("%s\n", pBuf);
		epch = strstr(pBuf, "error");
		if(epch != NULL)
		{
			logError("identify error\n");
			break;
		}
		pch = strtok(pBuf, " ");
		while(pch != NULL)
		{
			if(count == 2)
			{
				ppch = strtok(pch, "x");
				while(ppch != NULL)
				{
					if(ccount == 0)
						size.width = atoi(ppch);
					else
						size.height = atoi(ppch);
					ccount++;
					ppch = strtok(NULL, "x");
				}
			}
			pch = strtok(NULL, " ");
			count++;
		}
	}
	logInfo("\n");
	if(!feof(pipe))
		logError("Error: Failed to read the pipe to the end\n");

	if(pipe)
		_pclose(pipe);

	return size;
}


int fileExist(char *fileName)
{
	FILE *fp;
	fp = fopen(fileName, "r");
	if(fp == NULL)
	{
		logInfo("%s file doesn't exits\n", fileName);
		return -1;
	}
	fclose(fp);
	return 0;
}

char *getRealFileName(char *fileName, char * extension)
{
	char *pch, *ppch;
	char *name;
	char temp[TEMP_LEN];
	int count = 0, ccount = 0;
	
	name = (char *)malloc(TEMP_LEN*sizeof(char));
	memset(name, 0, TEMP_LEN*sizeof(char));
	memset(temp, 0, TEMP_LEN*sizeof(char));
	strcpy(temp, fileName);
	pch = strtok(temp, "\\");
	while(pch != NULL)
	{
		ppch = strstr(pch, extension);
		if(ppch)
		{
			strncpy(name, pch, (strlen(pch)-strlen(ppch)));
		}
		pch = strtok(NULL, "\\");
	}
	logInfo("base name: %s\n", name);

	return name;
}

int checkExtension(char *input, char *extension)
{
	char *ret;

	ret = strstr(input, extension);
	if(ret == NULL)
	{
		return -1;
	}

	return 0;
}


int runCommand(char *cmd, char *delima)
{
	char pBuf[MAX_LEN];
	char *pch;
	FILE *pipe;
	
	logInfo("%s\n", cmd);
	pipe = _popen(cmd, "rt");
	if(pipe == NULL)
	{
		logError("_popen failed\n");
		exit(0);
	}
	while(fgets(pBuf, MAX_LEN, pipe))
	{
		//logInfo("%s\n", pBuf);
		if(delima != NULL)
		{
			pch = strstr(pBuf, delima);
			if(pch != NULL)
			{
				if(pipe)
					_pclose(pipe);
				return 1;
			}
		}
	}
	if(pipe)
		_pclose(pipe);
	return 0;
}

char* getAbsolutedPath(char *cmdPath, char *exeName)
{
	char *pch;
	memset(gAbsPath, 0, sizeof(char)*MAX_LEN);
	pch = strstr(cmdPath, "\\");
	if(pch == NULL)
	{
		return "";
	}
	else
	{
		pch = strstr(cmdPath, exeName);
		if(pch != NULL)
		{
			strncpy(gAbsPath, cmdPath, (strlen(cmdPath)-strlen(exeName)));
			return gAbsPath;
		}
	}
	return NULL;
}


void catExit()
{
	logExit();
	return;
}

void catInit(char *path)
{
#if DEBUG
	logInit(path, (LOG_MODE_FILE|LOG_MODE_CONSOLE), "cat", LOG_LEVEL_DONTCARE);
#else
	logInit(path, (LOG_MODE_FILE|LOG_MODE_CONSOLE), "cat", LOG_LEVEL_INFO);
#endif

}