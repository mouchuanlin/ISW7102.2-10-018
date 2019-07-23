#include "config.h"
#include "io.h"

uint16_t currentNumBytes = 0;
bool areConfigBits = false, areIDBits = false;
uint8_t recordType = 0;


/* TEST ONLY */
uint8_t buffer[100] = {0};
uint8_t bpos = 0;
uint8_t currentChecksum = 0;

uint8_t extCnt = 0;
uint8_t cnt = 0;

void ascii_to_mem(uint8_t dataBuf[], uint8_t len, bool needAsciiConversion) {
    if (!waitingForBreak)
        lastCRC = crcResult;
    if (needAsciiConversion)
        ascii_to_hex(dataBuf, len); // 0,1,2... array positions filled
    if (rawData[1] >= 0x11)
        NOP();
    if (!reachedEOF && !waitingForNewAddr)
        intel_to_mem(dataBuf, (uint8_t) ((len - 1) / 2)); // => memData[]
    if (checksumOK && !isExtendedAddr && !waitingForBreak && !waitingForNewAddr) {
        checksumOK = false;
        if (reachedEOF && !areConfigBits && !areIDBits) {
            // TEST***
//            terminationCharCnt = 3;
//             must program remaining un-programmed bytes into memory here
                        save_to_mem(dataBuf, currentNumBytes);
//            if (receivedTermination)
//                check_crc();//dataBuf);
        } else if (!reachedEOF && !areConfigBits)
            save_to_mem(dataBuf, currentNumBytes);
        else if (areConfigBits) {
            areConfigBits = false;
            save_config(dataBuf, currentNumBytes);
        }
        if (!areConfigBits && !areIDBits && !isExtendedAddr && currentAddr <= 0x00FFFF)
            referenceAddr = currentAddr;
        lastAddr = currentAddr;
        //        write_uart(ACK);
        savedRecType = recordType;

        prevState = state;
//        state = WaitForABreak;
    } else if (!checksumOK && !waitingForBreak && !waitingForNewAddr)//&& !isExtendedAddr)??
    {
        crcResult = lastCRC;        // restore last-known good crc
//        if (!waitingForBreak)
            savedAddr = lastAddr;
            if ((lastAddr & 0xFF0000) != 0 && !wasExtendedAddr)
                saveExtended = true;
            else if (!wasExtendedAddr)
                saveExtended = false;   //otherwise checksum was bad on the line that has data, not the line with extended addr
                                        // ASSUMING CONFIG OR EXTENDED ADDR IS ON
                                        // ONE LINE.
            else
                savedAddr <<= 16;//addr has not already been extended
        waitingForBreak = true;
//        state = WaitForNBreak;
    }
    else if (checksumOK && isExtendedAddr)
    {
        checksumOK = false;
        lastAddr = currentAddr;
    }
}

void ascii_to_hex(uint8_t* dataBuf, uint8_t len) {
    uint8_t revCnt = 0;
    uint8_t temp = 0;
    for (uint8_t i = 0; i < len; i++)
    {
        if (!reachedEOF && *dataBuf != '#' && *dataBuf != '\r' && *dataBuf != '\n' && *dataBuf != ':' \
                && !waitingForBreak && *dataBuf != BREAK_CHAR)
            add_to_crc(*dataBuf);
        if (*dataBuf >= 0x30 && *dataBuf <= 0x39)
            *dataBuf += 0xD0;                       // ADD -0x30 = ADD 0xD0
        else if (*dataBuf >= 0x41 && *dataBuf <= 0x46)
            *dataBuf += 0xC9;                       // ADD -0x41+0x0A = ADD 0xC9
        if ((i % 2) == 0)
            temp = (uint8_t) ((*dataBuf << 4) & 0xF0);
        else
        {
            temp |= (uint8_t) (*dataBuf & 0x0F);
            dataBuf -= (i/2 + 1);
            *dataBuf = temp;
            dataBuf += (i/2 + 1);
        }
        dataBuf++;
    }
    dataBuf -= (len); // reset pointer to 0th index
    INTCONbits.GIE = 1;
}

