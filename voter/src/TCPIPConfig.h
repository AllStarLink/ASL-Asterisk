/*********************************************************************
 *
 *	Microchip TCP/IP Stack Demo Application Configuration Header
 *
 *********************************************************************
 * FileName:        TCPIPConfig.h
 * Dependencies:    Microchip TCP/IP Stack
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
 * Author               Date        Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Howard Schlunder		10/04/2006	Original
 * Ken Hesky            07/01/2008  Added ZG2100-specific features
 * SG                   03/25/2009  Added ZGLinkMgrII specific features
 ********************************************************************/
#ifndef __TCPIPCONFIG_H
#define __TCPIPCONFIG_H

#include "GenericTypeDefs.h"
#include "Compiler.h"
#define GENERATED_BY_TCPIPCONFIG "Version 1.0.3937.25080"

// =======================================================================
//   Application Options
// =======================================================================

/* Application Level Module Selection
 *   Uncomment or comment the following lines to enable or
 *   disabled the following high-level application modules.
 */
#define STACK_USE_UART					// Application demo using UART for IP address display and stack configuration
	//#define STACK_USE_UART2TCP_BRIDGE		// UART to TCP Bridge application example
//#define STACK_USE_IP_GLEANING
//#define STACK_USE_ICMP_SERVER			// Ping query and response capability
//#define STACK_USE_ICMP_CLIENT			// Ping transmission capability
//#define STACK_USE_HTTP_SERVER			// Old HTTP server
//#define STACK_USE_HTTP2_SERVER			// New HTTP server with POST, Cookies, Authentication, etc.
//#define STACK_USE_SSL_SERVER			// SSL server socket support (Requires SW300052)
//#define STACK_USE_SSL_CLIENTs			// SSL client socket support (Requires SW300052)
#define STACK_USE_DHCP_CLIENT			// Dynamic Host Configuration Protocol client for obtaining IP address and other parameters
//#define STACK_USE_DHCP_SERVER			// Single host DHCP server
//#define STACK_USE_FTP_SERVER			// File Transfer Protocol (old)
//#define STACK_USE_SMTP_CLIENT			// Simple Mail Transfer Protocol for sending email
//#define STACK_USE_SNMP_SERVER			// Simple Network Management Protocol v2C Community Agent
//#define STACK_USE_TFTP_CLIENT			// Trivial File Transfer Protocol client
//#define STACK_USE_GENERIC_TCP_CLIENT_EXAMPLE	// HTTP Client example in GenericTCPClient.c
//#define STACK_USE_GENERIC_TCP_SERVER_EXAMPLE	// ToUpper server example in GenericTCPServer.c
#define STACK_USE_TELNET_SERVER			// Telnet server
//#define STACK_USE_ANNOUNCE				// Microchip Embedded Ethernet Device Discoverer server/client
#define STACK_USE_DNS					// Domain Name Service Client for resolving hostname strings to IP addresses
//#define STACK_USE_NBNS					// NetBIOS Name Service Server for repsonding to NBNS hostname broadcast queries
//#define STACK_USE_REBOOT_SERVER			// Module for resetting this PIC remotely.  Primarily useful for a Bootloader.
//#define STACK_USE_SNTP_CLIENT			// Simple Network Time Protocol for obtaining current date/time from Internet
//#define STACK_USE_UDP_PERFORMANCE_TEST	// Module for testing UDP TX performance characteristics.  NOTE: Enabling this will cause a huge amount of UDP broadcast packets to flood your network on the discard port.  Use care when enabling this on production networks, especially with VPNs (could tunnel broadcast traffic across a limited bandwidth connection).
//#define STACK_USE_TCP_PERFORMANCE_TEST	// Module for testing TCP TX performance characteristics
#define STACK_USE_DYNAMICDNS_CLIENT		// Dynamic DNS client updater module
//#define STACK_USE_BERKELEY_API			// Berekely Sockets APIs are available


// =======================================================================
//   Data Storage Options
// =======================================================================

