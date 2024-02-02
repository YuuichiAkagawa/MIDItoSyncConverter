/*
 * MIDI to Sync Converter for ATtiny202
 * Copyright (C) 2023-2024 Yuuichi Akagawa
 *
*/

/* 
 * Configuration
 *   20MHz 3.3V
 *
 *  FUSEs
 *  ------------
 *  0x001280: 00 
 *  0x001281: 00 
 *  0x001282: 02 
 *  0x001283: FF 
 *  0x001284: 00 
 *  0x001285: F6 
 *  0x001286: 07 
 *  0x001287: 00 
 *  0x001288: 00 
 *  ------------
 */

#include <avr/io.h>
#include <avr/interrupt.h>

/* CLK_PER = 20MHz/6 */
#define F_CPU 3333333UL
#define USART0_BAUD_RATE(BAUD_RATE) ((float)(F_CPU * 64 / (16 * (float)BAUD_RATE)) + 0.5)

/* 2PPQ */
#define CLOCK_PERIOD (24/2)

#define SYNCPIN PIN1_bm
#define MODEPIN PIN2_bm
uint8_t countClock = 0;
bool isStart = false;

void USART0_init(void);
uint8_t USART0_readChar(void);
void TCA0_init(void);
void startSync(void);

/** 
 * Init USART0 for receiving MIDI messages
 */
void USART0_init(void)
{
    /* UART RX enable */
    PORTA.DIR &= ~PIN7_bm;
    USART0.CTRLB |= USART_RXEN_bm;

    /*  TX not used */
    //PORTA.DIR |= PIN6_bm;
    //USART0.CTRLB |= USART_RXEN_bm | USART_TXEN_bm;

    /* Baud rate setting */
    int8_t sigrow_val = SIGROW.OSC16ERR3V;
    int32_t baud_reg_val = (int32_t)USART0_BAUD_RATE(31250);
    baud_reg_val *= (1024 + sigrow_val);
    baud_reg_val /= 1024;
    USART0.BAUD = (uint16_t)baud_reg_val;
}

/**
 * Receive MIDI message
 */
uint8_t USART0_readChar(void)
{
    /* Wait until receive any */
    while (!(USART0.STATUS & USART_RXCIF_bm))
    {
        ;
    }
    return USART0.RXDATAL;
}

/**
 * Init TCA0 for the width of the Sync signal
 */
void TCA0_init(void)
{
    /* set the period (5ms) */
    TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV16_gc;  // set clock source (sys_clk/64)
    TCA0.SINGLE.PER = (1042);                        // 5ms  5e-3*(20e6/6/16)=1041.6
    //TCA0.SINGLE.PER = (3125);                      // 15ms  15e-3*(20e6/6/16)=3125

    /* set Normal mode */
    TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_NORMAL_gc;

    /* enable overflow interrupt */
    TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;

    /* disable event counting */
    TCA0.SINGLE.EVCTRL &= ~(TCA_SINGLE_CNTEI_bm);
}

/**
 * Timer interrupt hander (STOP Sync signal)
 */
ISR(TCA0_OVF_vect)
{
    PORTA.OUTTGL = SYNCPIN;                        // Fall SYNC pin
    TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;      // clearing the flag
    TCA0.SINGLE.CTRLA &= ~TCA_SINGLE_ENABLE_bm;    // stop timer
}

/**
 * Start Sync signal
 */
void startSync(void)
{
    countClock = 0;
    PORTA.OUT |= SYNCPIN;                          // Rise SYNC pin
    TCA0.SINGLE.CTRLA |= TCA_SINGLE_ENABLE_bm;     // Start Timer (one shot)
}

/**
 * main
 */
int main() {
    _PROTECTED_WRITE(CLKCTRL_MCLKCTRLA, 0x00);

    /* Init peripherals */
    PORTA.DIR |= (SYNCPIN);
    PORTA.PIN2CTRL = PORT_PULLUPEN_bm;  // enable pullup for MODEPIN

    USART0_init();
    cli(); 
    TCA0_init();
    sei();

    /* main loop */
    while(1){
        /* Wait for incoming MIDI messages */
        uint8_t c = USART0_readChar();

        switch(c){
            case 0xfa : // System message: Start
                isStart = true;
                countClock = CLOCK_PERIOD - 1;
                break;
            case 0xfb : // System message: Continue
                isStart = true;
                break;
            case 0xfc : // System message: Stop
                isStart = false;
                break;
            case 0xf8 : // System message: Timing clock
                if ( !(PORTA.IN & MODEPIN) ) { // MIDI Start sync mode (MODEPIN is low)
                    if( isStart == false ) {
                        break;
                    }
                }
                /* Count the number of timing clocks */
                countClock++;
                if( countClock == CLOCK_PERIOD ) {
                    startSync();
                }
                break;
            default:
                /* ignore others */
                break;
        }
    }
}
