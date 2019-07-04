## Project Description
Simple weather send and recieve application for TM4C1294XL. This project demonstrates multithreading, networking, clock, sensor I/O, ADC, software interrupt, and semaphore on TI-RTOS at the same time.

## Simpe-IoT-Application-for-TI-Tiva-C
This application acquires temperature from ADC (internal temperature sensor) using a timer interrupt every 100 milliseconds, takes the average for 2 seconds (i.e. 20 samples) in a software interrupt. Every 15 seconds, receive the current temperature from api.openweathermap.org in a task. Once the temperature is retrieved from OpenWeather, post a semaphore for sending the average internal temperature sensor and the one retrieved from openweather map to TestSocket.exe on 5011 port. 

## Specifications
* A multithreaded software architecture that running on TI/RTOS.
* The program uses EMAC system to connect to Ethernet. The system will acquire IP number over DHCP protocol and eventually netIPAddrHook() function will be executed. Inside the netIPAddrHook() function, the program activates HWI clock task with a 30 second period.
* Uses the clock HWI function (activated every 100 msecond) to sample the temperature from the internal temperature sensor.
* Timer hardware interrupt adds the ADC value to the array. Then it posts the SWI.
* The SWI checks if the array is full (if every 20 samples are in the array). If the array is full, it calculates the average and put in a global variable called averageTemp. Initializes the array. 
* httpTask is the task that connects api.openweathermap.org to retrieve the current weather temperature. Initially it will wait for a semaphore. (This semaphore is posted in the netIPAddrHook function)
* When httpTask finishes its job with openweather, a semaphore is posted to activate tcpSocketTask where a TCP Scoket connection is made to SocketTest server program to send the average temperature and the one obtained from openweathermap. 