/* MPFS Configuration
 *   MPFS is automatically included when required for other
 *   applications.  If your custom application requires it
 *   otherwise, uncomment the appropriate selection.
 */
//#define STACK_USE_MPFS
//#define STACK_USE_MPFS2

/* MPFS Storage Location
 *   If html pages are stored in internal program memory,
 *   comment both MPFS_USE_EEPROM and MPFS_USE_SPI_FLASH, then
 *   include an MPFS image (.c or .s file) in the project.
 *   If html pages are stored in external memory, uncomment the
 *   appropriate definition.
 *
 *   Supported serial flash parts include the SST25VFxxxB series.
 */
//#define MPFS_USE_EEPROM
//#define MPFS_USE_SPI_FLASH

/* EEPROM Addressing Selection
 *   If using the 1Mbit EEPROM, uncomment this line
 */
//#define USE_EEPROM_25LC1024

/* EEPROM Reserved Area
 *   Number of EEPROM bytes to be reserved before MPFS storage starts.
 *   These bytes host application configurations such as IP Address,
 *   MAC Address, and any other required variables.
 *
 *   For MPFS Classic, this setting must match the Reserved setting
 *	 on the Advanced Settings page of the MPFS2 Utility.
 */
//#define MPFS_RESERVE_BLOCK                (64ul)

/* MPFS File Handles
 *   Maximum number of simultaneously open MPFS2 files.
 *   For MPFS Classic, this has no effect.
 */
//#define MAX_MPFS_HANDLES				(7ul)


// =======================================================================
//   Network Addressing Options
// =======================================================================

/* Default Network Configuration
 *   These settings are only used if data is not found in EEPROM.
 *   To clear EEPROM, hold BUTTON0, reset the board, and continue
 *   holding until the LEDs flash.  Release, and reset again.
 */
#define MY_DEFAULT_HOST_NAME			"MCHPBOARD"

#define MY_DEFAULT_MAC_BYTE1            (0x00)	// Use the default of
#define MY_DEFAULT_MAC_BYTE2            (0x04)	// 00-04-A3-00-00-00 if using
#define MY_DEFAULT_MAC_BYTE3            (0xA3)	// an ENCX24J600 or ZeroG ZG2100
#define MY_DEFAULT_MAC_BYTE4            (0x00)	// and wish to use the internal
#define MY_DEFAULT_MAC_BYTE5            (0x00)	// factory programmed MAC
#define MY_DEFAULT_MAC_BYTE6            (0x00)	// address instead.


// =======================================================================
//   ZeroG Wireless Options
// =======================================================================

//#define STACK_USE_ZG2100

// Default SSID or wireless network name to connect to
#define MY_DEFAULT_SSID_NAME                "MicrochipDemoAP"

/*******************************************/
/* DOMAINS & CHANNEL COMPILE TIME DEFAULTS */
/*******************************************/
/* Valid domains:    kZGRegDomainFCC      Available Channels: 1 - 11     */
/*                   kZGRegDomainIC       Available Channels: 1 - 11     */
/*                   kZGRegDomainETSI     Available Channels: 1 - 13     */
/*                   kZGRegDomainJapanA   Available Channels: 14         */
/*                   kZGRegDomainJapanB   Available Channels: 1 - 13     */
#define MY_DEFAULT_DOMAIN                   kZGRegDomainFCC

// When attempting to find the wireless network, only these radio channels
// will be scanned.  Channels 1, 6, and 11 are the three non-overlapping
// radio channels normally used in the FCC regulatory domain.  If you add
// or subtract radio channels, be sure to also update the
// MY_DEFAULT_CHANNEL_LIST_SIZE setting.
#define MY_DEFAULT_CHANNEL_SCAN_LIST        {1, 6, 11, }
#define END_OF_MY_DEFAULT_CHANNEL_SCAN_LIST

// Count of elements in the MY_DEFAULT_CHANNEL_SCAN_LIST macro
#define MY_DEFAULT_CHANNEL_LIST_SIZE        (3u)

