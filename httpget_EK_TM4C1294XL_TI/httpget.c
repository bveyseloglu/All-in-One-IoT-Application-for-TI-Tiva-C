
#include <string.h>

/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/System.h>

/* TI-RTOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/GPIO.h>
#include <ti/net/http/httpcli.h>

/* Example/Board Header file */
#include "Board.h"

#include <stdio.h> /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), connect(), sendto(), and recvfrom() */
#include <arpa/inet.h> /* for sockaddr_in and inet_addr() */
#include <stdlib.h> /* for atoi() and exit() */
#include <string.h> /* for memset() */
#include <unistd.h> /* for close() */

#include <stdbool.h>
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "inc/hw_types.h"
#include "driverlib/adc.h"
#include "driverlib/sysctl.h"

#include <xdc/std.h>
#include <xdc/cfg/global.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>

// api
#define HOSTNAME          "api.openweathermap.org"
#define REQUEST_URI       "/data/2.5/weather?q=Eskisehir,tr&appid=---&mode=xml" // WARNING: Replace "---" with your token.
#define USER_AGENT        "HTTPCli (ARM; TI-RTOS)"
#define RCVBUFSIZE 2048                 // Size of receive buffer
unsigned short echoServPort = 5011;     // Echo server port
char *servIP = "192.168.1.37";          // Server IP address (dotted quad)

// hold values
double tempOpenWeather = 0;
double tempInternalAverage = 0;
double tempInternalValues[20];
int    lastTempIndex = 0;

// etc
uint32_t ADCValues[4];



/*
 *  ======== printError ========
 */
void printError(char *errString, int code)
{
    System_printf("Error! code = %d, desc = %s\n", code, errString);
    //BIOS_exit(code);
}

void ShowErrorAndExit(char *errorMessage)
{
    perror(errorMessage);
    exit(1);
}

void initialize_ADC()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    SysCtlDelay(3);

    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);

    ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_TS | ADC_CTL_IE |
    ADC_CTL_END);

    ADCSequenceEnable(ADC0_BASE, 3);

    ADCIntClear(ADC0_BASE, 3);
}