void intel_to_mem(uint8_t* dataBuf, uint8_t len) {
    uint8_t numBytes, checksum, temp;
    uint16_t checksumL = 0;
    
    numBytes = *dataBuf;
    currentNumBytes = numBytes; // save the number of bytes for memory
    // 1 = numbyte ; 3 = address + rec type; 2 = checksum offset from end of data
    dataBuf += numBytes + 3 + 1;        // pointer at checksum
    checksumL = *dataBuf;
    dataBuf -= (numBytes + 1);          //pointer at record type
    recordType = *dataBuf;
    dataBuf -= 2;       // pointer at start of addr
    if (isExtendedAddr) {
        isExtendedAddr = false;
        wasExtendedAddr = true;
        currentAddr = (uint32_t) ((lastAddr << 16) | (*dataBuf << 8));
        dataBuf++;
        currentAddr |= (uint32_t)(*dataBuf);
        if (currentAddr >= 0x300000 && currentAddr < 0x30000F)
        {
            areConfigBits = true;
            areIDBits = false;
        }
        else if (currentAddr >= 0x200000 && currentAddr < 0x20000F)
        {
            areIDBits = true;
            areConfigBits = false;
        }
    } else {
        areConfigBits = false;
        areIDBits = false;
        currentAddr = (uint32_t) (*dataBuf << 8);
        dataBuf++;
        currentAddr |= (uint32_t)(*dataBuf);
    }
    dataBuf -= 2;       // pointer at start of data
    if (recordType == 0x04) {
        isExtendedAddr = true;
    }
    if (isExtendedAddr)
    {
        dataBuf += 4;
        currentAddr = (uint32_t) (*dataBuf << 8);
        dataBuf ++;
        currentAddr |= (uint32_t)(*dataBuf);
        dataBuf -= 5;
    }
    
    if (currentAddr == 0 && numBytes == 0)
    {
        dataBuf += 4;
        if (numBytes == 0x00 && recordType == 0x01 && *dataBuf == 0xFF)
            reachedEOF = true;
        dataBuf -= 4;
    }
    if (lineCnt == 2)
        NOP();
    //checksum needs to be verified
    for (uint8_t j = 0; j < numBytes + 3 + 1; j++)
    {
        checksumL += *dataBuf;
        if (j > 3)
        {
            temp = *dataBuf;
            dataBuf -= 4;
            *dataBuf = temp;
            dataBuf += 4;
        }
        dataBuf++;
    }
    checksum = (uint8_t) (checksumL & 0x00FF);
    if (checksum == 0)
        checksumOK = true;
    else
        checksumOK = false;
}