/**********************************/
/* SECURITY COMPILE TIME DEFAULTS */
/**********************************/
// Security used on WiFi network.  Legal values are:
// kKeyTypeNone: No encryption/authentication
// kKeyTypeWep: Wired Equivalency Protocol (WEP) encryption
// kKeyTypePsk: WPA-PSK Personal or WPA2-PSK Personal TKIP or AES
//              encryption with precalculated key (see MY_DEFAULT_PSK).
// kKeyTypeCalcPsk: WPA-PSK Personal or WPA2-PSK Personal TKIP or AES
//              encryption with the hardware dynamically generating the
//              needed key for the selected SSID and passphrase.  This
//              option requires more time to associate with the access
//              point relative to kKeyTypePsk which has the key
//              pre-calculated.
#define MY_DEFAULT_ENCRYPTION_TYPE          kKeyTypeNone

// If using security type of kKeyTypePsk, then this section must be set to
// match the key for the MY_DEFAULT_SSID_NAME and MY_DEFAULT_PSK_PHRASE
// combination.  The tool at http://www.wireshark.org/tools/wpa-psk.html
// can be used to generate this field.
#define MY_DEFAULT_PSK                      {0x86, 0xC5, 0x1D, 0x71, 0xD9, 0x1A, 0xAA, 0x49, \
                                            0x40, 0xC8, 0x88, 0xC6, 0xE9, 0x7A, 0x4A, 0xD5, \
                                            0xE5, 0x6D, 0xDA, 0x44, 0x8E, 0xFB, 0x9C, 0x0A, \
                                            0xE1, 0x47, 0x81, 0x52, 0x31, 0x1C, 0x13, 0x7C, \
                                            }
#define END_OF_MY_DEFAULT_PSK

// Default pass phrase used for kKeyTypePsk and kKeyTypeCalcPsk security modes
#define MY_DEFAULT_PSK_PHRASE               "Microchip 802.11 Secret PSK Password"

#define MY_DEFAULT_WEP_KEY_LEN				kZGWEPKeyLenLong
#define MY_DEFAULT_WEP_KEYS					MY_DEFAULT_WEP_KEYS_LONG

// Default WEP keys used in kKeyTypeWep security mode
#define MY_DEFAULT_WEP_KEYS_LONG        {   {{0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C}},\
                                            {{0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C}},\
                                            {{0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C}},\
                                            {{0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C}},\
                                            }
#define END_OF_MY_DEFAULT_WEP_KEYS_LONG
#define MY_DEFAULT_WEP_KEYS_SHORT      		{{{0x00,0x01,0x02,0x03,0x04}},\
                                        	{{0x10,0x11,0x12,0x13,0x14}},\
                                        	{{0x20,0x21,0x22,0x23,0x24}},\
                                        	{{0x30,0x31,0x32,0x33,0x34}},\
                                        	}
#define END_OF_MY_DEFAULT_WEP_KEYS_SHORT

/* Valid Key Index: 0, 1, 2, 3  */
#define MY_DEFAULT_WEP_KEY_INDEX            (0u)

/* Valid WEP auth:   kZGAuthAlgOpen   */
/*                   kZGAuthAlgShared */
#define MY_DEFAULT_WEP_AUTH                 kZGAuthAlgOpen

// These options are required for all PIC devices
#if defined(__18CXX)
    #define ZG_NO_FUNC_PTRS                 y
#endif
#define ZG_CONFIG_LIBRARY                   y
#define ZG_PKG_STDIO                        y
#define ZG_RAW_DRIVER                       y

// These options are configurable
// Provides indication if AP beacons are lost
#define CONNECTION_LOST_FEATURE             y

// Command line interface
//#define ZG_CONFIG_CONSOLE                   y

// Provides API to switch between static and dynamic IP addressing
#if defined ( ZG_CONFIG_CONSOLE )
    //#define ZG_CONFIG_DHCP                  y
#endif

// WiFi (BSS), Adhoc (IBSS) connection management turn on/off
#define ZG_CONFIG_LINKMGRII                 y

