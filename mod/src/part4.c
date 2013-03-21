



#include <linux/kernel.h>

#include "common.h"
#include "part4.h"
#include "pn512app.h"
#include "pn512.h"
#include "picc.h"
#include "delay.h"
#include "typeA.h"
#include "typeB.h"
#include "debug.h"


const UINT8 FWI_Table[]=
{
// (330us) FWI=0
    0x00,0x11,
    0x00,0x80,
// (661us) FWI=1
    0x00,0x11,
    0x01,0x00,
// (1322us) FWI=2
    0x00,0x11,
    0x02,0x00,
// (2643us) FWI=3
    0x00,0x11,
    0x04,0x00,
// (5286us) FWI=4
    0x00,0x11,
    0x08,0x00, 
// (10.572ms) FWI=5
    0x00,0x44,
    0x04,0x16, 
// (21.14ms) FWI=6
    0x00,0x44,
    0x08,0x2C, 
// (42.29ms) FWI=7
    0x00,0x44,
    0x10,0x59, 
// (84.58ms) FWI=8
    0x00,0x44,
    0x20,0xB3, 
// (169.2ms) FWI=9
    0x02,0xA5,
    0x06,0x9C, 
// (338.3ms) FWI=10
    0x02,0xA5,
    0x0C,0x17, 
// (676.6ms;) FWI=11
    0x02,0xA5,
    0x1A,0x6E, 
// (1353.3ms) FWI=12
    0x02,0xA5,
    0x34,0xDD,
// (2706.5ms) FWI=13
    0x02,0xA5,
    0x69,0xB9,
// (5413.0ms) FWI=14
    0x02,0xA5,
    0xD3,0x72,
};




UINT8 tempIBlockBuf[253];


#ifdef USECID

typedef struct
{
    UINT8 NO;
    UINT8 *UID;
    BOOL  used;
}CIDInfo;

CIDInfo CIDNO[15];

void CIDInit(void)
{
    UINT8 i;

    for(i = 0; i < 15; i++)
    {
        CIDNO[i].NO   = i;
        CIDNO[i].UID  = NULL;
        CIDNO[i].used = FALSE;
    }
}

void ReleaseCID(UINT8 cid)
{
    CIDNO[cid].UID  = NULL:
    CIDNO[cid].used = FALSE;
}

#endif

UINT8 GetCID(UINT8 *uid)
{
#ifdef USECID

    UINT i = 0;


    do
    {
        if(CIDNO[i].used == TRUE)
        {
            i++;
        }
        else
        {
            break;
        }
    }while(i < 15);

    CIDNO[i].used = TRUE;
    CIDNO[i].UID  = uid;

    return(CIDNO[i].NO);
#else
    return(0);
#endif
}



/****************************************************************/
//       Type A RATS
/****************************************************************/
UINT8 PcdRequestATS(void)
{
    UINT8 nBytesReceived;
    UINT8 ret = ERROR_NO;
    UINT8 i;
    UINT8 retry = 0;


    while(retry < 3)
    {
        SetRegBit(REG_TXMODE, BIT_TXCRCEN);        //TXCRC enable
        SetRegBit(REG_RXMODE, BIT_RXCRCEN);        //RXCRC enable
        ClearRegBit(REG_STATUS2, BIT_MFCRYPTO1ON); // disable crypto 1 unit                             add.....
        
        //************* Cmd Sequence **********************************//
        FIFOFlush();
        RegWrite(REG_FIFODATA, 0xE0);                                       // Start byte
        RegWrite(REG_FIFODATA, pcd.FSDI << 4 | picc.CID);         // parameter byte
        
        SetTimer100us(60);            // 6 ms Time out
        ret = PcdHandlerCmd(CMD_TRANSCEIVE, BIT_PARITYERR | BIT_CRCERR); 
        
        //******** Error handling ***************//
        nBytesReceived = RegRead(REG_FIFOLEVEL);
        picc.ATS[0] = RegRead(REG_FIFODATA);    //TL: lenth byte, specifies the length byte of the ATS

        retry++;
        if((ret != ERROR_NO) && (retry >= 2))
        {
            break;
        }
        
        if(ret == ERROR_NO)
        {
            // store ATS
             if(nBytesReceived != picc.ATS[0])
            {
                return ERROR_ATSLEN;
            }
           
            picc.states = PICC_ACTIVATED;
            
            CLEAR_BIT(picc.fgTCL, BIT_PCDBLOCKNUMBER);    // reset block number
            SET_BIT(picc.fgTCL, BIT_CIDPRESENT);          // CID supported by PICC by default
            
            if(nBytesReceived < 2) 
            {
                return ret;
            }
            else
            {
                for(i=1; i < nBytesReceived; i++)
                {
                    picc.ATS[i] = RegRead(REG_FIFODATA);
                }
                
                picc.FSCI = picc.ATS[1] & 0x0F;    // T0: format byte, codes Y(1) and FSCI
                if(picc.FSCI > 8)
                {
                    picc.FSCI = 8;
                }
                
                i = 1;
                if(picc.ATS[1] & 0x10)
                {
                    // TA(1) present
                    i++;
                    picc.speed = picc.ATS[i];
                    if(picc.speed & 0x08)
                    {
                        // b4 = 1 should be interpreted by the PCD as (b8 to b1) = (00000000)b (only ~106 kbit/s in both directions).
                        picc.speed = 0x00;    // complaint to ISO14443-4: 2008
                    }
                }
                if(picc.ATS[1] & 0x20)
                {
                    // TB(1) present
                    i++;
                    picc.FWI = (picc.ATS[i] & 0xF0) >> 4;   // Set FWT
                    if(picc.FWI > 14) 
                    {
                        // Until the RFU value 15 is assigned by ISO/IEC, a PCD receiving FWI = 15 should interpret it as FWI = 4.
                        picc.FWI = 4;    // complaint to ISO14443-4: 2008
                    }
                    picc.SFGI = picc.ATS[i] & 0x0F;
                    if(picc.SFGI > 14) 
                    {
                        // Until the RFU value 15 is assigned by ISO/IEC, a PCD receiving SFGI = 15 should interpret it as SFGI = 0.
                        picc.SFGI = 0;    // complaint to ISO14443-4: 2008
                    }
                }
                if(picc.ATS[1] & 0x40)
                {   
                    // TC(1) present
                    i++;
                    if((picc.ATS[i] & 0x02) == 0x00)
                    {
                        // CID do not supported by the PICC
                        CLEAR_BIT(picc.fgTCL, BIT_CIDPRESENT);
                    }
                }
                break;
            }
        }
    }
    return ret;
}


