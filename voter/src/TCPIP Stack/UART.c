/*********************************************************************
 *
 *     UART access routines for C18 and C30
 *
 *********************************************************************
 * FileName:        UART.c
 * Dependencies:    Hardware UART module
 * Processor:       PIC18, PIC24F, PIC24H, dsPIC30F, dsPIC33F
 * Compiler:        Microchip C30 v3.12 or higher
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
 * Author               Date   		Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Howard Schlunder		4/04/06		Copied from dsPIC30 libraries
 * Howard Schlunder		6/16/06		Added PIC18
********************************************************************/
#define __UART_C

#include "TCPIPConfig.h"

#if defined(STACK_USE_UART)

#include "TCPIP Stack/TCPIP.h"


static BYTE txBuf[UART_TXBUF_SIZE];
static BYTE txgetidx;   
static BYTE txputidx;   
static BYTE rxBuf[UART_RXBUF_SIZE];
static BYTE rxputidx;   
static BYTE rxgetidx;   

static BYTE txBuf2[UART_TXBUF_SIZE];
static BYTE txgetidx2;   
static BYTE txputidx2;   
static BYTE rxBuf2[UART_RXBUF_SIZE];
static BYTE rxputidx2;   
static BYTE rxgetidx2;   

void __attribute__((interrupt, auto_psv)) _U1RXInterrupt(void)
{
	BYTE dummy;

	_LATA1 ^= 1;

	while(U1STAbits.URXDA)
	{
		if (U1STAbits.PERR || U1STAbits.FERR || U1STAbits.OERR)
		{
			U1STAbits.PERR = 0;
			U1STAbits.FERR = 0;
			U1STAbits.OERR = 0;
			dummy = U1RXREG;
		}
		else
		{
			if (((rxputidx + 1) & UART_RXBUF_MASK) != rxgetidx)
			{
				rxBuf[rxputidx] = U1RXREG; 
			    rxputidx++;
				rxputidx &= UART_RXBUF_MASK;
			} else dummy = U1RXREG;
		}
	}
	IFS0bits.U1RXIF = 0;
}

void __attribute__((interrupt, auto_psv)) _U1TXInterrupt(void)
{
    if (txputidx != txgetidx) 
	{
        U1TXREG = txBuf[txgetidx];
        txgetidx++;
		txgetidx &= UART_TXBUF_MASK;
    }
    else 
	{
        IEC0bits.U1TXIE = 0;   
    }
	IFS0bits.U1TXIF = 0;
}

void __attribute__((interrupt, auto_psv)) _U2RXInterrupt(void)
{
	BYTE dummy;

	while(U2STAbits.URXDA)
	{
		if (U1STAbits.PERR || U1STAbits.FERR || U1STAbits.OERR)
		{
			U2STAbits.PERR = 0;
			U2STAbits.FERR = 0;
			U2STAbits.OERR = 0;
			dummy = U2RXREG;
		}
		else
		{

			if (((rxputidx2 + 1) & UART_RXBUF_MASK) != rxgetidx2)
			{
				rxBuf2[rxputidx2] = U2RXREG; 
			    rxputidx2++;
				rxputidx2 &= UART_RXBUF_MASK;
			} else dummy = U2RXREG;
		}
	}
	IFS1bits.U2RXIF = 0;
}

void __attribute__((interrupt, auto_psv)) _U2TXInterrupt(void)
{
    if (txputidx2 != txgetidx2) 
	{
        U2TXREG = txBuf2[txgetidx2];
        txgetidx2++;
		txgetidx2 &= UART_TXBUF_MASK;
    }
    else 
	{
        IEC1bits.U2TXIE = 0;   
    }
}

void InitUARTS(void)
{
	DISABLE_INTERRUPTS();
	txgetidx = txputidx = 0;
	rxgetidx = rxputidx = 0;
	txgetidx2 = txputidx2 = 0;
	rxgetidx2 = rxputidx2 = 0;
	IEC0bits.U1TXIE = 0;
	IEC0bits.U1RXIE = 1;
	IEC1bits.U2TXIE = 0;
	IEC1bits.U2RXIE = 1;
	IPC2bits.U1RXIP = 1;
	IPC3bits.U1TXIP = 1;
	ENABLE_INTERRUPTS();
}

