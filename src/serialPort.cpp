
#include "cat.h"

#define DEFAULTCOM	4



Serial::Serial(const char *portName)
{
    //We're not yet connected
    this->connected = false;

    //Try to connect to the given port throuh CreateFile
    this->hSerial = CreateFile(portName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

    //Check if the connection was successfull
    if(this->hSerial==INVALID_HANDLE_VALUE)
    {
        //If not success full display an Error
        if(GetLastError()==ERROR_FILE_NOT_FOUND){

            //Print Error if neccessary
            printf("ERROR: Handle was not attached. Reason: %s not available.\n", portName);

        }
        else
        {
            printf("ERROR!!!");
        }
    }
    else
    {
        //If connected we try to set the comm parameters
        DCB dcbSerialParams = {0};

        //Try to get the current
        if (!GetCommState(this->hSerial, &dcbSerialParams))
        {
            //If impossible, show an error
            printf("failed to get current serial parameters!");
        }
        else
        {
            //Define serial connection parameters for the arduino board
            dcbSerialParams.BaudRate=CBR_9600;
            dcbSerialParams.ByteSize=8;
            dcbSerialParams.StopBits=ONESTOPBIT;
            dcbSerialParams.Parity=NOPARITY;
            //Setting the DTR to Control_Enable ensures that the Arduino is properly
            //reset upon establishing a connection
            dcbSerialParams.fDtrControl = DTR_CONTROL_DISABLE;
			dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;

             //Set the parameters and check for their proper application
             if(!SetCommState(hSerial, &dcbSerialParams))
             {
                printf("ALERT: Could not set Serial Port parameters");
             }
             else
             {
                 //If everything went fine we're connected
                 this->connected = true;
                 //Flush any remaining characters in the buffers 
                 PurgeComm(this->hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
                 //We wait 2s as the arduino board will be reseting
                 Sleep(ARDUINO_WAIT_TIME);
             }
        }
    }

}

Serial::~Serial()
{
    //Check if we are connected before trying to disconnect
    if(this->connected)
    {
        //We're no longer connected
        this->connected = false;
        //Close the serial handler
        CloseHandle(this->hSerial);
    }
}

int Serial::ReadData(unsigned char *buffer, unsigned int nbChar)
{
    //Number of bytes we'll have read
    DWORD bytesRead;
    //Number of bytes we'll really ask to read
    unsigned int toRead;

    //Use the ClearCommError function to get status info on the Serial port
    ClearCommError(this->hSerial, &this->errors, &this->status);

    //Check if there is something to read
    if(this->status.cbInQue>0)
    {
        //If there is we check if there is enough data to read the required number
        //of characters, if not we'll read only the available characters to prevent
        //locking of the application.
        if(this->status.cbInQue>nbChar)
        {
            toRead = nbChar;
        }
        else
        {
            toRead = this->status.cbInQue;
        }

        //Try to read the require number of chars, and return the number of read bytes on success
        if(ReadFile(this->hSerial, buffer, toRead, &bytesRead, NULL) )
        {
            return bytesRead;
        }

    }

    //If nothing has been read, or that an error was detected return 0
    return 0;

}


bool Serial::WriteData(unsigned char *buffer, unsigned int nbChar)
{
    DWORD bytesSend;

    //Try to write the buffer on the Serial port
    if(!WriteFile(this->hSerial, (void *)buffer, nbChar, &bytesSend, 0))
    {
        //In case it don't work get comm error and return false
        ClearCommError(this->hSerial, &this->errors, &this->status);

        return false;
    }
    else
        return true;
}

bool Serial::IsConnected()
{
    //Simply return the connection status
    return this->connected;
}

static void PrintCommState(DCB dcb)
{
    //  Print some of the DCB structure values
    _tprintf( TEXT("\nBaudRate = %d, ByteSize = %d, Parity = %d, StopBits = %d\n"), 
              dcb.BaudRate, 
              dcb.ByteSize, 
              dcb.Parity,
              dcb.StopBits );
}

static HANDLE initComPort(int comPort)
{
	char pcCommPort[128];
	DCB dcb;
	HANDLE hCom;
	BOOL fSuccess;
	COMMTIMEOUTS timeouts;

	memset(pcCommPort, 0, 128*sizeof(char));
	sprintf(pcCommPort, "COM%d", comPort);

	//pcCommPort = TEXT(nameBuf); //  Most systems have a COM1 port

	//  Open a handle to the specified com port.
	hCom = CreateFile( pcCommPort,
		GENERIC_READ | GENERIC_WRITE,
		0,      //  must be opened with exclusive-access
		NULL,   //  default security attributes
		OPEN_EXISTING, //  must use OPEN_EXISTING
		NULL,      //  not overlapped I/O
		NULL ); //  hTemplate must be NULL for comm devices

	if (hCom == INVALID_HANDLE_VALUE) 
	{
		//  Handle the error.
		printf ("CreateFile failed with error %d.\n", GetLastError());
		return NULL;
	}

	//  Initialize the DCB structure.
	SecureZeroMemory(&dcb, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);

	//  Build on the current configuration by first retrieving all current
	//  settings.
	fSuccess = GetCommState(hCom, &dcb);

	if (!fSuccess) 
	{
		//  Handle the error.
		printf ("GetCommState failed with error %d.\n", GetLastError());
		return NULL;
	}

	PrintCommState(dcb);       //  Output to console

	//  Fill in some DCB values and set the com state: 
	//  57,600 bps, 8 data bits, no parity, and 1 stop bit.
	dcb.BaudRate = CBR_115200;     //  baud rate
	dcb.ByteSize = 8;             //  data size, xmit and rcv
	dcb.Parity   = NOPARITY;      //  parity bit
	dcb.StopBits = ONESTOPBIT;    //  stop bit
	dcb.fBinary = TRUE;
	dcb.fDtrControl = DTR_CONTROL_ENABLE;
	dcb.fRtsControl = RTS_CONTROL_ENABLE;
	dcb.fOutxCtsFlow = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	dcb.fDsrSensitivity = FALSE;
	dcb.fAbortOnError = TRUE;
	

	fSuccess = SetCommState(hCom, &dcb);

	if (!fSuccess) 
	{
		//  Handle the error.
		printf ("SetCommState failed with error %d.\n", GetLastError());
		return NULL;
	}
	
	timeouts.ReadIntervalTimeout =50;
	timeouts.ReadTotalTimeoutMultiplier = 50;
	timeouts.ReadTotalTimeoutConstant = 10;
	timeouts.WriteTotalTimeoutMultiplier = 50;
	timeouts.WriteTotalTimeoutConstant = 10;
	if (!SetCommTimeouts(hCom, &timeouts))
    // setting timeouts failed.
	

	//  Get the comm config again.
	fSuccess = GetCommState(hCom, &dcb);

	if (!fSuccess) 
	{
		//  Handle the error.
		printf ("GetCommState failed with error %d.\n", GetLastError());
		return NULL;
	}

	PrintCommState(dcb);       //  Output to console

	_tprintf (TEXT("Serial port %s successfully reconfigured.\n"), pcCommPort);
	return (hCom);
}

/*
int main(int argc, char *argv[])
{
	HANDLE hCom;
	unsigned char DataBuffer[7];
    DWORD dwBytesToWrite = 6;
    DWORD dwBytesWritten;
    BOOL bErrorFlag = FALSE;
	HANDLE uart_handle;
	DWORD uart_pid;
	OVERLAPPED ov;
	int length;

	hCom = initComPort(DEFAULTCOM);

	DataBuffer[0] = 0xAA;
	DataBuffer[1] = 0x01;
	DataBuffer[2] = 0x33;
	DataBuffer[3] = 0x55;
	DataBuffer[4] = 0x01;
	DataBuffer[5] = 0x89;


	 bErrorFlag = WriteFile( 
                    hCom,           // open file handle
                    DataBuffer,      // start of data to write
                    dwBytesToWrite,  // number of bytes to write
                    &dwBytesWritten, // number of bytes that were written
                    NULL);            // no overlapped structure

    if (FALSE == bErrorFlag)
    {
        printf("Terminal failure: Unable to write to file.\n");
    }
    else
    {
        if (dwBytesWritten != dwBytesToWrite)
        {
            // This is an error because a synchronous write that results in
            // success (WriteFile returns TRUE) should write all data as
            // requested. This would not necessarily be the case for
            // asynchronous writes.
            printf("Error: dwBytesWritten != dwBytesToWrite\n");
        }
        else
        {
            _tprintf(TEXT("Wrote %d bytes to successfully.\n"), dwBytesWritten);
        }
    }

	Sleep(1000);
	while(1)
	{
		memset(DataBuffer, 0, 7*sizeof(unsigned char));

		length = ReadFile(hCom, DataBuffer, 1, &dwBytesWritten, NULL);
		if(length < 0)
		{
			printf("read error\n");	
		}
		DataBuffer[6] = '\0';
		printf("%c\n", DataBuffer[0]);	
	}
		
	CloseHandle(hCom);
}
*/