// Default link management
/* Valid Modes:   kZGLMNetworkModeIdle            (Standby / neutral state)  */
/*                kZGLMNetworkModeAdhoc           (IBSS networks)            */
/*                kZGLMNetworkModeInfrastructure  (BSS networks)             */
//#define MY_DEFAULT_LINK_MGMT                kZGLMNetworkModeIdle

// Uncomment this line if you do NOT need IBSS "adhoc" networks.
// If uncommented, ensure that MY_DEFAULT_LINK_MGMT is not kZGLMNetworkModeAdhoc
#define ZG_CONFIG_NO_ADHOCMGRII             y

// Uncomment this line if you do NOT need BSS "managed" networks.
// If uncommented, ensure that MY_DEFAULT_LINK_MGMT is not kZGLMNetworkModeInfrastructure
//#define ZG_CONFIG_NO_WIFIMGRII              y



// =======================================================================
//   Transport Layer Options
// =======================================================================

/* Transport Layer Configuration
 *   The following low level modules are automatically enabled
 *   based on module selections above.  If your custom module
 *   requires them otherwise, enable them here.
 */
#define STACK_USE_TCP
#define STACK_USE_UDP

/* Client Mode Configuration
 *   Uncomment following line if this stack will be used in CLIENT
 *   mode.  In CLIENT mode, some functions specific to client operation
 *   are enabled.
 */
#define STACK_CLIENT_MODE

/* TCP Socket Memory Allocation
 *   TCP needs memory to buffer incoming and outgoing data.  The
 *   amount and medium of storage can be allocated on a per-socket
 *   basis using the example below as a guide.
 */
	// Allocate how much total RAM (in bytes) you want to allocate
	// for use by your TCP TCBs, RX FIFOs, and TX FIFOs.
	#define TCP_ETH_RAM_SIZE					(3900ul)
	#define TCP_PIC_RAM_SIZE					(0ul)
	#define TCP_SPI_RAM_SIZE					(0ul)
	#define TCP_SPI_RAM_BASE_ADDRESS			(0x00)

	// Define names of socket types
	#define TCP_SOCKET_TYPES
		#define TCP_PURPOSE_GENERIC_TCP_CLIENT 0
		#define TCP_PURPOSE_GENERIC_TCP_SERVER 1
		#define TCP_PURPOSE_TELNET 2
		#define TCP_PURPOSE_FTP_COMMAND 3
		#define TCP_PURPOSE_FTP_DATA 4
		#define TCP_PURPOSE_TCP_PERFORMANCE_TX 5
		#define TCP_PURPOSE_TCP_PERFORMANCE_RX 6
		#define TCP_PURPOSE_UART_2_TCP_BRIDGE 7
		#define TCP_PURPOSE_HTTP_SERVER 8
		#define TCP_PURPOSE_DEFAULT 9
		#define TCP_PURPOSE_BERKELEY_SERVER 10
		#define TCP_PURPOSE_BERKELEY_CLIENT 11
	#define END_OF_TCP_SOCKET_TYPES

	#if defined(__TCP_C)
		// Define what types of sockets are needed, how many of
		// each to include, where their TCB, TX FIFO, and RX FIFO
		// should be stored, and how big the RX and TX FIFOs should
		// be.  Making this initializer bigger or smaller defines
		// how many total TCP sockets are available.
		//
		// Each socket requires up to 48 bytes of PIC RAM and
		// 40+(TX FIFO size)+(RX FIFO size) bytes bytes of
		// TCP_*_RAM each.
		// Note: The RX FIFO must be at least 1 byte in order to
		// receive SYN and FIN messages required by TCP.  The TX
		// FIFO can be zero if desired.
		#define TCP_CONFIGURATION
		ROM struct
		{
			BYTE vSocketPurpose;
			BYTE vMemoryMedium;
			WORD wTXBufferSize;
			WORD wRXBufferSize;
		} TCPSocketInitializer[] = 
		{
			//{TCP_PURPOSE_GENERIC_TCP_CLIENT, TCP_ETH_RAM, 125, 100},
			{TCP_PURPOSE_GENERIC_TCP_SERVER, TCP_ETH_RAM, 20, 20},
			{TCP_PURPOSE_TELNET, TCP_ETH_RAM, 200, 150},
			//{TCP_PURPOSE_TELNET, TCP_ETH_RAM, 200, 150},
			//{TCP_PURPOSE_TELNET, TCP_ETH_RAM, 200, 150},
			//{TCP_PURPOSE_FTP_COMMAND, TCP_ETH_RAM, 100, 40},
			//{TCP_PURPOSE_FTP_DATA, TCP_ETH_RAM, 0, 128},
			{TCP_PURPOSE_TCP_PERFORMANCE_TX, TCP_ETH_RAM, 200, 1},
			//{TCP_PURPOSE_TCP_PERFORMANCE_RX, TCP_ETH_RAM, 40, 1500},
			//{TCP_PURPOSE_UART_2_TCP_BRIDGE, TCP_ETH_RAM, 256, 256},
			{TCP_PURPOSE_HTTP_SERVER, TCP_ETH_RAM, 200, 200},
			{TCP_PURPOSE_HTTP_SERVER, TCP_ETH_RAM, 200, 200},
			{TCP_PURPOSE_DEFAULT, TCP_ETH_RAM, 200, 200},
			{TCP_PURPOSE_BERKELEY_SERVER, TCP_ETH_RAM, 25, 20},
			//{TCP_PURPOSE_BERKELEY_SERVER, TCP_ETH_RAM, 25, 20},
			//{TCP_PURPOSE_BERKELEY_SERVER, TCP_ETH_RAM, 25, 20},
			//{TCP_PURPOSE_BERKELEY_CLIENT, TCP_ETH_RAM, 125, 100},
		};
		#define END_OF_TCP_CONFIGURATION
	#endif