BYTE ReadStringUART(BYTE *Dest, BYTE BufferLen)
{
	BYTE c;
	BYTE count = 0;

	while(BufferLen--)
	{
		*Dest = '\0';

		while(!DataRdyUART());
		c = ReadUART();

		if(c == '\r' || c == '\n')
			break;

		count++;
		*Dest++ = c;
	}

	return count;
}

/***************************************************************************
* Function Name     : putsUART                                            *
* Description       : This function puts the data string to be transmitted *
*                     into the transmit buffer (till NULL character)       * 
* Parameters        : unsigned int * address of the string buffer to be    *
*                     transmitted                                          *
* Return Value      : None                                                 *  
***************************************************************************/

void putsUART(char *buffer)
{
	while(*buffer) WriteUART(*buffer++);
}

void putrsUART(ROM char *buffer)
{
	while(*buffer) WriteUART(*buffer++);
}

/******************************************************************************
* Function Name     : getsUART2                                               *
* Description       : This function gets a string of data of specified length * 
*                     if available in the UxRXREG buffer into the buffer      *
*                     specified.                                              *
* Parameters        : unsigned int length the length expected                 *
*                     unsigned int *buffer  the received data to be           * 
*                                  recorded to this array                     *
*                     unsigned int uart_data_wait timeout value               *
* Return Value      : unsigned int number of data bytes yet to be received    * 
******************************************************************************/

unsigned int getsUART(unsigned int length,char *buffer,
                       unsigned int uart_data_wait)

{
    unsigned int wait = 0;
    while(length)                         /* read till length is 0 */
    {
        while(!DataRdyUART())
        {
            if(wait < uart_data_wait)
                wait++ ;                  /*wait for more data */
            else
                return(length);           /*Time out- Return words/bytes to be read */
        }
        wait=0;
		*buffer++ = ReadUART();
        length--;
    }

    return(length);                       /* number of data yet to be received i.e.,0 */
}


/*********************************************************************
* Function Name     : DataRdyUart                                    *
* Description       : This function checks whether there is any data *
*                     that can be read from the input buffer, by     *
*                     checking URXDA bit                             *
* Parameters        : None                                           *
* Return Value      : char if any data available in buffer           *
*********************************************************************/

char DataRdyUART(void)
{
BYTE ret;

	DISABLE_INTERRUPTS();
	if (rxputidx != rxgetidx) ret = 1;
	else ret = 0;
	ENABLE_INTERRUPTS();
	return(ret);
}


/*************************************************************************
* Function Name     : BusyUART                                           *
* Description       : This returns status whether the transmission       *  
*                     is in progress or not, by checking Status bit TRMT *
* Parameters        : None                                               *
* Return Value      : char info whether transmission is in progress      *
*************************************************************************/

char BusyUART(void)
{  
BYTE ret;

	DISABLE_INTERRUPTS();
	if (((txputidx + 1) & UART_TXBUF_MASK) == txgetidx) ret = 1;
	else ret = 0;
	ENABLE_INTERRUPTS();
	return ret;
}


/***************************************************************************
* Function Name     : ReadUART                                             *
* Description       : This function returns the contents of UxRXREG buffer *
* Parameters        : None                                                 *  
* Return Value      : unsigned int value from UxRXREG receive buffer       * 
***************************************************************************/

BYTE ReadUART(void)
{
    BYTE c;

	DISABLE_INTERRUPTS();
    c = rxBuf[rxgetidx];
    rxgetidx++;
	rxgetidx &= UART_RXBUF_MASK;
	ENABLE_INTERRUPTS();
	return c;
}


/*********************************************************************
* Function Name     : WriteUART2                                     *
* Description       : This function writes data into the UxTXREG,    *
* Parameters        : unsigned int data the data to be written       *
* Return Value      : None                                           *
*********************************************************************/

void WriteUART(BYTE data)
{
	DISABLE_INTERRUPTS();
	if (((txputidx + 1) & UART_TXBUF_MASK) != txgetidx)
	{
		if (!IEC0bits.U1TXIE)
		{
			U1TXREG = data;
			IEC0bits.U1TXIE = 1;
		}
		else
		{
	        txBuf[txputidx] = data;
			txputidx++;
			txputidx &= UART_TXBUF_MASK;
		}
	}
	ENABLE_INTERRUPTS();
}