static void PiccSfgDelay(UINT8 TimeDelay)
{

    PrtMsg(DBGL2, "%s: start, TimeDelay = %02X\n", __FUNCTION__, TimeDelay);

    switch(TimeDelay)
    {
        case 1:            //661us
            Delay1us(200);
            break;
        case 2:            //1322us
            Delay1us(600);
            break;
        case 3:            //2643us
            Delay1us(1900);
            break;
        case 4:            //5286us
            Delay1us(4500);
            break;
        case 5:            //10.572ms
            Delay1us(9800);
            break;
        case 6:            //21.14ms
            Delay1ms(20);
            Delay1us(400);
            break;
        case 7:            //42.29ms
            Delay1ms(40);
            Delay1us(1500);
            break;
        case 8:            //84.58ms
            Delay1ms(80);
            Delay1us(3800);
            break;
        case 9:            //169.2ms
            Delay1ms(160);
            Delay1us(8500);
            break;
        case 10:           // 338.3ms
            Delay1ms(330);
            Delay1us(7600);
            break;
        case 11:           //676.6ms
            Delay1ms(670);
            Delay1us(5900);
            break;
        case 12:           //1353.3ms
            Delay1ms(1350);
            Delay1us(2600);
            break;
        case 13:           //2706.5ms
            Delay1ms(2500);
            Delay1ms(200);
            Delay1us(5800);
            break;
        case 14:           //5413.0ms
            Delay1s(5);
            Delay1ms(410);
            Delay1us(2300);
            break;
        default:          //330us
            break;

    }
}


/*********************************************/
//    Set the PN512 timer clock for FWT use
/********************************************/
void PcdSetTimeout(UINT8 timeout)
{
    UINT16 preScaler;
    UINT16 reloadValue;
    UINT8  tempWTXM = picc.WTXM;


//    PrtMsg(DBGL2, "%s: start, timeOut = %02X\n", __FUNCTION__, timeout);    

    preScaler   = MAKEWORD(FWI_Table[timeout * 4],     FWI_Table[timeout * 4 + 1]);
    reloadValue = MAKEWORD(FWI_Table[timeout * 4 + 2], FWI_Table[timeout * 4 + 3]);

    if(picc.type == PICC_TYPEB_TCL)
    {
        reloadValue += 8;
        if(BITISSET(picc.fgTCL, BIT_TYPEBATTRIB))
        {
            reloadValue += 8;
        }
        if(!timeout)
        {
            reloadValue += 20;
        }
    }
    if(BITISSET(picc.fgTCL, BIT_WTXREQUEST))
    {
        if(tempWTXM > 59)
        {
            tempWTXM = 59;
        }
        if(timeout < 9)
        {
            preScaler *= tempWTXM;
            if(preScaler > 0x0FFF)
            {
                preScaler = 0x0FFF;
            }
        }
        else
        {
            reloadValue *= tempWTXM;
        }
        CLEAR_BIT(picc.fgTCL, BIT_WTXREQUEST);
    }
    RegWrite(REG_TMODE, (UINT8)(preScaler>>8) | 0x80);
    RegWrite(REG_TPRESCALER, (UINT8)preScaler);
    RegWrite(REG_TRELOADVAL_HI, (UINT8)(reloadValue>>8));
    RegWrite(REG_TRELOADVAL_LO, (UINT8)reloadValue);
    RegWrite(REG_COMMIRQ,0x01);          	// Clear the TimerIrq bit
}


