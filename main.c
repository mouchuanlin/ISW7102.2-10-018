/* 
 * File:   main.c
 * Author: Scott
 *
 * Created on November 16, 2018, 12:52 PM
 * 011->013 - 01 Jul 2019. JV. Added CRC to end of hex reception.
 * 013->014 - 05 Jul 2019. JV. Added logic to control MUX during OTA. TEST ONLY.
 * 014->015 - 08 Jul 2019. JV. Cleaned up state machine and code for readability.
 * 015->016 - 10 Jul 2019. JV. Shortened time to process OTA hex code.
 * 016->017 - 14 Jul 2019. JV. Added code to prevent CRC overwrite after reception
 *                             of line w/ bad checksum. Added time-outs. Added 
 *                             MCLR reset after programming/when resetting OTA 
 *                             process. NOTE if OTA process downloads but fails 
 *                             to re-program PIC, check MCLRE CONFIG bit. MUST
 *                             be EXTMCLR for OTA to work.
 * 017->018 - 22 Jul 2019. JV. Added MUX control to migrate back to real HW.
 *                             No BOOT_SEL acking yet when waiting to close connection.
 */

#include "config.h"
#include "io.h"

/*
 * 
 */

void __interrupt isr()
{
    if (PIR1bits.RCIF)
    {
        PIR1bits.RCIF = 0;
        modemChar = RCREG;
        if ((modemChar != BREAK_CHAR && state == Datasave) || state != Datasave)
        {
            okToAddChar = true;
            if (state != Datasave)
                TXREG = modemChar;
            rawData[rdpos++] = modemChar;
        }
        else if (modemChar == BREAK_CHAR && state == Datasave)
            sendAckNack = true;
        
        if (rdpos >= sizeof(rawData) || (state == Datasave && modemChar == ':'))
        {
            rdpos = 0;
            lineCnt++;
        }
        
//        if (state == Datasave && modemChar == '\r')
//            NOP();
        UARTtimerCnt = 0;
    }
    
    if (PIR1bits.TMR2IF)
    {
        PIR1bits.TMR2IF = 0;
        UARTtimerCnt++;
        check_uart_timer();
    }
    
    if (IOCAFbits.IOCAF4)
    {
        IOCAFbits.IOCAF4 = 0;
        if (okToReceiveAck)
            receivedAck = true;
    }
    
    // run second timer for OTA BOOT_SEL time out
    if (INTCONbits.TMR0IF)
    {
        INTCONbits.TMR0IF = 0;
        if (!programmingConfigBits)
        {
            if (t0cnt++ >= _1S_t0)//_10MS)// TIMING NEEDS TO BE TESTED
            {
                timedOut = true;
                t0cnt = 0;
            }
        }
        else
        {
            if (t0cnt++ >= _1S_t0)//_20MS)// TIMING NEEDS TO BE TESTED
            {
                timedOut = true;
                t0cnt = 0;
                programmingConfigBits = false;
            }
        }
    }
}

void main()
{
    init_pic();     // enable WDT
    init_uart();    // enable UART
    
//    init_mem();
//    
//    MEM_SPI_BEGIN();
//    uint8_t rdid = send_spi(0x9F);//rdid
//    rdid = send_spi(0xff);
//    rdid = send_spi(0xff);
//    rdid = send_spi(0xff);
//    rdid = send_spi(0xff);
//    MEM_SPI_END();
//    
//    
//    MEM_SPI_BEGIN();
//    send_spi(0xB9);     // Deep PWDN
//    MEM_SPI_END();
        
//    MUX_CTRL = 1;
        
    while(1)
    {
        if (RCSTAbits.OERR)
        {
            CREN = 0;
            __delay_us(3);
            CREN = 1;
        }
        if (RCSTAbits.FERR)
        {
            uint8_t dummy = RCREG;
        }
        check_state();
//        if (!minTimerOn)
//        {
//            prepare_for_sleep();
//                CLRWDT();
//                SLEEP();
//                NOP();
//        }
        //***TEST
//        if (modemChar == 'R')
//        {
//            okToAddChar = false;
//            // *** TEST
//            MUX_CTRL = 1;
//            write_uart(0x06);
//            MUX_CTRL = 0;
//        }
//        __delay_ms(10);
        CLRWDT();
    }
}


void init_pic()
{
    OSCCON = 0b01111000;        // 16MHz INTOSC select (01110000 for 8MHz)
    OPTION_REG = 0b01000111;    // WPU enabled, 1:256 Timer0 prescaler,
                                // interrupt on rising edge of INT pin
    
    ANSELA = 0x00;
    ANSELC = 0x00;
    TRISA = 0b00001000;
    WPUA = 0b00000001;
//    start_ack_timer();          // MEM_HOLD has WPU enabled
    start_timer();              // UART timeout
    TRISC = 0b00010000;
    
    PORTA = 0b00111000;
    PORTC = 0b00100000;
    
    WDTCONbits.WDTPS = 0b00111;
    WDTCONbits.SWDTEN = 1;
    
    crcResult = 0;
    isFirstLine = false;
}

void check_state()
{
    uint8_t memStatus = 0;
    uint16_t j = 0;
    switch (state)
    {
        case Passthrough:
            if (okToAddChar)
                handle_char();
            break;
        case WaitReady:
            if (okToAddChar)// && !UARTtimedOut)
                handle_char();
            if (UARTtimedOut)
                reset_ota();
            break;
        case Datasave:
            if (okToAddChar)
            {
                stop_timer();//TEST***
                handle_char();
//                start_timer();
            }
            if (sendAckNack && waitingForBreak) // only for bad line; NACK
            {
//                CLRWDT();
                waitingForBreak = false;
                sendAckNack = false;
                MUX_SEL();
                write_uart(N_ACK);
                MUX_DESEL();
                waitingForNewAddr = true;
                isFirstLine = true;
                NOP();
            }
            else if (sendAckNack)               // otherwise, ACK upon break char
            {
                sendAckNack = false;
                MUX_SEL();
                write_uart(ACK);
                MUX_DESEL();
            }
            if (reachedEOF && receivedTermination)
            {
                check_crc();
                reachedEOF = false;
                handle_eof();
                pulse_boot_sel();
                timeWaiting = 0;        // ticks once per WDT cycle, 128ms
                en_boot_sel_int(0);     // interrupt on neg-going edge of BOOT_SEL
//                badTryCnt = 0;
//                start_timer();
            }
            if (UARTtimedOut)
                reset_ota();
            break;
        case WaitToCloseConnection:
        // tell hub when the data connection can be closed
            check_wait_status();
            break;
//             need to add code here to wait until receive boot_sel pulse from hub before programming
        case ProgramHub:
            for (uint16_t i = 32; i < 1024; i++)		// on a 64-byte (1-page) basis, start at 0x800
            {
                badTryCnt = 0;
                if (!program_page_ok(i))
                {
                    badTryCnt ++;
                    if (badTryCnt >= 15)
                    {
                        reset_ota();        // CRITICAL ERROR - WILL RESULT IN SYSTEM FAILURE AS-IS. MAY REVISE
                                            // OR ADD ERROR RESPONSE.
                        i = 1024;
                    }
                    i--;
                }
            }
            
            while (!write_config_ok(configBytes));//&& retryCnt-- > 0)
            while (!run_pic_ok());
            successful = true;
            reset_ota();
            break;
    }
}