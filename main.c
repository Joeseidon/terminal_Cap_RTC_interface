/*
 * -------------------------------------------
 *    MSP432 DriverLib - v3_21_00_05 
 * -------------------------------------------
 *
 * --COPYRIGHT--,BSD,BSD
 * Copyright (c) 2016, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --/COPYRIGHT--*/
/******************************************************************************
 * MSP432 Terminal and Capacitive Touch Set RTC
 *
 * Description: This program uses a combination of UART communication and
 *  capacitive touch to set the MSP432 on-board RTC. Once the time has been set,
 *  this program will print out the time every 15 seconds.
 * Author: Joseph Cutino
*******************************************************************************/


/* NOTE */

//IF this code is used in another program, make sure you import the changes to the startup_msp432p401r_css.c file


/* DriverLib Includes */
#include "driverlib.h"

// --- Standard Includes ---
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "msp.h"
#include "structure.h"
#include "CTS_Layer.h"
#include "CTS_HAL.h"

#include "clockConfig.h"

#include "sysTick.h"

// --- UART Configuration Parameter ---
const eUSCI_UART_Config uartConfig =
{
        //Configured for 9600 Baud Rate
        EUSCI_A_UART_CLOCKSOURCE_SMCLK,          // SMCLK Clock Source
        1284,                                     // BRDIV = ???
        0,                                       // UCxBRF = ?
        0,                                       // UCxBRS = ?
        EUSCI_A_UART_NO_PARITY,                  // No Parity
        EUSCI_A_UART_LSB_FIRST,                  // MSB First
        EUSCI_A_UART_ONE_STOP_BIT,               // One stop bit
        EUSCI_A_UART_MODE,                       // UART mode
        EUSCI_A_UART_LOW_FREQUENCY_BAUDRATE_GENERATION  // Low Frequency Mode
};

int fputc(int _c, register FILE *_fp)
{
    while(!(UCA0IFG&UCTXIFG));
    UCA0TXBUF = (unsigned char) _c;
    return((unsigned char)_c);
}
int fputs(const char *_ptr, register FILE *_fp)
{
    unsigned int i, len;
    len = strlen(_ptr);
    for(i=0 ; i<len ; i++)
    {
        while(!(UCA0IFG&UCTXIFG));
        UCA0TXBUF = (unsigned char) _ptr[i];
    }
return len;
}

void UART_init(void){

    // Selecting P1.2 and P1.3 in UART mode.
    MAP_GPIO_setAsPeripheralModuleFunctionInputPin(GPIO_PORT_P1,GPIO_PIN2 | GPIO_PIN3, GPIO_PRIMARY_MODULE_FUNCTION);
    MAP_UART_initModule(EUSCI_A0_BASE, &uartConfig);         // Configuring UART Module
    MAP_UART_enableModule(EUSCI_A0_BASE);               // Enable UART module
}

uint16_t raw_count[3];
uint16_t delta_count[3];

/* Statics */
 static volatile RTC_C_Calendar newTime;

/* Time is November 12th 1955 10:03:00 PM */
RTC_C_Calendar currentTime =
{
     1,
     3,
     10,
     12,
     11,
     00,
     2018
};

RTC_C_Calendar newtime;

//Used to track user input
typedef enum time_set{
    HOURS = 0,
    MINUTES,
    DAY,
    MONTH
}time_set_stage;

//Used to track user input for RTC
typedef struct time_step{
    time_set_stage stage;
    char *prompt;
    int increment_mod;
}time_step;

time_set_stage current_Stage = HOURS;

time_step setup_steps[4] = {
    {HOURS,  "Hours:  ",13},
    {MINUTES,"Minutes:",60},
    {DAY,    "Day:    ",32},
    {MONTH,  "Month:  ",13}
};

//Global Variables
int SET_TIME = false;
uint16_t value = 0;
int reset_time = 0;
int print_count = 0;
int second_count = 0;