UINT8 PiccSpeedCheck(void)
{
    UINT8 priByte;
    UINT8 curByte;


    pcd.curSpeed = 0x80;
    priByte = 0x00;
    
    if(picc.speed & 0x01)
    {
        // DR = 2 supported, PCD to PICC, 1 etu = 64 / fc, bit rate supported is fc / 64 (~ 212 kbit/s)
        priByte = 0x01;
    }
    if(picc.speed & 0x02)
    {
        // DR = 4 supported, PCD to PICC, 1 etu = 32 / fc, bit rate supported is fc / 32 (~ 424 kbit/s)
        priByte = 0x02;
    }
    if(picc.speed & 0x04)
    {
        // DR = 8 supported, PCD to PICC, 1 etu = 16 / fc, bit rate supported is fc / 16 (~ 848 kbit/s)
        priByte = 0x03;
    }
    
    curByte = 0x00;
    
    if(picc.speed & 0x10)
    {
        // DS = 2 supported,  PICC to PCD, 1 etu = 64 / fc, bit rate supported is fc / 64 (~ 212 kbit/s)
        curByte = 0x01;
    }
    if(picc.speed & 0x20)
    {
        // DS = 4 supported, PICC to PCD, 1 etu = 32 / fc, bit rate supported is fc / 32 (~ 424 kbit/s)
        curByte = 0x02;
    }
    if(picc.speed & 0x40)
    {
        // DS = 8 supported, PICC to PCD, 1 etu = 16 / fc, bit rate supported is fc / 16 (~ 848 kbit/s)
        curByte = 0x03;
    }


    if(picc.speed & 0x80)    
    {
        // Only the same D for both directions supported , if bit is set to 1
        if(curByte > priByte)
        {
            curByte = priByte;
        }
        else
        {
            priByte = curByte;
        }
    }
    
    if(picc.speed & 0x08)
    {
        // if b4 = 1, b8 to b1 should be interpreted  as (00000000)b, accroding to ISO14443-4: 2008(typeA) and ISO14443-3: 2011(typeB)
        curByte = priByte = 0x00;
    }
    
    curByte = (curByte << 2) | priByte;
    
    return(curByte);
}

void PiccHighSpeedConfig(UINT8 speedParam, UINT8 typeB)
{
    pcd.curSpeed = picc.speed & 0x80;
    if((speedParam & 0x03) == 0x03)
    {
        // DR = 8 supported, PCD to PICC, 1 etu = 16 / fc, bit rate supported is fc / 16 (~ 848 kbit/s)
        pcd.curSpeed |= 0x03;
        PcdConfigIso14443Type(CONFIGNOTHING, TYPEA_848TX | typeB);
    }
    else if((speedParam & 0x03) == 0x02)
    {
        // DR = 4 supported, PCD to PICC, 1 etu = 32 / fc, bit rate supported is fc / 32 (~ 424 kbit/s)
        pcd.curSpeed |= 0x02;
        PcdConfigIso14443Type(CONFIGNOTHING, TYPEA_424TX | typeB);
    }
    else if((speedParam & 0x03) == 0x01)
    {
        // DR = 2 supported, PCD to PICC, 1 etu = 64 / fc, bit rate supported is fc / 64 (~ 212 kbit/s)
        pcd.curSpeed |= 0x01;
        PcdConfigIso14443Type(CONFIGNOTHING, TYPEA_212TX | typeB);
    }
    if((speedParam & 0x0c) == 0x0c)
    {
        // DS = 8 supported,  PICC to PCD, 1 etu = 16 / fc, bit rate supported is fc / 16 (~ 848 kbit/s) 
        pcd.curSpeed |= 0x18;
        PcdConfigIso14443Type(CONFIGNOTHING, TYPEA_848RX | typeB);
    }
    else if((speedParam & 0x0c) == 0x08)
    {
        // DS = 4 supported, PICC to PCD, 1 etu = 32 / fc, bit rate supported is fc / 32 (~ 424 kbit/s)
        pcd.curSpeed |= 0x10;
        PcdConfigIso14443Type(CONFIGNOTHING, TYPEA_424RX | typeB);

    }
    else if((speedParam & 0x0c) == 0x04)
    {
        // DS = 2 supported,  PICC to PCD, 1 etu = 64 / fc, bit rate supported is fc / 64 (~ 212 kbit/s)
        pcd.curSpeed |= 0x08;
        PcdConfigIso14443Type(CONFIGNOTHING, TYPEA_212RX | typeB);
    }
}