void save_to_mem(uint8_t *dataBuf, uint8_t len) {
    uint8_t addr[3] = {0}, tempAddr[3] = {0};
    uint8_t i = 0, j = 0;
    bool withinLastBlock = false;
    addr[0] = 0; // can only program in blocks of 256 bytes  //(uint8_t)(currentAddr & 0x0000FF);
    addr[1] = (uint8_t) ((currentAddr & 0x00FF00) >> 8);
    addr[2] = 0; //unused in this PIC18 chip   (uint8_t)((currentAddr & 0xFF0000) >> 16);
    currentPage = addr[1];

    if (mdpos == 0xFC)
        NOP();

    if (addr[1] == 0xFF)// && ((currentAddr & 0x00FF00) >> 8) == 0xFC)
        NOP();
    
    if (currentAddr >= 0x000890)
        NOP();
    
    lastAddr = referenceAddr;       //referenceAddr used for non-config, non-ID bits

    currentAddr = currentAddr & 0x00FFFF;
    uint24_t ttt = (uint24_t) ((lastAddr & 0xFF00) + mdpos);
    // if the addr of current data does not line up with the next anticipated address
    if (currentAddr != (uint24_t) ((lastAddr & 0xFF00) + mdpos))    // note mdpos must be incremented at end of each addition to the array,
    {                                                               // except when resetting the position in the array.
        withinLastBlock = (bool)((currentAddr >= lastAddr) && (currentAddr <= ((lastAddr & 0xFF00) | 0xFF)));
        // if addr of current data is still within the same block
        if (withinLastBlock && !reachedEOF)
        {
            // fill buffer from current position to desired position with 0xFF
            for (i = mdpos; i <= (currentAddr % 256); i++)
                memBlock[i] = 0xFF;
            mdpos = (uint8_t) (currentAddr % 256);
            for (j = 0; j < len; j++) {
                memBlock[mdpos++] = *dataBuf;
                dataBuf++;
                if (mdpos >= 256) {
                    /*if (*/page_program(currentPage, mdpos); //)
                    //                    {
                    mdpos = 0;
                    addr[1]++; // addr[1] = number of 256-byte blocks
//                    lastAddr = addr[1] + mdpos;// make sure we're on the same page for next addr.
                    //                    }
                    //                    else
                    //                        ;// error
                }
            }
        }            // if addr of current data is outside this block,
        else if (currentAddr > (uint24_t) (lastAddr & 0xFF00 + 255) || reachedEOF) {
            tempAddr[0] = 0x00;
            tempAddr[1] = (uint8_t) ((lastAddr & 0xFF00) >> 8);
            tempAddr[2] = 0x00;

            // program the last block in first,
            /*if (*/page_program(tempAddr[1], mdpos); //)
            //            {
            if (!reachedEOF)
            {
                // set pointer at starting data address
                mdpos = currentAddr % 256;

                // fill the initial unused portion of the block with 0xFF
                for (i = 0; i < mdpos; i++)
                    memBlock[i] = 0xFF;
                for (j = 0; j < len; j++) {
                    // store all available data in consecutive array spaces
                    if (i < 255) {
                        memBlock[i++] = *dataBuf;
                        dataBuf++;
                        mdpos++;
                    }
                    // if the space runs out, program the buffer into memory
                    // and wrap the data.
                    else {
                        /*if (!*/page_program(addr[1], mdpos); //)
                        ; // error
                        addr[1]++;
                        i = 0;
                        mdpos = 0;
//                        lastAddr = addr[1] + mdpos;// make sure we're on the same page for next addr.
                    }
                }
            }
            /* *** */
            NOP();
            //            }
            //            else
            //                ;   // error
        }
    }        // normal fill of memory buf with data at consecutive memory spaces
    else {
        if (!reachedEOF) {
            for (i = 0; i < len; i++) {
                memBlock[mdpos++] = *dataBuf;
                dataBuf++;
                if (mdpos >= sizeof (memBlock)) {
                    /*if (!*/page_program(currentPage, mdpos); //)
                    //                        ;   // error
                    addr[1]++;
                    mdpos = 0;
                }
            }
            //            write_byte_test();
        } else {
            tempAddr[0] = 0x00;
            tempAddr[1] = (uint8_t) ((lastAddr & 0xFF00) >> 8);
            tempAddr[2] = 0x00;
            /*if (!*/page_program(tempAddr[1], mdpos); //)
            //                ;   // error
            mdpos = 0;
        }
    }
    currentAddr = (addr[1] << 8) + mdpos - 1;// make sure we're on the same page for next addr.
    lastProgrammedAddr = currentAddr;
}

void save_config(uint8_t dataBuf[], uint8_t len) {
    uint8_t addr = (uint8_t) (currentAddr - 0x300000);
    uint8_t i;
    bool okToStartHere = false;
    for (i = 0; i < len; i++) {
        if (addr == i)
            okToStartHere = true;
        if (okToStartHere)
            configBytes[i] = dataBuf[i]; // skip over address & num bytes
    }
}

void check_crc()//uint8_t *dataBuf)
{
//     uint16_t tempCRC = (uint16_t)(*dataBuf << 8);
//     dataBuf ++;
//     tempCRC |= (uint16_t)(*dataBuf); 
    if (crcResult == serverCRCVal)
        crcOK = true;
    else
        crcOK = false;
}

void check_termination() {
    uint8_t temp;
    if (terminationCharCnt >= 3) {
        terminationCharCnt = 0;
        receivedTermination = true;
        serverCRCVal = 0;
        for (uint8_t i = (uint8_t) (rdpos - 7); i < (uint8_t) (rdpos - 3); i++) {
            temp = rawData[i];
            if (temp >= 0x30 && temp <= 0x39)               // numbers 0-9
                temp += 0xD0;
            else if (temp >= 0x41 && temp <= 0x46)          // uppercase A-F
                temp += 0xC9;
            else if (temp >= 0x61 && temp <= 0x66)  // lowercase a-f
                temp += 0xA9;
            serverCRCVal |= (uint8_t) (temp & 0x0F);
            if (i < (rdpos - 4))
                serverCRCVal <<= 4;
        }
    }
}

