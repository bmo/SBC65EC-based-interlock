/*********************************************************************
*       UDP example program.
*********************************************************************
* FileName:        main.c
* Processor:       PIC18
* Complier:        MCC18 v1.00.50 or higher
*                  HITECH PICC-18 V8.10PL1 or higher
* Company:         Microchip Technology, Inc.
 ********************************************************************/

 /*********************************************************************
 * Software License Agreement
 *
 * The software supplied herewith is owned by Modtronix Engineering, and is
 * protected under applicable copyright laws. The software supplied herewith is
 * intended and supplied to you, the Company customer, for use solely and
 * exclusively on products manufactured by Modtronix Engineering. The code may
 * be modified and can be used free of charge for commercial and non commercial
 * applications. All rights are reserved. Any use in violation of the foregoing
 * restrictions may subject the user to criminal sanctions under applicable laws,
 * as well as to civil liability for the breach of the terms and conditions of this license.
 *
 * THIS SOFTWARE IS PROVIDED IN AN 'AS IS' CONDITION. NO WARRANTIES, WHETHER EXPRESS,
 * IMPLIED OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. THE
 * COMPANY SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL OR
 * CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
 **********************************************************************
 * File History
 *
 * 2005-12-22, David Hosken (DH):
 *    - Initial version
 *********************************************************************/


//////////////////////////////////////////////////////////////////////
// Documentation
/**
@defgroup mod_examples_udp5 UDP Client Example
@ingroup  mod_examples

This example is contained in "./udp/src/main_udp5.c"

This example program shows how to use the TCP/IP stack in client mode.
It sends a UDP datagram each time PIC port pin B0 is 0. The datagram
is sent to socket 10.1.0.101:54124 (IP address 10.1.0.101, port 54124).

For testing this example, the free Netix Network utility program (see
www.modtronix.com/soft/netix) can be used. This program has a UDP terminal
that can be used for sending and receiving UDP Datagrams.

The following modules are included in this project:
- ARP
- ICMP
- IP
- UDP
********************************************************************/


/*
 * Following define uniquely define this file as main entry/application in whole
 * project, there should only be one such definition.
 */
#define THIS_IS_STACK_APPLICATION

#include <string.h>

#include "projdefs.h"

#include "net\cpuconf.h"	//Set default CPU configuration bits
#include "net\stacktsk.h"
#include "net\tick.h"
#include "net\helpers.h"
#include "net\udp.h"
#include "net\arp.h"
#include "net\arptsk.h"

#if defined(STACK_USE_HTTP_SERVER)
#include "net\http.h"
#endif



//Create a UDP socket for receiving and sending data
static UDP_SOCKET udpSocketUser = INVALID_UDP_SOCKET;

//UDP State machine
#define SM_UDP_SEND_ARP     0
#define SM_UDP_WAIT_RESOLVE 1
#define SM_UDP_RESOLVED     2
static BYTE smUdp = SM_UDP_SEND_ARP;

static void InitializeBoard(void);
static void ServiceBoard(void);
static void ServiceBoard1(void);

static BYTE old_state;
static BYTE old_inputs;

/////////////////////////////////////////////////
//High Interrupt ISR
#if defined(MCHP_C18)
    #pragma interrupt HighISR save=section(".tmpdata")
    void HighISR(void)
#elif defined(HITECH_C18)
    #if defined(STACK_USE_SLIP)
        extern void MACISR(void);
    #endif
    void interrupt HighISR(void)
#endif
{
    //TMR0 is used for the ticks
    if (INTCON_TMR0IF)
    {
        TickUpdate();

        #if defined(STACK_USE_SLIP)
        MACISR();
        #endif

        INTCON_TMR0IF = 0;
    }
}

#if defined(MCHP_C18)
#pragma code highVector=HIVECTOR_ADR
void HighVector (void)
{
    _asm goto HighISR _endasm
}
#pragma code /* return to default code section */
#endif

/*
 * Main entry point.
 */