/***************************************************************************
* Function Name     : putsUART2                                            *
* Description       : This function puts the data string to be transmitted *
*                     into the transmit buffer (till NULL character)       * 
* Parameters        : unsigned int * address of the string buffer to be    *
*                     transmitted                                          *
* Return Value      : None                                                 *  
***************************************************************************/

void putsUART2(char *buffer)
{
	while(*buffer) WriteUART2(*buffer++);
}

void putrsUART2(ROM char *buffer)
{
	while(*buffer) WriteUART2(*buffer++);
}

/******************************************************************************
* Function Name     : getsUART2                                               *
* Description       : This function gets a string of data of specified length * 
*                     if available in the UxRXREG buffer into the buffer      *
*                     specified.                                              *
* Parameters        : unsigned int length the length expected                 *
*                     unsigned int *buffer  the received data to be           * 
*                                  recorded to this array                     *
*                     unsigned int uart_data_wait timeout value               *
* Return Value      : unsigned int number of data bytes yet to be received    * 
******************************************************************************/

unsigned int getsUART2(unsigned int length,char *buffer,
                       unsigned int uart_data_wait)

{
    unsigned int wait = 0;
    while(length)                         /* read till length is 0 */
    {
        while(!DataRdyUART2())
        {
            if(wait < uart_data_wait)
                wait++ ;                  /*wait for more data */
            else
                return(length);           /*Time out- Return words/bytes to be read */
        }
        wait=0;
		*buffer++ = ReadUART2();
        length--;
    }

    return(length);                       /* number of data yet to be received i.e.,0 */
}


/*********************************************************************
* Function Name     : DataRdyUart2                                   *
* Description       : This function checks whether there is any data *
*                     that can be read from the input buffer, by     *
*                     checking URXDA bit                             *
* Parameters        : None                                           *
* Return Value      : char if any data available in buffer           *
*********************************************************************/

char DataRdyUART2(void)
{
BYTE ret;

	DISABLE_INTERRUPTS();
	if (rxputidx2 != rxgetidx2) ret = 1;
	else ret = 0;
	ENABLE_INTERRUPTS();
	return(ret);
}


/*************************************************************************
* Function Name     : BusyUART2                                          *
* Description       : This returns status whether the transmission       *  
*                     is in progress or not, by checking Status bit TRMT *
* Parameters        : None                                               *
* Return Value      : char info whether transmission is in progress      *
*************************************************************************/

char BusyUART2(void)
{  
BYTE ret;

	DISABLE_INTERRUPTS();
	if (((txputidx2 + 1) & UART_TXBUF_MASK) == txgetidx2) ret = 1;
	else ret = 0;
	ENABLE_INTERRUPTS();
	return ret;
}


/***************************************************************************
* Function Name     : ReadUART2                                            *
* Description       : This function returns the contents of UxRXREG buffer *
* Parameters        : None                                                 *  
* Return Value      : unsigned int value from UxRXREG receive buffer       * 
***************************************************************************/

BYTE ReadUART2(void)
{
    BYTE c;

	DISABLE_INTERRUPTS();
    c = rxBuf2[rxgetidx2];
    rxgetidx2 ++;
	rxgetidx2 &= UART_RXBUF_MASK;
	ENABLE_INTERRUPTS();
	return c;
}


/*********************************************************************
* Function Name     : WriteUART2                                     *
* Description       : This function writes data into the UxTXREG,    *
* Parameters        : unsigned int data the data to be written       *
* Return Value      : None                                           *
*********************************************************************/

void WriteUART2(BYTE data)
{
	DISABLE_INTERRUPTS();
	if (((txputidx2 + 1) & UART_TXBUF_MASK) != txgetidx2)
	{
		if (!IEC1bits.U2TXIE)
		{
			U2TXREG = data;
			IEC1bits.U2TXIE = 1;
		}
		else
		{
	        txBuf2[txputidx2] = data;
			txputidx2++;
			txputidx2 &= UART_TXBUF_MASK;
		}
	}
	ENABLE_INTERRUPTS();
}

#endif	//STACK_USE_UART