void TCLPrologueFieldLoad(void)
{
    if((pcd.PCB & 0xC0) != 0xC0)
    {
        // I-block or R-block
        if(BITISSET(picc.fgTCL, BIT_PCDBLOCKNUMBER))
        {
            pcd.PCB |= 0x01;
        }
        else
        {
            pcd.PCB &= 0xFE;
        }
    }
    if(BITISSET(picc.fgTCL, BIT_CIDPRESENT))
    {
        pcd.PCB |= 0x08;
    }
    else
    {
        pcd.PCB &= 0xF7;
    }
    
    FIFOFlush();            //Empty FIFO
    RegWrite(REG_FIFODATA, pcd.PCB);
    if(BITISSET(picc.fgTCL, BIT_CIDPRESENT))
    {
        RegWrite(REG_FIFODATA, picc.CID);
    }
}



/****************************************************************/
//       Type A PPS
/****************************************************************/
void PiccPPSCheckAndSend(void)
{
    UINT8 speedParam;
    UINT8 nBytesReceived;
    UINT8 ret = ERROR_NO;
    UINT8 i;
    UINT8 RecBuf[5];
    

    speedParam = PiccSpeedCheck();
    if(speedParam)
    {

        //************* Cmd Sequence **********************************//
        FIFOFlush();
        RegWrite(REG_FIFODATA, 0xD0 | picc.CID);      // PPSS
        RegWrite(REG_FIFODATA, 0x11);                 // PPS0
        RegWrite(REG_FIFODATA, speedParam);           // PPS1
        PiccSfgDelay(picc.SFGI);
        Delay1ms(5);
        PcdSetTimeout(picc.FWI);
        ret = PcdHandlerCmd(CMD_TRANSCEIVE, BIT_PARITYERR | BIT_CRCERR);
    

        //******** Error handling ***************//
        if(ret == ERROR_NO)
        {
            nBytesReceived = RegRead(REG_FIFOLEVEL);
            for(i=0; i < nBytesReceived; i++)
            {
                RecBuf[i] = RegRead(REG_FIFODATA);
            }
            if((RecBuf[0] & 0xF0) == 0xD0)
            {
                // return PPSS,  PPS successful
                PiccHighSpeedConfig(speedParam, TYPEA_106TX);
            }
        }
    }
}


UINT8 TCL_Select(UINT8 blockPCB)
{
    UINT8 tempFWI;
    UINT8 ret;


    PrtMsg(DBGL3, "%s: start\n", __FUNCTION__);    

    pcd.PCB = blockPCB;
    TCLPrologueFieldLoad();
    tempFWI = picc.FWI;
    tempFWI = (tempFWI > 0x06) ? 0x06 : tempFWI;
    PcdSetTimeout(tempFWI);
    ret = PcdHandlerCmd(CMD_TRANSCEIVE, BIT_CRCERR);

	PrtMsg(DBGL3, "%s: exit\n", __FUNCTION__); 

    return(ret);
}


UINT8 DeselectRequest(void)
{
    UINT8 ret = ERROR_NO;


    PrtMsg(DBGL4, "%s: start\n", __FUNCTION__);
    
    if(picc.states == PICC_ACTIVATED)
    {
        pcd.PCB = 0xC2;    // S-block, DESELECT
        TCLPrologueFieldLoad();
        SetTimer100us(53);
        ret = PcdHandlerCmd(CMD_TRANSCEIVE, BIT_CRCERR);
        if(picc.states != PICC_POWEROFF)
        {
            picc.states = PICC_IDLE;
        }
        if(picc.type == PICC_TYPEA_TCL)
        {
            PcdConfigIso14443Type(CONFIGNOTHING, TYPEA_106TX);
            PcdConfigIso14443Type(CONFIGNOTHING, TYPEA_106RX);
        }
        else if(picc.type == PICC_TYPEB_TCL)
        {
            PcdConfigIso14443Type(CONFIGNOTHING,TYPEB_106TX);
            PcdConfigIso14443Type(CONFIGNOTHING,TYPEB_106RX);
        }
    }

    PrtMsg(DBGL4, "%s: exit\n", __FUNCTION__);

    return(ret);
}



