#include "cat.h"

char gAbsPath[MAX_LEN];

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
		logInfo("%s\n", pBuf);
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