void fill_array(uint8_t addrL, uint8_t addrH, uint8_t *data) {
    uint8_t addr[2];
    addr[0] = addrL;
    addr[1] = addrH;
    *data = (read(addr, NUM_WORDS));
    NOP();
}

/* *** */
void write_byte_test() {
    init_uart();
    write_uart(mdpos);
    __delay_ms(1);
    for (uint16_t i = 230; i < 256; i++)
        write_uart(memBlock[i]);
}

void reset_ota() {
//    MUX_CTRL = 0;
    if (!successful)
        send_bad_ota();
    else
        successful = false;
    stop_timer();
    prevState = state;
    state = Passthrough;
    sendAckNack = false;
    receivedTermination = false;
    crcOK = false;
    crcResult = 0;
    UARTtimedOut = false;
    waitingForNewAddr = false;
    mdpos = 0;
    //    rdpos = 0;
    init_mem();
    lastProgrammedAddr = 0x400000;
    BOOT_CTRL = 1;
    __delay_ms(100);
    CLRWDT();
    __delay_ms(100);
    CLRWDT();
    BOOT_CTRL = 0;
    __delay_ms(20);
    BOOT_SEL_TRIS = 1;      // release boot_sel
    BOOT_SEL_PU = 1;
}

bool program_page_ok(uint16_t index) {
    uint8_t j = 0;
    uint8_t addrL, addrH, addrU, crcL, crcH;
    bool progOK = false;
    // inner loop until boot_select goes low
    for (uint8_t h = 0; h < 10; h++) {
        if(!start_tx_ok())
        {
            //end inner loop
            //                    en_boot_sel_int(1);// interrupt on pos-going edge of boot_sel
            okToReceiveAck = true;
            start_ack_timer();
            write_uart(0x00);
            write_uart(0x00);
            write_uart(0x00);
            write_uart(ETX); // wait
            t0cnt = 0;
            while (!receivedAck && !timedOut) {
                if (!BOOT_SELI) {
                    __delay_us(50);
                    if (!BOOT_SELI) {
                        while (!timedOut && !receivedAck) {
                            if (BOOT_SELI)
                                receivedAck = true; //okToReceiveAck = true;      // enables the ability to set receiveAck = true
                        }
                    }
                }
            }
            stop_ack_timer();
            if (receivedAck) {
                receivedAck = false;
                progOK = true;
                h = 10;
            }
            //                    dis_boot_sel_int();
            __delay_ms(10);
        }
    }
    timedOut = !progOK;
    if (timedOut)
      return false;

    // end outer loop
    currentAddr = (uint24_t) (index * 64);
    addrL = (uint8_t) (currentAddr & 0x0000FF);
    addrH = (uint8_t) ((currentAddr & 0x00FF00) >> 8);
    addrU = 0;
    
    if (!timedOut) {
        
        // ERASE BLOCK IN PIC
        // outer loop until boot_select goes low
        for (uint8_t h = 0; h < 10; h++) {
            // inner loop until boot_select goes low
            if(start_tx_ok())
            {
                // end inner loop
                crcResult = 0;
                rawData[0] = 0x03; // temporarily use rawData here
                rawData[1] = addrL;
                rawData[2] = addrH;
                rawData[3] = addrU;
                rawData[4] = 0x00;
                rawData[5] = 0x01; // work on 4-page (256-byte) basis
                for (j = 0; j < 6; j++)
                    add_to_crc(rawData[j]);
                crcL = (uint8_t) (crcResult & 0x00FF);
                crcH = (uint8_t) ((crcResult & 0xFF00) >> 8);
                rawData[6] = crcL;
                rawData[7] = crcH;
                rawData[8] = ETX;
                for (j = 0; j < 8; j++) {
                    if (rawData[j] == STX || rawData[j] == ETX || rawData[j] == DLE)
                        write_uart(DLE);
                    write_uart(rawData[j]);
                }
                write_uart(ETX);
                // wait
                start_ack_timer();

                while (!receivedAck && !timedOut) {
                    if (!BOOT_SELI) {
                        __delay_us(50);
                        if (!BOOT_SELI) {
                            while (!timedOut && !receivedAck) {
                                if (BOOT_SELI)
                                    receivedAck = true; //okToReceiveAck = true;      // enables the ability to set receiveAck = true
                            }
                        }
                    }
                }
                stop_ack_timer();
                if (receivedAck) {
                    receivedAck = false;
                    progOK = true;
                    h = 10;
                }
                //                    dis_boot_sel_int();
                __delay_ms(10);
            }
            timedOut = !progOK;
        }
    }
    if (timedOut)
        return false;

    // end outer loop
    // READ PAGE FROM MEMORY
    if (addrL == 0x00) {
        disable_write();
        MEM_SPI_BEGIN();
        send_spi(0x03);
        send_spi(addrL);
        send_spi(addrH);
        send_spi(addrU);
        for (uint16_t g = 0; g < sizeof (memBlock); g++)
            memBlock[g] = send_spi(0xFF);
        MEM_SPI_END();
    }

    if (addrH == 0x08 && addrL >= 0x40)
        NOP();
    if (!timedOut) {
        // PROGRAM PIC
        crcResult = 0;
        add_to_crc(0x04);
        add_to_crc(addrL);
        add_to_crc(addrH);
        add_to_crc(addrU);
        add_to_crc(0x00);
        add_to_crc(0x01); // program 4 pages at once
        for (j = 0; j < 64; j++) // calculate CRC outside of UART write to ensure consistent baud
            add_to_crc(memBlock[(uint16_t) (addrL + j)]); // addrL is always in 64-byte increments
        crcL = (uint8_t) (crcResult & 0x00FF);
        crcH = (uint8_t) ((crcResult & 0xFF00) >> 8);

        // outer loop until BOOT_SEL goes low or times out
        for (uint8_t h = 0; h < 10; h++) {
            // inner loop until BOOT_SEL goes low or times out
            while (!start_tx_ok());
            // end inner loop
            write_uart(0x05);
            write_uart(0x04);
            if (addrL == STX || addrL == ETX || addrL == DLE)
                write_uart(0x05);
            write_uart(addrL);
            if (addrH == STX || addrH == ETX || addrH == DLE)
                write_uart(0x05);
            write_uart(addrH);
            if (addrU == STX || addrU == ETX || addrU == DLE)
                write_uart(0x05);
            write_uart(addrU);
            write_uart(0x00);
            write_uart(0x01);
            for (j = 0; j < 64; j++) {
                if (memBlock[addrL + j] == STX || memBlock[addrL + j] == ETX || memBlock[addrL + j] == DLE)
                    write_uart(0x05);
                write_uart(memBlock[(uint16_t) (addrL + j)]);

            }
            if (crcL == STX || crcL == ETX || crcL == DLE)
                write_uart(0x05);
            write_uart(crcL);
            if (crcH == STX || crcH == ETX || crcH == DLE)
                write_uart(0x05);
            write_uart(crcH);
            write_uart(ETX);
            // wait
            start_ack_timer();
            while (!receivedAck && !timedOut) {
                if (!BOOT_SELI) {
                    __delay_us(50);
                    if (!BOOT_SELI) {
                        while (!timedOut && !receivedAck) {
                            if (BOOT_SELI)
                                receivedAck = true; //okToReceiveAck = true;      // enables the ability to set receiveAck = true
                        }
                    }
                }
            }
            stop_ack_timer();
            if (receivedAck) {
                receivedAck = false;
                progOK = true;
                h = 10;
            }
            //                    dis_boot_sel_int();
            __delay_ms(10);
        }
        timedOut = !progOK;

    } // end outer loop
    return (!timedOut);
}

