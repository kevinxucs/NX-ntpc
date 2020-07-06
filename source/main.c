/* This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org>

Additionally, the ntp_packet struct uses code licensed under the BSD 3-clause. See LICENSE-THIRD-PARTY for more information. */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>

#include <switch.h>

 #define NTP_TIMESTAMP_DELTA 2208988800ull

/* ------------- BEGIN BSD 3-CLAUSE LICENSED CODE-------------------- */
// https://www.cisco.com/c/en/us/about/press/internet-protocol-journal/back-issues/table-contents-58/154-ntp.html
// Struct adapted from https://github.com/lettier/ntpclient , see LICENSE-THIRD-PARTY for more information.
typedef struct
{
  uint8_t li_vn_mode;      // li: two bits, leap indicator. vn: three bits, protocol version number. mode: three bits, client mode.
  
  uint8_t stratum;         // Stratum level of the local clock.
  uint8_t poll;            // Maximum interval between successive messages.
  uint8_t precision;       // Precision of the local clock.

  uint32_t rootDelay;      // Total round trip delay time.
  uint32_t rootDispersion; // Max error allowed from primary clock source.
  uint32_t refId;          // Reference clock identifier.

  uint32_t refTm_s;        // Reference time-stamp seconds.
  uint32_t refTm_f;        // Reference time-stamp fraction of a second.

  uint32_t origTm_s;       // Originate time-stamp seconds.
  uint32_t origTm_f;       // Originate time-stamp fraction of a second.

  uint32_t rxTm_s;         // Received time-stamp seconds.
  uint32_t rxTm_f;         // Received time-stamp fraction of a second.

  uint32_t txTm_s;         // Transmit time-stamp seconds.
  uint32_t txTm_f;         // Transmit time-stamp fraction of a second.
} ntp_packet;
/* ------------- END BSD 3-CLAUSE LICENSED CODE-------------------- */

void serviceCleanup(void)
{
    nifmExit();
    setsysExit();
    socketExit();
    timeExit();
    consoleExit(NULL);
}

void print(const char *msg)
{
    puts(msg);
    consoleUpdate(NULL);
}

void printWithArgs(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    vprintf(msg, args);
    va_end(args);
    consoleUpdate(NULL);
}

// We need system service access, not just user (the default)
TimeServiceType __nx_time_service_type = TimeServiceType_System;
int main(int argc, char **argv)
{
    const char *server_name = "172.31.1.1";
    const uint16_t port = 123;
    int sockfd = -1;

    consoleInit(NULL);

    Result rs = timeInitialize();
    if(R_FAILED(rs))
    {
        printWithArgs("Failed to init time services, error code %x\n", rs);
        goto done;
    }
    
    print("Time services initialized");

    rs = socketInitializeDefault();
    if(R_FAILED(rs))
    {
        printWithArgs("Failed to init socket services, error code %x\n", rs);
        goto done;
    }

    print("Socket services initialized");

    rs = nifmInitialize(NifmServiceType_User);
    if(R_FAILED(rs))
    {
        printWithArgs("Failed to init nifm services, with error code %x\n", rs);
        goto done;
    }

    NifmInternetConnectionStatus nifmICS;
    rs = nifmGetInternetConnectionStatus(NULL, NULL, &nifmICS);
    if(R_FAILED(rs))
    {
        printWithArgs("Failed to get internet connection status, with error code %x\nPlease ensure your console is connected to the internet, and try again.\n", rs);
        goto done;
    }

    if(nifmICS != NifmInternetConnectionStatus_Connected)
    {
        print("You're not connected to the internet. Please run this application again after connecting.");
        goto done;
    }

    ntp_packet packet;
    memset(&packet, 0, sizeof(ntp_packet));

    packet.li_vn_mode = (0 << 6) | (4 << 3) | 3; // LI 0 | Client version 4 | Mode 3 

    packet.txTm_s = htonl(NTP_TIMESTAMP_DELTA + time(NULL)); // Current networktime on the console

    struct sockaddr_in serv_addr;
    struct hostent *server;

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sockfd < 0)
    {
        printWithArgs("Failed to open socket with error code %x\n", errno);
        goto done;
    }

    print("Opened socket");
    printWithArgs("Attempting to connect to %s\n", server_name);
    errno = 0;
    if((server = gethostbyname(server_name)) == NULL)
    {
        printWithArgs("Gethostbyname failed: %x\n", errno);
        goto done;
    }

    memset(&serv_addr, 0, sizeof(struct sockaddr_in));
    
    serv_addr.sin_family = AF_INET;
    
    memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr_list[0], 4);
    
    serv_addr.sin_port = htons(port);

    errno = 0;
    int res = 0;
    if((res = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0)
    {
        printWithArgs("Connect failed: %x %x\n", res, errno);
        goto done;
    }

    printWithArgs("Connected to %s with result: %x %x\nSending time request...\n", server_name, res, errno);
    
    errno = 0;
    if((res = send(sockfd, (char *)&packet, sizeof(ntp_packet), 0)) < 0)
    {
        printWithArgs("Error writing to socket: %x %x\n", res, errno);
        goto done;
    }

    printWithArgs("Sent time request with result: %x %x, waiting for response...\n", res, errno);

    errno = 0;
    if((res = recv(sockfd, (char *)&packet, sizeof(ntp_packet), 0)) < sizeof(ntp_packet))
    {
        printWithArgs("Error reading from socket: %x %x\n", res, errno);
        goto done;
    }

    packet.txTm_s = ntohl(packet.txTm_s);

    time_t tim = (time_t) (packet.txTm_s - NTP_TIMESTAMP_DELTA);

    printWithArgs("Time received from server: %s\n", ctime((const time_t *)&tim));

    rs = timeSetCurrentTime(TimeType_NetworkSystemClock, (uint64_t)tim);
    if(R_FAILED(rs))
    {
        printWithArgs("Failed to set NetworkSystemClock, %x\n", rs);
    }
    else
        print("Successfully set NetworkSystemClock\n");

    rs = setsysInitialize();
    if(R_FAILED(rs))
    {
        printWithArgs("setsysInitialize failed, %x\n", rs);
        goto done;
    }

    bool internetTimeSync;

    rs = setsysIsUserSystemClockAutomaticCorrectionEnabled(&internetTimeSync);
    if(R_FAILED(rs))
    {
        printWithArgs("Unable to detect if internet time sync is enabled, %x\n", rs);
        goto done;
    }

    if(internetTimeSync == false)
    {
        print("Internet time sync is not enabled (enabling it will correct the time on the home screen, and this prompt will not appear again).\nPress A to enable internet time sync (and restart the console), or PLUS to quit.");
        while(appletMainLoop())
        {
            hidScanInput();

            u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

            if (kDown & KEY_PLUS) goto cleanup;
            if (kDown & KEY_A) 
            {
                rs = setsysSetUserSystemClockAutomaticCorrectionEnabled(true);
                if(R_SUCCEEDED(rs))
                {
                    serviceCleanup();
                    bpcInitialize();
                    bpcRebootSystem();
                    bpcExit();
                }

                printWithArgs("Unable to enable internet time sync, %x\n", rs);
            }

            consoleUpdate(NULL);
        }
    }

done:
    print("Press PLUS to quit.");

    while(appletMainLoop())
    {
        hidScanInput();

        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

        if (kDown & KEY_PLUS) break;

        consoleUpdate(NULL);
    }
cleanup:
    if(sockfd != -1)
        close(sockfd);
    serviceCleanup();
    return 0;
}