void main(void)
{
    TICK8  tsecWait = 0;           //General purpose wait timer
    TICK16 tsecMsgSent = 0;        //Time last message was sent
    TICK16 tsecBlinker = 0;  
    BYTE main_state;				// what are the inputs
	BYTE main_inputs;				// who has the transmit
    char c;
    NODE_INFO udpServerNode;

    //Initialize AppConfig structure
    appcfgInit();
    
    //Initialize any application specific hardware.
    InitializeBoard();

    //Initialize all stack related components.
    TickInit();

    //Initialize the TCP/IP stack
    StackInit();


    /////////////////////////////////////////////////
    //Initialize UDP socket

    //Initialize remote IP and address with 10.1.0.101. The MAC address is
    //is not intialized yet, but after we receive an ARP responce.
    //Configure for local port 54123 and remote port 54124.
    udpServerNode.IPAddr.v[0] = 255;
    udpServerNode.IPAddr.v[1] = 255;
    udpServerNode.IPAddr.v[2] = 255;
    udpServerNode.IPAddr.v[3] = 255;
    udpSocketUser = UDPOpen(54123, &udpServerNode, 54124);
	//udpSocketUser = UDPOpen(54123, NULL, 54124);
	smUdp = SM_UDP_RESOLVED;
    
    //An error occurred during the UDPOpen() function
    if (udpSocketUser == INVALID_UDP_SOCKET) {
        //Add user code here to take action if required!
    }

    /*
     * Once all items are initialized, go into infinite loop and let stack items execute
     * their tasks. If the application needs to perform its own task, it should be done at
     * the end of while loop. Note that this is a "co-operative mult-tasking" mechanism where
     * every task performs its tasks (whether all in one shot or part of it) and returns so
     * that other tasks can do their job. If a task needs very long time to do its job, it
     * must broken down into smaller pieces so that other tasks can have CPU time.
     */
    while(1)
    {
	    ServiceBoard();
		if (TickGetSecDiff(tsecBlinker) >= (TICK16)1) 
		{
			tsecBlinker = TickGetSec();     //Update with current time
			//Toggle system LED 
#ifdef BLINKTIME
            TRISB_RB6 = 0;
            LATB6 ^= 1;
#endif
		}
        switch (smUdp) {
        case SM_UDP_SEND_ARP:
            if (ARPIsTxReady()) {
                tsecWait = TickGet8bitSec();   //Remember when we sent last request
                
                //Send ARP request for given IP address
                ARPResolve(&udpServerNode.IPAddr);
                
                smUdp = SM_UDP_WAIT_RESOLVE;
            }
            break;
        case SM_UDP_WAIT_RESOLVE:
            //The IP address has been resolved, we now have the MAC address of the
            //node at 10.1.0.101
            if (ARPIsResolved(&udpServerNode.IPAddr, &udpServerNode.MACAddr)) {
                smUdp = SM_UDP_RESOLVED;
            }
            //If not resolved after 2 seconds, send next request
            else {
                if (TickGetDiff8bitSec(tsecWait) >= (TICK8)2) {
                    smUdp = SM_UDP_SEND_ARP;
                }
            }
            break;
        case SM_UDP_RESOLVED:
            if ( 1 || !PORTB_RB0) {
                //Send a message every second for as long as PIC port pin B0 is = 0
                if ((TickGetSecDiff(tsecMsgSent) >= (TICK16)1) ||
					((main_state != old_state) || (main_inputs != old_inputs))) {
                    //Checks if there is a transmit buffer ready for accepting data, and that the given socket
                    //is valid (not equal to INVALID_UDP_SOCKET for example)
                    if (UDPIsPutReady(udpSocketUser)) {
                        tsecMsgSent = TickGetSec();     //Update with current time

                        //Send a UDP Datagram with one byte only indicating the status We are only interrested in the first byte of the message.
                        UDPPut('H');UDPPut('E');UDPPut('L');UDPPut('L');UDPPut('O');
						UDPPut(old_state);
						UDPPut(old_inputs);
						main_state = old_state;
						main_inputs = old_inputs;
                        //Send contents of transmit buffer, and free buffer
                        UDPFlush();
                       	
						//Toggle system LED each time a message is sent
                    	TRISB_RB6 = 0;
                    	LATB6 ^= 1; 
                    }

                    
                }
            }
            break;
        }

        //This task performs normal stack task including checking for incoming packet,
        //type of packet and calling appropriate stack entity to process it.
        StackTask();

        //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        // ADD USER CODE HERE
        //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    }
}

//Relays
#define MXD2R_RLY1   LATB4
#define MXD2R_RLY2   LATB5
#define MXD2R_RLY3	 LATC2

#define TRIS_RLY1  TRISB_RB4
#define TRIS_RLY2  TRISB_RB5
#define TRIS_RLY3  TRISC_RC2

#define MXD2R_I1    PORTA_RA0
#define MXD2R_I2    PORTA_RA1
#define MXD2R_I3    PORTA_RA2
#define MXD2R_O1    LATA0
#define MXD2R_O2    LATA1
#define MXD2R_O3    LATA2
#define MXD2R_O4    LATC2
#define MXD2R_O5    LATC1
#define MXD2R_O6    LATC0

//I/O port direction
#define MXD2R_IO1_DIR   TRISA_RA0
#define MXD2R_IO2_DIR   TRISA_RA1
#define MXD2R_IO3_DIR   TRISA_RA2

//Defines used in evtLatchExIn and evtLatchExInUpdt variables
#define IO1_LAT_MASK    0x0001ul
#define IO2_LAT_MASK    0x0002ul
#define IO3_LAT_MASK    0x0004ul

void mxd2rInit(void)
{
    //BYTE i;
    
    //Relay ports are outputs
    TRIS_RLY1 = 0;
    TRIS_RLY2 = 0;
	TRIS_RLY3 = 0;

    //IO variables
    old_state = 0;
	old_inputs = 0;

	MXD2R_RLY1 = 0;
	MXD2R_RLY2 = 0;
	MXD2R_RLY3 = 0;

	
}

void stateToRly(BYTE i)
{
	if (i == 0) {
		MXD2R_RLY1 = 0;
		MXD2R_RLY2 = 0;
		MXD2R_RLY3 = 0;
		MXD2R_O6 = 0;
		MXD2R_O5 = 0;
		MXD2R_O4 = 0;
	}
	if (i&0x01) { 
		MXD2R_RLY1 = 1; MXD2R_RLY2 = 0; MXD2R_RLY3 = 0;
		MXD2R_O6 = 1; MXD2R_O5 = 0;
		//MXD2R_O4 = 1;
	}
	
	if (i&0x04) {
		MXD2R_RLY2 = 1; MXD2R_RLY1 = 0; MXD2R_RLY3 = 0;
		MXD2R_O5 = 1; MXD2R_O6 = 0;
		//MXD2R_O4 = 1;
	}
	
//
	if (i&0x10) {
		MXD2R_RLY3 = 1;MXD2R_RLY1 = 0; MXD2R_RLY2 = 0;
		MXD2R_O5 = 0; 
		MXD2R_O6 = 0 ; // 
	}
	

	//MXD2R_RLY1 = !!(i & 0x01);
	//MXD2R_RLY2 = !!(i & 0x04);
}

BYTE decider(BYTE i)
{
    if (i & 0x04)
	  return 0x04;
	if (i & 0x01)
	  return 0x01;
	if (i & 0x10)
	  return 0x10;
	return 0x00;
}
void ppp(BYTE i) 
{
	BYTE next_state=old_state;
	if ((old_state == 0) && (old_inputs == 0) && (i != 0)) // something turned on, but make sure previous state was all off
	{
		next_state = decider(i);		
	} 
	if ((old_state & i) == 0) // turn off...
	{
		next_state = decider(i);
	}
	old_state = next_state;
	old_inputs = i;
    stateToRly(old_state);
}

static void ServiceBoard(void) 
{
	BYTE input_value;
	input_value = PORTF ; // invert inputs -- use active LOW
	input_value = (input_value & 0x15) ^ 0x15;
	TRISB_RB6 = 0;
    //if ((input_value & 0x01)==0 && (input_value & 0x04)==0) //
    //if (input_value==0) 
	//   LATB6 = 0;
	//else
	//   LATB6 = 1;
	
	ppp(input_value);
}
static void ServiceBoard1(void)
{
    //PORTA = 0x00;
	TRISA = 0xFC;
	
	// set relays as outputs
	TRIS_RLY1 = 0;
	TRIS_RLY2 = 0;
	
	MXD2R_RLY1 = PORTF_RF0 ? 0 : 1;
	MXD2R_RLY2 = PORTF_RF2 ? 0 : 1;
}
/**
 * Initialize the boards hardware
 */
static void InitializeBoard(void)
{
	#if (defined(MCHP_C18) && (defined(__18F458) || defined(__18F448))) \
		|| (defined(HITECH_C18) && (defined(_18F458) || defined(_18F448)))
		CMCON  = 0x07; /* Comparators off CM2:CM0 = 111 for PIC 18F448 & 18F458 */
	#endif

    //Enable external pull-ups on Port B
    INTCON2_RBPU = 0;

    /////////////////////////////////////////////////
    //Initialize USART1 Control registers
    TXSTA = 0b00100100;     //High BRG speed
    RCSTA = 0b10010000;
    BAUDCON = 0b00001000;
    //#define BAUD 1040   /* 9600 BAUD at 40MHz clock */
    //#define BAUD 520    /* 19200 BAUD at 40MHz clock */
    //#define BAUD 259    /* 38400 BAUD at 40MHz clock */
    #define BAUD 172    /* 57600 BAUD at 40MHz clock */
    //#define BAUD 86     /* 115200 BAUD at 40MHz clock */
    SPBRG =  (BYTE)BAUD;
    SPBRGH = (BYTE)(BAUD >> 8);


    /////////////////////////////////////////////////
    //Initialize ports    
		
	// Not a/d function
	ADCON1 = 0x0F; // disable all a/d
	ADCON0 = 0x00; // no analog input

	PORTA = 0x00; // clear it.
    TRISA = 0b11111111; //All inputs

    //Ensure port TRISB 0 is always an inputs when NIC INT0 is enabled:
    #if defined(NIC_DISABLE_INT0)
        TRISB = 0b11111111; //All inputs
    #else
        //B0 = Input from RTL8019AS
        TRISB = 0b11111111 | 0x01;
    #endif

    //Ensure following TRISC ports are always inputs:
    //C3 = Is controlled by I2C unit
    //C4 = Is controlled by I2C unit
    //C6 = Is controlled by USART
    //C7 = Is controlled by USART
    TRISC = 0b10011000 | 0xd8;

    //Set PORT G and F direction bits
    TRISF = 0b11111111; //All inputs
    
    //Ensure TRISG 4 is always an input, it is connected to IOCHRDY
    TRISG = 0b11111111 | 0x10;
    
    
    //Set system led port as an output, and switch it on
    TRISB_RB6 = 0;
    LATB6 = 0;    //Switch system LED on

    //Enable interrupts
    T0CON = 0;
    INTCON_GIEH = 1;
    INTCON_GIEL = 1;

	mxd2rInit();

}