//void tell_hub_close_connection() {
//    start_ack_timer();
//    t0cnt = 0;
//    CLRWDT();
//    while (!receivedAck && !timedOut) {
//        if (!BOOT_SELI) {
//            __delay_us(100);
//            if (!BOOT_SELI) {
//                while (!timedOut && !receivedAck) {
//                    if (BOOT_SELI)
//                        receivedAck = true; //okToReceiveAck = true;      // enables the ability to set receiveAck = true
//                }
//            }
//        }
//    }
//}

void parse_new_data() {
    start_timer();
    if (modemChar == '#')
        terminationCharCnt++;
    else
        terminationCharCnt = 0;
    
    // if received a full line but not reached EOF and not waiting for re-write
    // of line with bad checksum:
    if ((lastModemChar == '\r' || lastModemChar == '\n') && \
            (modemChar == '\r' || modemChar == '\n') && !reachedEOF && \
            !waitingForNewAddr && !waitingForBreak)
    {
        PIE1bits.RCIE = 0;
        ascii_to_mem(rawData, rdpos, true);
        rdpos = 0;
        PIE1bits.RCIE = 1;
    }
    // if received a full line but not reached EOF and waiting for re-write of
    // line with bad checksum
    else if ((lastModemChar == '\r' || lastModemChar == '\n') && \
            (modemChar == '\r' || modemChar == '\n') && !reachedEOF && \
            waitingForNewAddr && !waitingForBreak)
    {
        PIE1bits.RCIE = 0;
        ascii_to_hex(rawData, rdpos);
        verify_checksum(rawData, (rdpos>>1)-1);
        if (!isExtendedSavedAddr)
            currentAddr = 0;

        currentAddr |= (uint24_t)((rawData[1] << 8) | rawData[2]);
        if (rawData[3] == 0x04)
        {
            isExtendedSavedAddr = true;
            currentAddr = (uint24_t)(((rawData[4] << 8) | rawData[5]));
            currentAddr <<= 16;
        }
        
        // if reached last-programmed line with good checksum
        if (currentAddr >= savedAddr)
        {
                if (rawData[3] == savedRecType && checksumOK)
                {
                    waitingForNewAddr = false;  // should take care of case where UART error occurs at
                                                // line after extended addr vs line with extended addr
                    if (isFirstLine && lastProgrammedAddr != currentAddr)
                    {
                        ascii_to_mem(rawData, rdpos, false);    // special condition, see below
                    }
                }
//                ascii_to_mem(rawData, rdpos, false);
//                if (isFirstLine && checksumOK)
//                    ascii_to_mem(rawData, rdpos, false);        // special condition: if error occurred
                                                                // on first line in the block, save this
                                                                // to memory.
                else if (!checksumOK)
                {
                    waitingForNewAddr = false;
                    waitingForBreak = true;
                }
            // conditions not provisioned: multi-line config bit settings;
            // condition where the 50-line mark splits extended addr from 
            // contents of the registers at that location; there must be one
            // buffer line between start of 50-line point and start of extended
            // addr line.
        }
        else if (currentAddr <= savedAddr && UARTtimedOut)
            reset_ota();
        PIE1bits.RCIE = 1;
        isFirstLine = false;
    }
    if (reachedEOF)
        check_termination();
    if (modemChar != BREAK_CHAR)
        lastModemChar = modemChar;
}