/* UDP Socket Configuration
 *   Define the maximum number of available UDP Sockets, and whether
 *   or not to include a checksum on packets being transmitted.
 */
#define MAX_UDP_SOCKETS     (10u)
#define UDP_USE_TX_CHECKSUM		// This slows UDP TX performance by nearly 50%, except when using the ENCX24J600, which has a super fast DMA and incurs virtually no speed pentalty.


/* Berkeley API Sockets Configuration
 *   Note that each Berkeley socket internally uses one TCP or UDP socket
 *   defined by MAX_UDP_SOCKETS and the TCPSocketInitializer[] array.
 *   Therefore, this number MUST be less than or equal to MAX_UDP_SOCKETS + the
 *   number of TCP sockets defined by the TCPSocketInitializer[] array
 *   (i.e. sizeof(TCPSocketInitializer)/sizeof(TCPSocketInitializer[0])).
 *   This define has no effect if STACK_USE_BERKELEY_API is not defined and
 *   Berkeley Sockets are disabled.  Set this value as low as your application
 *   requires to avoid waisting RAM.
 */
#define BSD_SOCKET_COUNT (5u)


// =======================================================================
//   Application-Specific Options
// =======================================================================

// -- HTTP2 Server options -----------------------------------------------

	// Maximum numbers of simultaneous HTTP connections allowed.
	// Each connection consumes 2 bytes of RAM and a TCP socket
	#define MAX_HTTP_CONNECTIONS	(2u)

	// Indicate what file to serve when no specific one is requested
	#define HTTP_DEFAULT_FILE		"index.htm"
	#define HTTPS_DEFAULT_FILE		"index.htm"
	#define HTTP_DEFAULT_LEN		(10u)		// For buffer overrun protection.
												// Set to longest length of above two strings.

	// Configure MPFS over HTTP updating
	// Comment this line to disable updating via HTTP
	#define HTTP_MPFS_UPLOAD		"mpfsupload"
	//#define HTTP_MPFS_UPLOAD_REQUIRES_AUTH	// Require password for MPFS uploads
		// Certain firewall and router combinations cause the MPFS2 Utility to fail
		// when uploading.  If this happens, comment out this definition.

	// Define which HTTP modules to use
	// If not using a specific module, comment it to save resources
	#define HTTP_USE_POST					// Enable POST support
	#define HTTP_USE_COOKIES				// Enable cookie support
	#define HTTP_USE_AUTHENTICATION			// Enable basic authentication support

	//#define HTTP_NO_AUTH_WITHOUT_SSL		// Uncomment to require SSL before requesting a password
	#define HTTP_SSL_ONLY_CHAR		(0xFF)	// Files beginning with this character will only be served over HTTPS
											// Set to 0x00 to require for all files
											// Set to 0xff to require for no files

	#define STACK_USE_HTTP_APP_RECONFIG		// Use the AppConfig web page in the Demo App (~2.5kb ROM, ~0b RAM)
	#define STACK_USE_HTTP_MD5_DEMO			// Use the MD5 Demo web page (~5kb ROM, ~160b RAM)
	#define STACK_USE_HTTP_EMAIL_DEMO		// Use the e-mail demo web page

