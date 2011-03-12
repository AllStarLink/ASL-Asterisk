/*********************************************************************
 *
 *                  Microchip TCP/IP Stack Definitions
 *
 *********************************************************************
 * FileName:        StackTsk.h
 * Dependencies:    Compiler.h
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
 * Nilesh Rajbharti     8/10/01 Original        (Rev 1.0)
 * Nilesh Rajbharti     2/9/02  Cleanup
 * Nilesh Rajbharti     5/22/02 Rev 2.0 (See version.log for detail)
 * Nilesh Rajbharti     8/7/03  Rev 2.21 - TFTP Client addition
 * Howard Schlunder		9/30/04	Added MCHP_MAC, MAC_POWER_ON_TEST, 
 								EEPROM_BUFFER_SIZE, USE_LCD
 * Howard Schlunder		8/09/06	Removed MCHP_MAC, added STACK_USE_NBNS, 
 *								STACK_USE_DNS, and STACK_USE_GENERIC_TCP_EXAMPLE
 ********************************************************************/
#ifndef __STACK_TSK_H
#define __STACK_TSK_H

#if defined (WF_CS_TRIS)
    #include "WF_Config.h" // pull in additional defines from wireless settings
#endif

// Check for potential configuration errors in "TCPIPConfig.h"
#if (MAX_UDP_SOCKETS <= 0 || MAX_UDP_SOCKETS > 255 )
#error Invalid MAX_UDP_SOCKETS value specified
#endif

// Check for potential configuration errors in "TCPIPConfig.h"
#if (MAX_HTTP_CONNECTIONS <= 0 || MAX_HTTP_CONNECTIONS > 255 )
#error Invalid MAX_HTTP_CONNECTIONS value specified.
#endif

// Structure to contain a MAC address
typedef struct __attribute__((__packed__))
{
    BYTE v[6];
} MAC_ADDR;

// Definition to represent an IP address
#define IP_ADDR		DWORD_VAL

// Address structure for a node
typedef struct __attribute__((__packed__))
{
    IP_ADDR     IPAddr;
    MAC_ADDR    MACAddr;
} NODE_INFO;

// Application-dependent structure used to contain address information
typedef struct __attribute__((__packed__)) 
{
	WORD		SerialNumber;			// Serial #
	IP_ADDR		MyIPAddr;               // IP address
	IP_ADDR		MyMask;                 // Subnet mask
	IP_ADDR		MyGateway;              // Default Gateway
	IP_ADDR		PrimaryDNSServer;       // Primary DNS Server
	IP_ADDR		SecondaryDNSServer;     // Secondary DNS Server
	IP_ADDR		DefaultIPAddr;          // Default IP address
	IP_ADDR		DefaultMask;            // Default subnet mask
	IP_ADDR		DefaultGateway;         // Default Gateway
	IP_ADDR		DefaultPrimaryDNSServer;   // Default Primary DNS Server
	IP_ADDR		DefaultSecondaryDNSServer; // Default Secondary DNS Server
	BYTE		NetBIOSName[16];        // NetBIOS name
	struct
	{
		unsigned char : 6;
		unsigned char bIsDHCPEnabled : 1;
		unsigned char bInConfigMode : 1;
	} Flags;                            // Flag structure
	MAC_ADDR	MyMACAddr;              // Application MAC address

	BYTE SqlNoiseGain;		// Squelch Noise Gain Setting
	WORD SqlDiode;			// Diode Calibration setting
	WORD TxBufferLength;	// Tx buffer length
	WORD TxBufferDelay;		// Tx buffer Delay
	char VoterServerFQDN[50];  // FQDN of Voter Server
	WORD VoterServerPort;	// UDP Port of Voter Server
	WORD DefaultPort;		// Default local port
	WORD MyPort;			// My UDP Port
	char Password[20];
	char HostPassword[20];
	BYTE GPSPolarity;
	BYTE PPSPolarity;
	DWORD GPSBaudRate;
	BYTE DebugLevel;
	WORD TelnetPort;
	BYTE TelnetUsername[20];
	BYTE TelnetPassword[20];
	BYTE DynDNSEnable;
	BYTE DynDNSUsername[20];
	BYTE DynDNSPassword[20];
	BYTE DynDNSHost[50];
} APP_CONFIG;

#ifndef THIS_IS_STACK_APPLICATION
    extern APP_CONFIG AppConfig;
#endif


void StackInit(void);
void StackTask(void);
void StackApplications(void);

#endif