void handle_eof()
{
    uint8_t memStatus;
 // crcOK = true;//false;***
        prevState = state;
        state = WaitToCloseConnection;
        badTryCnt = 0;
//        start_timer();
//        tell_hub_close_connection();

        nMD_PWR = 1; // turn off this chip's control over modem
//    memStatus = 0;
//    while (memStatus != 0x02) {
//        enable_write();
//        memStatus = read_status() & 0x03;
//    }
}



void handle_char()
{
    switch (state)
    {
        case Passthrough:
            okToAddChar = false;
            if (ota_receive_ready(modemChar))
            {
                prevState = state;
                state = WaitReady;
                nMD_PWR = 0;
                start_timer();      // how long to wait?
            }
            minTimerOn = true;
            start_timer();
            break;
        case WaitReady:
            okToAddChar = false;
            if (header_received(modemChar))
            {
                prevState = state;
                state = Datasave;
                rdpos = 0;
                lastProgrammedAddr = 0x400000;      // take care of potential for UART
                                                    // error on first line of hex file
                                                    // future improvements could be to
                                                    // replace savedAddr with this var.
//                MUX_CTRL = 1;
                write_uart(0x0f);
                start_timer();
            }
            start_timer();      // how long to wait?
            break;
        case Datasave:
            okToAddChar = false;
            parse_new_data();
            break;
    }
}



void verify_checksum(uint8_t *data, uint8_t len)
{
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < len; i++)
    {
        checksum += *data;
        data++;
    }
    checksumOK = (bool)(checksum == 0);
}