void SendInfoToServer(double openWeatherTemp, double sensorTemp)
{
    int sock;                        /* Socket descriptor */
    struct sockaddr_in echoServAddr; /* Echo server address */
    char sendBuffer[RCVBUFSIZE];
    int round = 1;

    // set output string
    unsigned int echoStringLen = 100;      /* Length of string to echo */
    char echoString[100];    /* String to send to echo server */
    sprintf(echoString, "Internal Sensor: %.2lf\nOpenWeather.org: %.2lf\n", sensorTemp, openWeatherTemp);

    // send

    sprintf(sendBuffer, "%s", echoString);

    /* Create a reliable, stream socket using TCP */
    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        ShowErrorAndExit("socket() failed");

    /* Construct the server address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr));     /* Zero out structure */
    echoServAddr.sin_family      = AF_INET;             /* Internet address family */
    echoServAddr.sin_addr.s_addr = inet_addr(servIP);   /* Server IP address */
    echoServAddr.sin_port        = htons(echoServPort); /* Server port */

    /* Establish the connection to the echo server */
    if (connect(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
        ShowErrorAndExit("connect() failed");

    echoStringLen = strlen(echoString);          /* Determine input length */

    if (echoStringLen < 2) {
      echoStringLen = 3000;
    }
    /*  Send the string to the server */
    while (--round >= 0) {
      if (send(sock, sendBuffer, echoStringLen, 0) != echoStringLen)
          ShowErrorAndExit("send() sent a different number of bytes than expected");
    }

    close(sock);
}

void getOpenWeatherDataTaskFunc()
{
    bool moreFlag = false;
        char data[2000];
        int ret;
        int len;
        struct sockaddr_in addr;

        HTTPCli_Struct cli;
        HTTPCli_Field fields[3] = {
            { HTTPStd_FIELD_NAME_HOST, HOSTNAME },
            { HTTPStd_FIELD_NAME_USER_AGENT, USER_AGENT },
            { NULL, NULL }
        };

        System_printf("Sending a HTTP GET request to '%s'\n", HOSTNAME);
        System_flush();

        HTTPCli_construct(&cli);

        HTTPCli_setRequestFields(&cli, fields);

        ret = HTTPCli_initSockAddr((struct sockaddr *)&addr, HOSTNAME, 0);
        if (ret < 0) {
            printError("httpTask: address resolution failed", ret);
        }

        ret = HTTPCli_connect(&cli, (struct sockaddr *)&addr, 0, NULL);
        if (ret < 0) {
            printError("httpTask: connect failed", ret);
        }

        ret = HTTPCli_sendRequest(&cli, HTTPStd_GET, REQUEST_URI, false);
        if (ret < 0) {
            printError("httpTask: send failed", ret);
        }

        ret = HTTPCli_getResponseStatus(&cli);
        if (ret != HTTPStd_OK) {
            printError("httpTask: cannot get status", ret);
        }

        System_printf("HTTP Response Status Code: %d\n", ret);

        ret = HTTPCli_getResponseField(&cli, data, sizeof(data), &moreFlag);
        if (ret != HTTPCli_FIELD_ID_END) {
            printError("httpTask: response field processing failed", ret);
        }

        len = 0;
        do {
            ret = HTTPCli_readResponseBody(&cli, data, sizeof(data), &moreFlag);
            if (ret < 0) {
                printError("httpTask: response body processing failed", ret);
            }

            len += ret;
        } while (moreFlag);

        // read temp value
        int i;
        for (i = 0; i < len; i++) {
            if (data[i] == 't' && data[i+1] == 'e' && data[i+2] == 'm' && data[i+3] == 'p') // catch "temp" word
            {
                char tempVal[3];
                tempVal[0] = data[i+19] - 48;
                tempVal[1] = data[i+20] - 48;
                tempVal[2] = data[i+21] - 48;

                char tempValFraction[2];
                tempValFraction[0] = data[i+23] - 48;
                tempValFraction[1] = data[i+24] - 48;

                tempOpenWeather = tempVal[0] * 100 + tempVal[1] * 10 + tempVal[2] + tempValFraction[0] * 0.1 + tempValFraction[1] * 0.01 - 273; // celcius

                break;
            }
        }

        System_printf("Recieved %d bytes of payload\n", len);
        System_flush();

        HTTPCli_disconnect(&cli);
        HTTPCli_destruct(&cli);
}


/*
 *  ======== SWI ISR ========
 *  calculates the average of internal temp values
 */
Void swiAverage_ISR(UArg arg0, UArg arg1)
{

        while(!ADCIntStatus(ADC0_BASE, 3, false)) { }
        ADCIntClear(ADC0_BASE, 3);
        ADCSequenceDataGet(ADC0_BASE, 3, ADCValues);

        if (lastTempIndex < 20) {
            // yeni degeri yaz
            tempInternalValues[lastTempIndex++] = (double)(147.5-((75.0*3.3 *(float)ADCValues[0])) / 4096.0);
        }
        else {
            // average al
            double temporaryAvgTemp = 0;

            int i;
            for (i = 0; i < 20; i++)
                temporaryAvgTemp += tempInternalValues[i];

            tempInternalAverage = temporaryAvgTemp / 20;

            // sifirla
            lastTempIndex = 0;
            tempInternalValues[lastTempIndex++] = (double)(147.5-((75.0*3.3 *(float)ADCValues[0])) / 4096.0);
        }
}

/*
 *  ======== TASK FUNC ========
 *  reads weather and the stores it into 'tempOpenWeather'
 */
Void getOpenWeatherDataTask(UArg arg0, UArg arg1)
{
    for (;;) {
        Semaphore_pend(semaphoreOpenWeatherTask, BIOS_WAIT_FOREVER);
        getOpenWeatherDataTaskFunc();
        Semaphore_post(semaphoreSendWeatherTask);
    }
}

/*
 *  ======== TASK FUNC ========
 *  send temp value
 */
Void sendWeatherDataTask(UArg arg0, UArg arg1)
{
    for (;;) {
        Semaphore_pend(semaphoreSendWeatherTask, BIOS_WAIT_FOREVER);
        SendInfoToServer(tempOpenWeather, tempInternalAverage);
    }
}

/*
 *  ======== TIMER ISR ========
 *  reads temp value from adc and then stores it
 */
Void timerInternalADC_ISR(UArg arg0, UArg arg1)
{
    ADCProcessorTrigger(ADC0_BASE, 3);

    Swi_post(swiAverage);
}

/*
*  ======== TIMER ISR ========
*  receives the temp value and sends it every 15 secs.
*/
Void timerGetAndSendWeather_ISR(UArg arg0, UArg arg1)
{
        Semaphore_post(semaphoreOpenWeatherTask);
}

/*
 *  ======== netIPAddrHook ========
 *  This function is called when IP Addr is added/deleted
 */
void netIPAddrHook(unsigned int IPAddr, unsigned int IfIdx, unsigned int fAdd)
{
    static Task_Handle taskHandle, taskHandle2;
    Task_Params taskParams, taskParams2;
    Error_Block eb, eb2;

    /* Create a HTTP task when the IP address is added */
    if (fAdd && !taskHandle && !taskHandle2) {
        Error_init(&eb);

        Task_Params_init(&taskParams);
        taskParams.stackSize = 4096;
        taskParams.priority = 1;
        taskHandle = Task_create((Task_FuncPtr)getOpenWeatherDataTask, &taskParams, &eb);

        // obur task'i olustur
        Error_init(&eb2);
        Task_Params_init(&taskParams2);
        taskParams2.stackSize = 8000;
        taskParams2.priority = 1;
        taskHandle2 = Task_create((Task_FuncPtr)sendWeatherDataTask, &taskParams2, &eb2);

        if (taskHandle == NULL || taskHandle2 == NULL) {
            printError("netIPAddrHook: Failed to create HTTP Task\n", -1);
        }

        // start timers
        Timer_start(timerGetAndSendWeather);
        Timer_start(timerInternalADC);

    }
}

/*
 *  ======== main ========
 */
int main(void)
{
    /* Call board init functions */
    Board_initGeneral();
    Board_initGPIO();
    Board_initEMAC();

    /* Turn on user LED */
    GPIO_write(Board_LED0, Board_LED_ON);

    System_printf("Starting the HTTP GET example\nSystem provider is set to "
            "SysMin. Halt the target to view any SysMin contents in ROV.\n");
    /* SysMin will only print to the console when you call flush or exit */
    System_flush();

    // start ADC
    initialize_ADC();

    /* Start BIOS */
    BIOS_start();

    return (0);
}