/*****************************************************************/
// Function Name: PcdTCLCmd
// Input parameters: senBuf, senLen
// Output Parameters: recBuf, recLen
// Function describes: 
// Return Value:
/*****************************************************************/
static UINT8 PcdTCLCmd(UINT8 *senBuf, UINT8 senLen, UINT8 *recBuf, UINT8 *recLen)
{
    UINT8 ret = ERROR_NO;
    UINT8 i = 0;
    UINT8 tempLen;


    PrtMsg(DBGL2, "%s: start, senLen = %02X\n", __FUNCTION__, senLen);

    while(senLen && (i < MAX_FIFO_LENGTH))
    {
        RegWrite(REG_FIFODATA, senBuf[i]);
        tempIBlockBuf[i] = senBuf[i];
        senLen--;
        i++;
    }
    PcdSetTimeout(picc.FWI);
    RegWrite(REG_COMMIRQ, 0x21);                 //Clear the Rx Irq bit first
    RegWrite(REG_COMMAND, CMD_TRANSCEIVE);
    RegWrite(REG_BITFRAMING, 0x80);              //Start transmission

    while(senLen)
    {
        if(RegRead(REG_FIFOLEVEL) < 0x30)
        {
            RegWrite(REG_FIFODATA, senBuf[i]);
            tempIBlockBuf[i] = senBuf[i];
            i++;
            senLen--;
        }
    }
    PrtMsg(DBGL2, "%s: 00000000000000000000000000000\n", __FUNCTION__);
    while((RegRead(REG_STATUS2) & 0x07) < 0x05)     // waiting for transmitter goto receiveing state
    {
        if(RegRead(REG_COMMIRQ) & 0x21)
        {
            break;
		}
    }

    tempLen = 0;
    i = 0;
    PrtMsg(DBGL2, "%s: 11111111111111111111111111111\n", __FUNCTION__);
    while(!(RegRead(REG_COMMIRQ) & 0x21))
    {
        // when the receiver doesn't detect the end of a valid datastream and the timer doesn't time out
        tempLen = RegRead(REG_FIFOLEVEL);

        if(tempLen > 10)
        {
            while(tempLen)
            {
                recBuf[i] = RegRead(REG_FIFODATA);
                i++;
                tempLen--;
                if(i > FSDLENTH)
                {
                    SetRegBit(REG_CONTROL, BIT_TSTOPNOW);  	// Stop Timer Now
                    return(ERROR_FSDLENTH);
                }
            }
        }
    }

    tempLen = RegRead(REG_FIFOLEVEL);
    while(tempLen--)
    {
        recBuf[i] = RegRead(REG_FIFODATA);
        i++;
        if(i > FSDLENTH)
        {
            return(ERROR_FSDLENTH);
        }
    }
    
    *recLen = i;
    SetRegBit(REG_CONTROL, BIT_TSTOPNOW);  	// Stop Timer Now 
    RegWrite(REG_COMMAND, CMD_IDLE);
   	if(RegRead(REG_COMMIRQ) & 0x01) 
   	{
        RegWrite(REG_COMMIRQ, 0x01);         // Clear the Time out Irq bit first
        ret = ERROR_NOTAG;                   // Time Out Error 
   	}
   	else
   	{
        i = RegRead(REG_ERROR) & 0x17;
        if(picc.type == PICC_TYPEB_TCL)
        {
            i &= 0x1C;        // buffer overflow, bit-collision, RxCRC En
        }
        if(i)
        {
            ret = ERROR_PROTOCOL; 
        }
    }
    
    return(ret);
}


static UINT8 TclRBlockCmd(UINT8 blockPCB, UINT8 *recBuf, UINT8 *recLen)
{
    UINT8 ret;


    pcd.PCB = blockPCB;
    TCLPrologueFieldLoad();
    ret = PcdTCLCmd(0, 0, recBuf, recLen);

    return(ret);
}




