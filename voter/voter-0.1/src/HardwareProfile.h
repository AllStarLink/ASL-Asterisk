/*********************************************************************
 *
 *	Hardware specific definitions
 *
 *********************************************************************
 * FileName:        HardwareProfile.h
 * Dependencies:    None
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
 * Author               Date		Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Howard Schlunder		10/03/06	Original, copied from Compiler.h
 * Ken Hesky            07/01/08    Added ZG2100-specific features
 ********************************************************************/
#ifndef __HARDWARE_PROFILE_H
#define __HARDWARE_PROFILE_H

#include "GenericTypeDefs.h"
#include "Compiler.h"

extern char dummy_loc;

// Set configuration fuses (but only once)
#if defined(THIS_IS_STACK_APPLICATION)

// Set Configuration Registers
_FGS( GSS_OFF & GCP_OFF & GWRP_OFF )
_FOSCSEL( FNOSC_PRIPLL & IESO_OFF )
_FOSC( FCKSM_CSDCMD & IOL1WAY_OFF & OSCIOFNC_OFF & POSCMD_XT )
_FWDT( FWDTEN_OFF )
_FPOR(ALTI2C_OFF & FPWRT_PWR1 )	
_FICD(JTAGEN_OFF & ICS_PGD3 )

#endif // Prevent more than one set of config fuse definitions

// Clock frequency value.
// This value is used to calculate Tick Counter value


// dsPIC33F processor
#define GetSystemClock()		(80000000ul)      // Hz
#define GetInstructionClock()	(GetSystemClock()/2)
#define GetPeripheralClock()	GetInstructionClock()


#define	SPISel(x) { _LATB3 = 0; _LATB4 = 0; LATB |= x; }
#define SPICS_ENC 0x18
#define	SPICS_POT 8
#define	SPICS_IOEXP 0x10
#define	SPICS_EEPROM 0
#define	SPICS_IDLE SPICS_POT
#define	SPICS_POT_IDLE SPICS_IOEXP

#define UARTTX_TRIS			dummy_loc
#define UARTTX_IO			dummy_loc
#define UARTRX_TRIS			dummy_loc
#define UARTRX_IO			dummy_loc

// ENC28J60 I/O pins
#define ENC_CS_TRIS			(TRISBbits.TRISB11)	// Comment this line out if you are using the ENC424J600/624J600, ZeroG ZG2100, or other network controller.
#define ENC_CS_IO			dummy_loc
//#define ENC_RST_TRIS		(TRISBbits.TRISB10)	
//#define ENC_RST_IO			(PORTBbits.RB10)

#define ENC_SPI_IF			(IFS0bits.SPI1IF)
#define ENC_SSPBUF			(SPI1BUF)
#define ENC_SPISTAT			(SPI1STAT)
#define ENC_SPISTATbits		(SPI1STATbits)
#define ENC_SPICON1			(SPI1CON1)
#define ENC_SPICON1bits		(SPI1CON1bits)
#define ENC_SPICON2			(SPI1CON2)

// 25LC256 I/O pins
#define EEPROM_CS_TRIS		dummy_loc
#define EEPROM_CS_IO		dummy_loc
#define EEPROM_SCK_TRIS		dummy_loc
#define EEPROM_SDI_TRIS		dummy_loc
#define EEPROM_SDO_TRIS		dummy_loc
#define EEPROM_SPI_IF		(IFS0bits.SPI1IF)
#define EEPROM_SSPBUF		(SPI1BUF)
#define EEPROM_SPICON1		(SPI1CON1)
#define EEPROM_SPICON1bits	(SPI1CON1bits)
#define EEPROM_SPICON2		(SPI1CON2)
#define EEPROM_SPISTAT		(SPI1STAT)
#define EEPROM_SPISTATbits	(SPI1STATbits)

#define POT_SPI_IF		(IFS0bits.SPI1IF)
#define POT_SSPBUF		(SPI1BUF)
#define POT_SPICON1		(SPI1CON1)
#define POT_SPICON1bits	(SPI1CON1bits)
#define POT_SPICON2		(SPI1CON2)
#define POT_SPISTAT		(SPI1STAT)
#define POT_SPISTATbits	(SPI1STATbits)

#define IOEXP_SPI_IF		(IFS0bits.SPI1IF)
#define IOEXP_SSPBUF		(SPI1BUF)
#define IOEXP_SPICON1		(SPI1CON1)
#define IOEXP_SPICON1bits	(SPI1CON1bits)
#define IOEXP_SPICON2		(SPI1CON2)
#define IOEXP_SPISTAT		(SPI1STAT)
#define IOEXP_SPISTATbits	(SPI1STATbits)

#define	DISABLE_INTERRUPTS() __builtin_disi(0x3FFF)
#define	ENABLE_INTERRUPTS() __builtin_disi(0)

#endif
