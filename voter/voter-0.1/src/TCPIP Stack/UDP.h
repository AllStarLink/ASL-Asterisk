/*********************************************************************
 *
 *                  UDP Module Defs for Microchip TCP/IP Stack
 *
 *********************************************************************
 * FileName:        UDP.h
 * Dependencies:    StackTsk.h
 *                  MAC.h
 * Processor:       PIC18, PIC24F, PIC24H, dsPIC30F, dsPIC33F, PIC32
 * Compiler:        Microchip C32 v1.05 or higher
 *					Microchip C30 v3.12 or higher
 *					Microchip C18 v3.30 or higher
 *					HI-TECH PICC-18 PRO 9.63PL2 or higher
 * Company:         Microchip Technology, Inc.
 *
 * Software License Agreement
 *
 * Copyright (C) 2002-2009 Microchip Technology Inc.  All rights
 * reserved.
 *
 * Microchip licenses to you the right to use, modify, copy, and
 * distribute:
 * (i)  the Software when embedded on a Microchip microcontroller or
 *      digital signal controller product ("Device") which is
 *      integrated into Licensee's product; or
 * (ii) ONLY the Software driver source files ENC28J60.c, ENC28J60.h,
 *		ENCX24J600.c and ENCX24J600.h ported to a non-Microchip device
 *		used in conjunction with a Microchip ethernet controller for
 *		the sole purpose of interfacing with the ethernet controller.
 *
 * You should refer to the license agreement accompanying this
 * Software for additional information regarding your rights and
 * obligations.
 *
 * THE SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT
 * WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION, ANY WARRANTY OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * MICROCHIP BE LIABLE FOR ANY INCIDENTAL, SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF
 * PROCUREMENT OF SUBSTITUTE GOODS, TECHNOLOGY OR SERVICES, ANY CLAIMS
 * BY THIRD PARTIES (INCLUDING BUT NOT LIMITED TO ANY DEFENSE
 * THEREOF), ANY CLAIMS FOR INDEMNITY OR CONTRIBUTION, OR OTHER
 * SIMILAR COSTS, WHETHER ASSERTED ON THE BASIS OF CONTRACT, TORT
 * (INCLUDING NEGLIGENCE), BREACH OF WARRANTY, OR OTHERWISE.
 *
 *
 * Author               Date    Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Nilesh Rajbharti     3/19/01  Original        (Rev 1.0)
 ********************************************************************/
#ifndef __UDP_H
#define __UDP_H

// Stores a UDP Port Number
typedef WORD UDP_PORT;

// Provides a handle to a UDP Socket
typedef BYTE UDP_SOCKET;

// Stores information about a current UDP socket
typedef struct
{
    NODE_INFO   remoteNode;		// IP and MAC of remote node
    UDP_PORT    remotePort;		// Remote node's UDP port number
    UDP_PORT    localPort;		// Local UDP port number, or INVALID_UDP_PORT when free
} UDP_SOCKET_INFO;


#define INVALID_UDP_SOCKET      (0xffu)		// Indicates a UDP socket that is not valid
#define INVALID_UDP_PORT        (0ul)		// Indicates a UDP port that is not valid

/****************************************************************************
  Section:
	External Global Variables
  ***************************************************************************/
#if !defined(__UDP_C)
    extern UDP_SOCKET activeUDPSocket;
    extern UDP_SOCKET_INFO  UDPSocketInfo[MAX_UDP_SOCKETS];
	extern WORD UDPTxCount;
	extern WORD UDPRxCount;
#endif

// Stores the header of a UDP packet
typedef struct
{
    UDP_PORT    SourcePort;				// Source UDP port
    UDP_PORT    DestinationPort;		// Destination UDP port
    WORD        Length;					// Length of data
    WORD        Checksum;				// UDP checksum of the data
} UDP_HEADER;

/****************************************************************************
  Section:
	Function Prototypes
  ***************************************************************************/
void UDPInit(void);
void UDPTask(void);

UDP_SOCKET UDPOpen(UDP_PORT localPort, NODE_INFO *remoteNode, UDP_PORT remotePort);
void UDPClose(UDP_SOCKET s);
BOOL UDPProcess(NODE_INFO *remoteNode, IP_ADDR *localIP, WORD len);

void UDPSetTxBuffer(WORD wOffset);
void UDPSetRxBuffer(WORD wOffset);
WORD UDPIsPutReady(UDP_SOCKET s);
BOOL UDPPut(BYTE v);
WORD UDPPutArray(BYTE *cData, WORD wDataLen);
BYTE* UDPPutString(BYTE *strData);
void UDPFlush(void);

// ROM function variants for PIC18
#if defined(__18CXX)
	WORD UDPPutROMArray(ROM BYTE *cData, WORD wDataLen);
	ROM BYTE* UDPPutROMString(ROM BYTE *strData);
#else
	#define UDPPutROMArray(a,b)	UDPPutArray((BYTE*)a,b)
	#define UDPPutROMString(a)	UDPPutString((BYTE*)a)
#endif

WORD UDPIsGetReady(UDP_SOCKET s);
BOOL UDPGet(BYTE *v);
WORD UDPGetArray(BYTE *cData, WORD wDataLen);
void UDPDiscard(void);

#endif