int main(void)
{
    //Disable Watchdog
    WDTCTL = WDTPW | WDTHOLD;

    //Setup clock
    clockStartUp();

    //Enable UART for User interface
    UART_init();

    /* Config RTC */
    /* Configuring pins for peripheral/crystal usage */
    MAP_GPIO_setAsPeripheralModuleFunctionOutputPin(GPIO_PORT_PJ,
        GPIO_PIN0 | GPIO_PIN1, GPIO_PRIMARY_MODULE_FUNCTION);

    /* Starting LFXT in non-bypass mode without a timeout. */
    CS_startLFXT(false);

    /* Specify an interrupt to assert every minute */
    MAP_RTC_C_setCalendarEvent(RTC_C_CALENDAREVENT_MINUTECHANGE);

    /* Enable interrupt for RTC Ready Status, which asserts when the RTC
     * Calendar registers are ready to read.
     * Also, enable interrupts for the Calendar alarm and Calendar event. */
    MAP_RTC_C_clearInterruptFlag(
            RTC_C_CLOCK_READ_READY_INTERRUPT | RTC_C_TIME_EVENT_INTERRUPT);
    MAP_RTC_C_enableInterrupt(
            RTC_C_CLOCK_READ_READY_INTERRUPT | RTC_C_TIME_EVENT_INTERRUPT);

    //initalize baseline measurement
    TI_CAPT_Init_Baseline(&my_keys);

    //update baseline measurement (average 30 measurements)
    TI_CAPT_Update_Baseline(&my_keys, 30);

    sysTickInit();

    printf("Clock Init:");

    /* Enable interrupts and go to sleep. */
    MAP_Interrupt_enableInterrupt(INT_RTC_C);
    MAP_Interrupt_enableMaster();

    while(1)
    {
        //Record element delta and raw counts
        TI_CAPT_Custom(&my_keys, delta_count);
        TI_CAPT_Raw(&my_keys, raw_count);


        while(!SET_TIME){
            SysTick_delay(20);
            //Inquire if a button has been pressed
            const struct Element *tmp = TI_CAPT_Buttons(&my_keys);

            if(print_count == 0){
                printf("\n\r%s\n",setup_steps[current_Stage].prompt);
                print_count++;
            }
            printf("\b\b  \b\b%02.0d",value);

            //Determine if a button has been pressed
            if(&select_element == tmp)
            {
                print_count = 0;

                uint16_t temp = value;
                //store setting in RTC struct
                switch(current_Stage){
                    case HOURS:
                        currentTime.hours = temp;
                        current_Stage = MINUTES;
                        break;

                    case MINUTES:
                        currentTime.minutes = temp;
                        current_Stage = DAY;
                        break;

                    case DAY:
                        currentTime.dayOfmonth = temp;
                        current_Stage = MONTH;
                        break;

                    case MONTH:
                        currentTime.month = temp;
                        current_Stage = HOURS;
                        SET_TIME=1;//break loop
                        break;
                }
                SysTick_delay(1);

                value = 0;
                SysTick_delay(100);

                //Wait for Button release
                while(TI_CAPT_Buttons(&my_keys) == &select_element){
                        ;
                }
            }else if(&down_element == tmp){
                SysTick_delay(1);

                if(value==0){
                    //if positive, decrement is allowed
                    value = setup_steps[current_Stage].increment_mod - 1;
                }else{
                    value--;
                }

                //Wait for Button Release
                while(TI_CAPT_Buttons(&my_keys) == &down_element){
                    ;
                }

            }else if(&up_element == tmp){
                SysTick_delay(1);
                value = (value + 1) % setup_steps[current_Stage].increment_mod;

                //Wait for Button Release
                while(TI_CAPT_Buttons(&my_keys) == &up_element){
                    ;
                }
            }else{
                SysTick_delay(10);
            }
        }

        if(SET_TIME){
            SET_TIME=false;
            /* Initializing RTC with current time as described in time in
             * definitions section */
            MAP_RTC_C_initCalendar(&currentTime, RTC_C_FORMAT_BINARY);

            /* Start RTC Clock */
            MAP_RTC_C_startClock();

            printf("\nTime Set\n\r");
            char data[10];
            while(1){
                if(second_count==15){
                    newtime = MAP_RTC_C_getCalendarTime();
                    sprintf(data,"%02.0d:%02.0d    %02.0d/%02.0d",
                                newtime.hours,
                                newtime.minutes,
                                newtime.month,
                                newtime.dayOfmonth);
                    printf("Current Time: %s\n\r",data);
                    second_count = 0;
                }
                if(reset_time){

                    reset_time = 0;
                    newtime = MAP_RTC_C_getCalendarTime();
                    sprintf(data,"%02.0d:%02.0d    %02.0d/%02.0d",
                                newtime.hours,
                                newtime.minutes,
                                newtime.month,
                                newtime.dayOfmonth);
                    printf("One Minute Passed. Time: \n\r%s\n\r",data);
                }
            }
        }
    }
}

/* RTC ISR */
void RTC_C_IRQHandler(void)
{
    uint32_t status;

    status = MAP_RTC_C_getEnabledInterruptStatus();
    MAP_RTC_C_clearInterruptFlag(status);

    if (status & RTC_C_CLOCK_READ_READY_INTERRUPT)
    {
        second_count++;
    }
    if (status & RTC_C_TIME_EVENT_INTERRUPT)
    {
        /* Interrupts every minute - Set breakpoint here */
        reset_time = 1;
    }
}