static UINT8 TCLDataSendErrorCheck(UINT8 *senBuf, UINT8 senLen, UINT8 *recBuf, UINT8 *recLen)
{
    UINT8 timeoutRetry = 0;
    UINT8 frameRetry = 0;
    UINT8 ackRetry = 0;
    UINT8 i;
    UINT8 ret;
    UINT8 resend;
    UINT8 resendCount;


    PrtMsg(DBGL2, "%s: start, senLen = %02X\n", __FUNCTION__, senLen);

    resendCount = 0;
    do
    {

        resend = 0;
        pcd.PCB = picc.PCB;
        PiccSfgDelay(picc.SFGI);
        TCLPrologueFieldLoad();
        ret = PcdTCLCmd(senBuf, senLen, recBuf, recLen);
        PrtMsg(DBGL2, "%s: ret = %02X\n", __FUNCTION__, ret);
        while(1)
        {
            //Error handling
            if(ret == ERROR_NOTAG)            //Time Out 
            {
                CLEAR_BIT(picc.fgTCL, BIT_WTXREQBEFORE);
                if(BITISSET(picc.fgTCL, BIT_PICCCHAINING))
                {
                    if(ackRetry == 2)
                    {
                        ret = SLOTERROR_ICC_MUTE;
                        break;
                    }

                    ret = TclRBlockCmd(0xA2, recBuf, recLen);    // R-Block: ACK
                    ackRetry++;
                }
                else
                {
                    if(timeoutRetry == 2)
                    {
                        ret = SLOTERROR_ICC_MUTE;
                        break;
                    }
                    ret = TclRBlockCmd(0xB2, recBuf, recLen);   // R-Block: NAK
                    timeoutRetry++;
                }
            }
            else if(ret == ERROR_NO)
            {
                CLEAR_BIT(picc.fgTCL, BIT_WTXREQBEFORE);
                ret = SLOT_NO_ERROR;
                if((recBuf[0] & 0xC0) == 0xC0)
                {
                    //S-Block received
                    if((recBuf[0] & 0x30) == 0x30)
                    {
                        //S(WTX) received
                        if(recBuf[0] & 0x08)
                        {
                            // CID following
                            if(BITISCLEAR(picc.fgTCL, BIT_CIDPRESENT))
                            {
                                return(SLOTERROR_TCL_BLOCK_INVALID);
                            }
                        }
                        if(recBuf[0] & 0x04)
                        {
                            return(SLOTERROR_TCL_BLOCK_INVALID);
                        }
                        
                        if(BITISSET(picc.fgTCL, BIT_CIDPRESENT))
                        {
                            picc.WTXM = recBuf[2];

                        }
                        else
                        {
                            picc.WTXM = recBuf[1];
                        }
                        if((picc.WTXM == 0) || (picc.WTXM > 59)) 
                        {
                            // WTXM must be code in the range from 1 to 59, or it will be treat as a protocol error. ISO/IEC 14443-4:2008
                            return(SLOTERROR_TCL_BLOCK_INVALID);
                        }
                        pcd.PCB = 0xF2;
                        PiccSfgDelay(picc.SFGI);
                        TCLPrologueFieldLoad();
                        RegWrite(REG_FIFODATA, picc.WTXM);
                        ret = PcdTCLCmd(0, 0, recBuf, recLen);
                        
                        SET_BIT(picc.fgTCL, BIT_WTXREQUEST);    // the time FWTtemp starts after the PCD has sent the S(WTX) response
                        SET_BIT(picc.fgTCL, BIT_WTXREQBEFORE);
                    }
                    else
                    {
                        return(SLOTERROR_TCL_BLOCK_INVALID);
                    }
                }
                else if((recBuf[0] & 0xC0) == 0x80)
                {
                    //R-Block received
                    if(recBuf[0] & 0x14)
                    {
                        return(SLOTERROR_TCL_BLOCK_INVALID);  //PICC Never send a R(NAK) and Never used NAD
                    }
                    if((recBuf[0] & 0x20) == 0x00)
                    {
                        return(SLOTERROR_TCL_BLOCK_INVALID);  // R(ACK) bit 6 must set to 1
                    }
                    if(recBuf[0] & 0x08)
                    {
                        if(BITISCLEAR(picc.fgTCL, BIT_CIDPRESENT))
                        {
                            return(SLOTERROR_TCL_BLOCK_INVALID);
                        }
                    }

                    if((picc.PCB ^ recBuf[0]) & 0x01)
                    {
                        // the block number of R-block do not equal the block number of the last I-block
                        timeoutRetry = 0;
                        
                        if(resendCount == 2)
                        {
                            return(SLOTERROR_TCL_3RETRANSMIT_FAIL);
                        }
                        picc.PCB &= 0x1F;
                        for(i = 0; i < senLen; i++)
                        {
                            senBuf[i] = tempIBlockBuf[i]; 
                        }
                        
                        //re-transmit last I-block
                        resend = 01;
                        resendCount++;
                    }
                    else
                    {
                        if(BITISSET(picc.fgTCL, BIT_PCDCHAINING))
                        {
                            // PCD Chaining continue 
                            TOGGLE_BIT(picc.fgTCL, BIT_PCDBLOCKNUMBER);
                        }
                        else
                        {
                            return(SLOTERROR_TCL_BLOCK_INVALID);

                        }
                    }
                    break;
                }
                else if((recBuf[0] & 0xC0) == 0x00)
                {
                    //I-Block received
                    if(recBuf[0] & 0x08)
                    {
                        if(BITISCLEAR(picc.fgTCL, BIT_CIDPRESENT))
                        {
                            return(SLOTERROR_TCL_BLOCK_INVALID);
                        }
                    }

                    if((recBuf[0] & 0x02) == 0x00)
                    {
                        return(SLOTERROR_TCL_BLOCK_INVALID);
                    }

                    if(recBuf[0] & 0x04)                     // Not used NAD
                    {
                        return(SLOTERROR_TCL_BLOCK_INVALID);
                    }

                    if(BITISSET(picc.fgTCL, BIT_PCDCHAINING))
                    {
                        return(SLOTERROR_TCL_BLOCK_INVALID);
                    }
                    else
                    {
                        if((picc.PCB ^ recBuf[0]) & 0x01) //Block number error
                        {
                            return(SLOTERROR_TCL_BLOCK_INVALID);
                        }
                        
                        TOGGLE_BIT(picc.fgTCL, BIT_PCDBLOCKNUMBER);
                        
                        if(recBuf[0] & 0x10)
                        {
                            SET_BIT(picc.fgTCL, BIT_PICCCHAINING);
                        }
                        else
                        {
                            CLEAR_BIT(picc.fgTCL, BIT_PICCCHAINING);
                        }
                        break;
                    }
                }
                else
                {
                    return(SLOTERROR_TCL_BLOCK_INVALID);
                }
            }
            else if(ret == ERROR_PROTOCOL)
            {
                if(BITISSET(picc.fgTCL, BIT_WTXREQBEFORE))
                {
                    SET_BIT(picc.fgTCL, BIT_WTXREQUEST);
                    CLEAR_BIT(picc.fgTCL, BIT_WTXREQBEFORE);
                }
                if(BITISSET(picc.fgTCL, BIT_PICCCHAINING))
                {
                    if(ackRetry == 2)
                    {
                        ret = SLOTERROR_T1_3RETRY_FAIL_RESYNCH_FAIL;
                        break;
                    }
                    PiccSfgDelay(picc.SFGI);
                    ret = TclRBlockCmd(0xA2,recBuf,recLen);
                    ackRetry++;
                }
                else
                {
                    if(frameRetry == 2)
                    {
                        ret = SLOTERROR_T1_3RETRY_FAIL_RESYNCH_FAIL;
                        break;
                    }
                    PiccSfgDelay(picc.SFGI);
                    ret = TclRBlockCmd(0xB2,recBuf,recLen);
                    frameRetry++;
                }
            }
            else
            {
                CLEAR_BIT(picc.fgTCL, BIT_WTXREQBEFORE);
                return SLOTERROR_ICC_MUTE;
            }
        }
    }while(resend);
    
    return(ret);
}




