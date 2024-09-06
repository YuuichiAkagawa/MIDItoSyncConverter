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
#include <avr/eeprom.h>

/* CLK_PER = 20MHz/6 */
#define F_CPU 3333333UL
#define USART0_BAUD_RATE(BAUD_RATE) ((float)(F_CPU * 64 / (16 * (float)BAUD_RATE)) + 0.5)

/* PPQ (default: 2PPQ)*/
#define CLOCK_BASE (24)
#define DEFAULT_PPQN (2)
#define SYSEX_DEVICE_ID (0x49)

/* Sync Pulse Width (SPW) */
#define SPW_5MS  (1)
#define SPW_15MS (2)
#define SPW_RESERVED (3)
#define DEFAULT_SPW SPW_5MS

/* EEPROM address*/
#define EEPROM_ADDRESS_PPQN 0x00
#define EEPROM_ADDRESS_SPW  0x01
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
void TCA0_init(uint8_t spw)
{
    /* set the period */
    TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV16_gc;  // set clock source (sys_clk/64)
    switch(spw){
        case SPW_5MS:
            TCA0.SINGLE.PER = (1042);                // 5ms  5e-3*(20e6/6/16)=1041.6
            break;
        case SPW_15MS:
            TCA0.SINGLE.PER = (3125);                // 15ms  15e-3*(20e6/6/16)=3125
            break;
        default:
            TCA0.SINGLE.PER = (1042);                // default 5ms
            break;
    }

    /* set Normal mode */
    TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_NORMAL_gc;

    /* enable overflow interrupt */
    TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;

    /* disable event counting */
    TCA0.SINGLE.EVCTRL &= ~(TCA_SINGLE_CNTEI_bm);
}

/**
 * Timer interrupt handler (STOP Sync signal)
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

#define eepromIsReady()	bit_is_clear (NVMCTRL.STATUS, NVMCTRL_EEBUSY_bp)
#define eepromBusyWait() do {} while (!eepromIsReady())
/**
 * EEPROM read
 */
uint8_t eepromRead(uint8_t eepromAddress){
//   uint8_t *eepromAddress = (uint8_t*)addr;
   eepromBusyWait(); 
   uint8_t byteOfData = eeprom_read_byte((uint8_t*)eepromAddress);
   return byteOfData;
}

/**
 * EEPROM write
 */
void eepromWrite(uint8_t eepromAddress, uint8_t data){
//   uint8_t *eepromAddress = (uint8_t*)addr;
   eeprom_write_byte((uint8_t*)eepromAddress, data);
}

/**
 * Set and Save PPQN
 * c: PPQN : 1 to 24
 *   bit|7|6|5|4|3|2|1|0|
 *      |0|0|0|PPQN 0-24|
 */
uint8_t setPPQN(uint8_t c, uint8_t ppqn)
{
    uint8_t newPpqn = ppqn;

    // bit 6,5 
    // 00 : PPQN mode, 01-11: SPW mode
    if ((c & 0x60) != 0) {
        return newPpqn;
    }

    //Extracting PPQN value
    c = c & 0x1f;
    if( 0 < c && c <= 24 ) { // Valid range is 1 to 24
        newPpqn = c;
        eepromWrite(EEPROM_ADDRESS_PPQN, newPpqn);
    }
    return newPpqn;
}

/**
 * Set and Save clock pluse width
 * c: SPW | bit 6,5 = 00: PPQN mode, 01: SPW 5ms, 10: SPW 15ms, 11: Reserved
 *   bit|7|6|5|4|3|2|1|0|
 *      |0|X|X|1|1|1|1|1|
 */
void setSPW(uint8_t c)
{
    // Bits 0 through 4 must all be 1
    if( (c & 0x1f) != 0x1f ) { 
        return;
    }

    //Extracting SPW value
    uint8_t newSpw = (c >> 5) & 3;
    if ( newSpw == 0 ){
        return;
    }
    //Save SPW value
    eepromWrite(EEPROM_ADDRESS_SPW, newSpw);
    //set SPW
    cli(); 
    TCA0_init(newSpw);
    sei();
}

enum class SysexState : uint8_t {
    idle = 0,
    start = 1,
    devid = 2,
    id1 = 3,
    id2 = 4,
    change = 5,
    end = 6
};

/**
 * main
 */
int main() {
    _PROTECTED_WRITE(CLKCTRL_MCLKCTRLA, 0x00);

    /* Init peripherals */
    PORTA.DIR |= (SYNCPIN);
    PORTA.PIN2CTRL = PORT_PULLUPEN_bm;  // enable pullup for MODEPIN

    /* EEPROM init & PPQN setting */
    uint8_t PPQN;
    uint8_t tmpPPQN = eepromRead(EEPROM_ADDRESS_PPQN);
    if( tmpPPQN == 0xff ){  //initial value?
        PPQN = DEFAULT_PPQN;
        eepromWrite(EEPROM_ADDRESS_PPQN, PPQN);
    } else {
        PPQN = tmpPPQN;
    }
    uint8_t CLOCK_PERIOD =  (CLOCK_BASE/PPQN);

    /* EEPROM init & SPW setting */
    uint8_t tmpSpw = eepromRead(EEPROM_ADDRESS_SPW);
    if( tmpSpw == 0xff ){   //initial value?
        tmpSpw = DEFAULT_SPW;
        eepromWrite(EEPROM_ADDRESS_SPW, DEFAULT_SPW);
    }

    USART0_init();
    cli(); 
    TCA0_init(tmpSpw);
    sei();

    /* SysEx state */
    SysexState  sysexState = SysexState::idle;

    /* main loop */
    while(1){
        /* Wait for incoming MIDI messages */
        uint8_t c = USART0_readChar();

        /* Processing SysEx                                     */
        /*   Acceptable Messages                                */
        /*     device ID:49                                     */
        /*      F0 7E 49 0B 02 nn F7   : Change PPQN/SPW        */
        /*         PPQN nn = 0x01,0x02,0x03,0x04,0x08,0x0c,0x18 */
        /*         SPW  nn = 0x3F:5ms, 0x5F:15ms, 0x7F:Reserved */
        if( sysexState != SysexState::idle ){
            switch(sysexState){
                case SysexState::start:
                    if( c == 0x7e ) {// non realtime universal system exclusive message
                        sysexState = SysexState::devid;
                        break;
                    }else{
                        sysexState = SysexState::idle;
                        break;
                    }
                case SysexState::devid:
                    if( c == SYSEX_DEVICE_ID ) {// device ID
                        sysexState = SysexState::id1;
                        break;
                    }else{
                        sysexState = SysexState::idle;
                        break;
                    }
                case SysexState::id1:
                    if( c == 0x0b ) {// File Reference Message
                        sysexState = SysexState::id2;
                    }else {
                        sysexState = SysexState::idle;
                    }
                    break;
                case SysexState::id2:
                    if( c == 0x02 ) {// change
                        sysexState = SysexState::change;
                    } else {
                        sysexState = SysexState::idle;
                    }
                    break;
                case SysexState::change:
                    //Change PPQN
                    PPQN = setPPQN(c, PPQN);
                    CLOCK_PERIOD =  (CLOCK_BASE/PPQN);
                    //Change SPW
                    setSPW(c);
                    sysexState = SysexState::end;
                    break;
                case SysexState::end:
                    if( c == 0xF7 ) {// change
                        sysexState = SysexState::idle;
                    }
                    break;
                default:
                    sysexState = SysexState::idle;
                    break;
            }
        }

        /* MIDI Message handling */
        switch(c){
            case 0xf0 : //SysEx
                sysexState = SysexState::start;
                break;
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