// -- SSL Options --------------------------------------------------------

	#define MAX_SSL_CONNECTIONS		(2ul)	// Maximum connections via SSL
	#define MAX_SSL_SESSIONS		(2ul)	// Max # of cached SSL sessions
	#define MAX_SSL_BUFFERS			(4ul)	// Max # of SSL buffers (2 per socket)
	#define MAX_SSL_HASHES			(5ul)	// Max # of SSL hashes  (2 per, plus 1 to avoid deadlock)

	// Bits in SSL RSA key.  This parameter is used for SSL sever
	// connections only.  The only valid value is 512 bits (768 and 1024
	// bits do not work at this time).  Note, however, that SSL client
	// operations do currently work up to 1024 bit RSA key length.
	#define SSL_RSA_KEY_SIZE		(512ul)


// -- Telnet Options -----------------------------------------------------

	// Number of simultaneously allowed Telnet sessions.  Note that you
	// must have an equal number of TCP_PURPOSE_TELNET type TCP sockets
	// declared in the TCPSocketInitializer[] array above for multiple
	// connections to work.  If fewer sockets are available than this
	// definition, then the the lesser of the two quantities will be the
	// actual limit.
	#define MAX_TELNET_CONNECTIONS	(1u)


// -- SNMP Options -------------------------------------------------------

	// Comment following line if SNMP TRAP support is needed
	//#define SNMP_TRAP_DISABLED

	// This is the maximum length for community string.
	// Application must ensure that this length is observed.
	// SNMP module adds one byte extra after SNMP_COMMUNITY_MAX_LEN
	// for adding '\0' NULL character.
	#define SNMP_COMMUNITY_MAX_LEN  	(8u)
	#define SNMP_MAX_COMMUNITY_SUPPORT	(3u)
	#define NOTIFY_COMMUNITY_LEN		(SNMP_COMMUNITY_MAX_LEN)

	// Default SNMPv2C community names.  These can be overridden at run time if
	// alternate strings are present in external EEPROM or Flash (actual
	// strings are stored in AppConfig.readCommunity[] and
	// AppConfig.writeCommunity[] arrays).  These strings are case sensitive.
	// An empty string means disabled (not matchable).
	// For application security, these default community names should not be
	// used, but should all be disabled to force the end user to select unique
	// community names.  These defaults are provided only to make it easier to
	// start development.  Specifying more strings than
	// SNMP_MAX_COMMUNITY_SUPPORT will result in the later strings being
	// ignored (but still wasting program memory).  Specifying fewer strings is
	// legal, as long as at least one is present.  A string larger than
	// SNMP_COMMUNITY_MAX_LEN bytes will be ignored.
	#define SNMP_READ_COMMUNITIES        {"public", "read", ""}
    #define END_OF_SNMP_READ_COMMUNITIES
	#define SNMP_WRITE_COMMUNITIES        {"private", "write", "public"}
    #define END_OF_SNMP_WRITE_COMMUNITIES
#endif


//#define STACK_USE_SSL_CLIENT		

//#define MPFS_USE_FAT		

//#define STACK_USE_AUTO_IP		

#define MDD_ROOT_DIR_PATH		"\\"

//#define SNMP_STACK_USE_V2_TRAP		

//#define STACK_USE_SNMPV3_SERVER		
