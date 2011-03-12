/*********************************************************************
 *
 *                  ARP Module Defs for Microchip TCP/IP Stack
 *
 *********************************************************************
 * FileName:        ARP.h
 * Dependencies:    Stacktsk.h
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
 * Nilesh Rajbharti     5/1/01  Original        (Rev 1.0)
 * Nilesh Rajbharti     2/9/02  Cleanup
 * Nilesh Rajbharti     5/22/02 Rev 2.0 (See version.log for detail)
 * Howard Schlunder		8/17/06	Combined ARP.h and ARPTsk.h into ARP.h
 ********************************************************************/
#ifndef __ARP_H
#define __ARP_H

#ifdef STACK_CLIENT_MODE
	void ARPInit(void);
#else
	#define ARPInit()
#endif

#define ARP_OPERATION_REQ       0x0001u		// Operation code indicating an ARP Request
#define ARP_OPERATION_RESP      0x0002u		// Operation code indicating an ARP Response

#define HW_ETHERNET             (0x0001u)	// ARP Hardware type as defined by IEEE 802.3
#define ARP_IP                  (0x0800u)	// ARP IP packet type as defined by IEEE 802.3


// ARP packet structure
typedef struct __attribute__((aligned(2), packed))
{
    WORD        HardwareType;
    WORD        Protocol;
    BYTE        MACAddrLen;
    BYTE        ProtocolLen;
    WORD        Operation;
    MAC_ADDR    SenderMACAddr;
    IP_ADDR     SenderIPAddr;
    MAC_ADDR    TargetMACAddr;
    IP_ADDR     TargetIPAddr;
} ARP_PACKET;

BOOL ARPProcess(void);
void ARPResolve(IP_ADDR* IPAddr);
BOOL ARPIsResolved(IP_ADDR* IPAddr, MAC_ADDR* MACAddr);
void SwapARPPacket(ARP_PACKET* p);

#ifdef STACK_USE_ZEROCONF_LINK_LOCAL
	/* API specific Definitions */
	#define ARP_REQ       0x0001u		// Operation code indicating an ARP Request
	#define ARP_RESP      0x0002u		// Operation code indicating an ARP Response

	struct arp_app_callbacks {
    	BOOL used;
    	void (*ARPPkt_notify)(DWORD SenderIPAddr, DWORD TargetIPAddr, 
                          	MAC_ADDR* SenderMACAddr, MAC_ADDR* TargetMACAddr, BYTE op_req);
	};
	CHAR ARPRegisterCallbacks(struct arp_app_callbacks *app);
	BOOL ARPDeRegisterCallbacks(CHAR id);
	BOOL ARPSendPkt(DWORD SrcIPAddr, DWORD DestIPAddr, BYTE op_req );
#endif

#endif