UINT8 PiccStandardApduTCL(UINT8 *cmdBuf, UINT16 senLen, UINT8 *recBuf, UINT16 *recLen, UINT8 *level)
{
    UINT8  ret = SLOT_NO_ERROR;
    UINT16 i;
    UINT8  tempSenLen;
    UINT8  offset;
    UINT8  tempRecLen;
    UINT8  *pSenAddr;
    UINT8  *pRecAddr;
    UINT8  ChainLastLen = 0;
    UINT8  LastCCIDRemainLen = 0x00;


    PrtMsg(DBGL2, "%s: start, senLen = %02X\n", __FUNCTION__, senLen);    

    pSenAddr = cmdBuf;
    pRecAddr = recBuf;
    tempRecLen = 0;
    if(*level == 0x10)
    {
        senLen = 0;
        *recLen = 0;
        for(i = 0; i < ChainLastLen; i++)
        {
            recBuf[i] = tempIBlockBuf[i];
        }
        tempRecLen   = ChainLastLen;
        *recLen      = ChainLastLen;
        ChainLastLen = 0;
    }
    else if(((*level == 0x03) || (*level == 0x02)) && (LastCCIDRemainLen != 0))
    {
        i = senLen;
        while(i--)
        {
            pSenAddr[i + LastCCIDRemainLen] = pSenAddr[i];
        }
        for(i=0; i < LastCCIDRemainLen; i++)
        {
            pSenAddr[i] = tempIBlockBuf[i];
        }
        senLen += LastCCIDRemainLen;
        LastCCIDRemainLen = 0;
    }

    while(senLen)
    {
        CLEAR_BIT(picc.fgTCL, BIT_PICCCHAINING);
        if((senLen <= picc.FSC) && ((*level == 0x02) || (*level == 0x00)))
        {
            CLEAR_BIT(picc.fgTCL, BIT_PCDCHAINING);
            picc.PCB = 0x02;            // I-block, no chaining
            if(BITISSET(picc.fgTCL, BIT_PCDBLOCKNUMBER))
            {
                picc.PCB |= 0x01;
            }
            if(BITISSET(picc.fgTCL, BIT_CIDPRESENT))
            {
                picc.PCB |= 0x08;
            }
            tempSenLen = (UINT8)senLen;
            ret = TCLDataSendErrorCheck(pSenAddr, tempSenLen, pRecAddr, &tempRecLen);

            senLen = 0;

            if(ret == SLOT_NO_ERROR)
            {
                if(BITISSET(picc.fgTCL, BIT_CIDPRESENT))
                {
                    offset = 2;
                }
                else
                {
                    offset = 1;
                }
                tempRecLen -= offset;
                for(i = 0; i < tempRecLen; i++)
                {
                    pRecAddr[i] = pRecAddr[i + offset];
                }
                if(tempRecLen == 1)
                {
                    pRecAddr[1] = 0x90;
                    pRecAddr[2] = 0x00;
                    *recLen     = 3;
                }
                else
                {
                    *recLen = tempRecLen;
                }
            }
            else
            {
                CLEAR_BIT(picc.fgTCL, BIT_PICCCHAINING);
            }
        }
        else
        {
            // PCD chaining
            SET_BIT(picc.fgTCL, BIT_PCDCHAINING);
            picc.PCB = 0x12;
            if(BITISSET(picc.fgTCL, BIT_PCDBLOCKNUMBER))
            {
                picc.PCB |= 0x01;
            }
            if(BITISSET(picc.fgTCL, BIT_CIDPRESENT))
            {
                picc.PCB |= 0x08;
            }
            ret = TCLDataSendErrorCheck(pSenAddr, picc.FSC, pRecAddr, &tempRecLen);

            CLEAR_BIT(picc.fgTCL, BIT_PCDCHAINING);
            if(ret == SLOT_NO_ERROR)
            {

                senLen   -= picc.FSC;
                pSenAddr += picc.FSC;
                if(((*level == 0x01) || (*level == 0x03)) && (senLen < picc.FSC))
                {

                    LastCCIDRemainLen = (UINT8) senLen;
                    for(i = 0; i < LastCCIDRemainLen; i++)
                    {
                        tempIBlockBuf[i] = pSenAddr[i];
                    }
                    *recLen = 0;
                    *level  = 0x10;
                    senLen  = 0;
                    CLEAR_BIT(picc.fgTCL, BIT_PICCCHAINING);
                    return(ret);
                }
            }
            else
            {
                senLen = 0;
                CLEAR_BIT(picc.fgTCL, BIT_PICCCHAINING);
            }
        }
    }
    // PICC Chaining
    while(BITISSET(picc.fgTCL, BIT_PICCCHAINING))
    {
        picc.PCB = 0xA2;
        if(BITISSET(picc.fgTCL, BIT_PCDBLOCKNUMBER))
        {
            picc.PCB |= 0x01;
        }
        if(BITISSET(picc.fgTCL, BIT_CIDPRESENT))
        {
            picc.PCB |= 0x08;
        }
        pRecAddr += tempRecLen;
        ret = TCLDataSendErrorCheck(pSenAddr, 0, pRecAddr, &tempRecLen);

        if(ret == SLOT_NO_ERROR)
        {
            if(BITISSET(picc.fgTCL, BIT_CIDPRESENT))
            {
                offset = 2;
            }
            else
            {
                offset = 1;
            }
            tempRecLen -= offset;
            for(i = 0; i < tempRecLen; i++)
            {
                pRecAddr[i] = pRecAddr[i + offset];
            }
            *recLen += tempRecLen;

            if(*recLen > APDURECLENTHREHOLD)
            {
                if(*level == 0x10)                    //Continue Chaining
                {
                    *level = 0x03;
                }
                else
                {
                    *level = 0x01;                    //Chaining Beginning
                }
                ChainLastLen = (UINT8)(*recLen - APDURECLENTHREHOLD);
                for(i = 0; i < ChainLastLen; i++)
                {
                    tempIBlockBuf[i] = recBuf[i + APDURECLENTHREHOLD];
                }
                *recLen       = APDURECLENTHREHOLD;
                pcd.piccPoll  = FALSE;
                pcd.pollDelay = 1000;              // 1000ms, start another poll
                return(SLOT_NO_ERROR);
            }
        }
        else 
        {
            CLEAR_BIT(picc.fgTCL, BIT_PICCCHAINING);
        }
    }
    if(*level == 0x10)
    {
        *level = 0x02;
    }
    else
    {
        *level = 0x00;
    }
    if(ret == SLOT_NO_ERROR)
    {

        pcd.piccPoll  = FALSE;
        pcd.pollDelay = 1000;    // 1000ms, start another poll
    }
    else
    {
        pcd.piccPoll = TRUE;
        recBuf[0]        = 0x63;
        recBuf[1]        = 0x00;
        *recLen          = 2;
        CLEAR_BIT(picc.fgTCL, BIT_PCDCHAINING);
        CLEAR_BIT(picc.fgTCL, BIT_PICCCHAINING);
    }
    
    return(ret);
}

