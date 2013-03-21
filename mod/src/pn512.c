/*
* Name: PN512 drive source file
* Date: 2012/12/04
* Author: Alex Wang
* Version: 1.0
*/


#include <linux/spi/spi.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm-generic/gpio.h>
#include <asm/io.h>
#include <linux/gpio.h>

#include "common.h"
#include "pn512.h"
#include "debug.h"



static struct spi_device *PN512 = NULL; 



static INT32 PN512_SendCmd(UINT8 *senBuf, UINT32 senLen)
{
    INT32 ret;
    
    
    if((ret = spi_write(PN512, senBuf, senLen)) < 0)
    {
        PrtMsg(DBGL1, "%s: Fail to send SPI command!!!, ret = %X\n", __FUNCTION__, ret);
        return(-1);
    }
    return(0);
}

static INT32 PN512_SendThenRead(UINT8 *senBuf, UINT8 senLen, UINT8 *recBuf, UINT8 recLen)
{
    INT32 ret;
    
    
    if((ret = spi_write_then_read(PN512, senBuf, senLen, recBuf,recLen)) < 0)
    {
        PrtMsg(DBGL1, "%s: Fail to send SPI command!!!, ret = %X\n", __FUNCTION__, ret);
        return(-1);
    }
    
    return(0);
}

INT32 PN512_RegWrite(UINT8 reg, UINT8 value)
{
    UINT8 cmdBuf[2];
    

    reg <<= 1;
    CLEAR_BIT(reg, BIT(7));
    cmdBuf[0] = reg;
    cmdBuf[1] = value;

    if(PN512_SendCmd(cmdBuf, 2))
    {
        PrtMsg(DBGL1, "%s: reg write fail\n", __FUNCTION__);
         return(-1);
    }

  
    return(0);
}

UINT8 PN512_RegRead(UINT8 reg)
{
    UINT8 tempReg;
    UINT8 tempVal = 0x00;

    
    tempReg = (reg << 1);
    SET_BIT(tempReg, BIT(7));


    if(PN512_SendThenRead(&tempReg, 1, &tempVal, 1) < 0)
    {
        PrtMsg(DBGL1, "%s: reg read fail\n", __FUNCTION__);     
        return(-1);
    }

    PrtMsg(DBGL5, "%s: reg = %02X, value = %02X\n", __FUNCTION__, reg , tempVal);
    return(tempVal);
}

/******************************************************************************
NxpPCD_FifoWrite()
******************************************************************************/
INT32 PN512_FIFOWrite(UINT8 *data, UINT8 len)
{
    UINT8 buf[65];
    UINT8 i;
    

    if (len > 64)
    {
        return(-1);
    }
    
    buf[0] = (REG_FIFODATA << 1) & 0x7E;     // accroding to PN512: bit 7 = 0, write; bit 0 = 0
    memcpy(buf + 1, data, len);

    PrtMsg(DBGL1, "%s: FIFOBUF:", __FUNCTION__);
    for(i = 0; i <= len; i++)
    {
        PrtMsg(DBGL1, " %02X", buf[i]);
    }
    PrtMsg(DBGL1, "\n");

    if (PN512_SendCmd(buf, len + 1) < 0)
    {
        PrtMsg(DBGL1, "%s: FIFO write fail\n", __FUNCTION__);
        return(-1);
    }
    
    return(0);
}


/******************************************************************************
NxpPCD_FifoRead()
******************************************************************************/
INT32 PN512_FIFORead(UINT8 *data, UINT8 len)
{
    UINT8 i;
    UINT8 buf[64];
    

    if (len > 64)
    {
       	return(-1);
    }
    
    if (len == 0)
    {
        return(0);
    }
    
    memset(buf, (REG_FIFODATA << 1) | 0x80, len);    // accroding to PN512: bit 7 = 1, read; bit 0 = 0


    for(i = 0; i < len; i++)
    {
        if(PN512_SendThenRead(buf + i, 1, data + i, 1) < 0)
        {
	        PrtMsg(DBGL1, "%s: FIFO read fail\n", __FUNCTION__);
	        return(-1);
        }
    }

//    memcpy(data, buf, len);
    
    return(0);
}


void SetPN512Timer(UINT16 timeOut)
{
    PN512_RegWrite(REG_TMODE, 0x82);                        //  TAuto=1,TAutoRestart=0,TPrescaler=677=2a5h
    PN512_RegWrite(REG_TPRESCALER, 0xA5);                   //  Indicate 100us per timeslot
    PN512_RegWrite(REG_TRELOADVAL_HI, (UINT8)(timeOut>>8));  	// 
    PN512_RegWrite(REG_TRELOADVAL_LO, (UINT8)timeOut);        //
    PN512_RegWrite(REG_COMMIRQ, 0x01);                      // Clear the TimerIrq bit
}


static INT32 PN512_INT_CFG(void)
{
    return(0);
}

static INT32 PN512_probe(struct spi_device *spi)
{
    unsigned int *reg = NULL;
    
  
    PrtMsg(DBGL1, "%s: start\n", __FUNCTION__);
    
    PN512 = spi;

    reg = ioremap(0x480021C8, 1);
    *reg = (((1 << 3)  << 16)  | ((1 << 8) |  (1 << 3)));        // spi1 simo, clk: spi mode, pull up enable
    reg = ioremap(0x480021CC, 1);
    *reg = (((1 << 3)  << 16)  |  ((1 << 8) | (1 << 4) | (1 << 3)));        // spi1 cs0, somi: spi mode, pull up enable
    


    PN512_RegWrite(REG_COMMAND, CMD_SOFTRESET);
    mdelay(100);
  
    PN512_RegWrite(REG_TXCONTROL, 0x00);         //Turn off the Antenna

    PN512_RegWrite(REG_COMMAND, 0x00);           // Switch on the analog part of the receiver 
    PN512_RegWrite(REG_CONTROL, 0x10);           // Set PN512 in initiator mode
    PN512_RegWrite(REG_FIFOLEVEL, 0x80);         // flush FIFO

    return(0);
}

static INT32 __devexit PN512_remove(struct spi_device *spi)
{
    return(0);
}

static const struct spi_device_id pn512_id_table[] =
{
    {"pn512", 0},
    {}
};

static struct spi_driver pn512_driver =
{
    .driver = 
    {
        .name  = "pn512",
        .bus   = &spi_bus_type,
        .owner = THIS_MODULE,
    },
 
    .id_table  = pn512_id_table,
    
    .probe = PN512_probe,
    .remove = __devexit_p(PN512_remove),
};

INT32 Pn512Init(void)
{
    PrtMsg(DBGL1, "%s: start\n", __FUNCTION__);
    
    if(PN512_INT_CFG())    return(-1);

    if(spi_register_driver(&pn512_driver) < 0)
    {
        PrtMsg(DBGL1, "%s: Fail to registe the driver PN512!!!\n", __FUNCTION__);
        return(-1);
    }
 
    return(0);
}

INT32 Pn512Uninit(void)
{
    PN512_RegWrite(REG_TXCONTROL, 0x00);    // turn off antenna
    spi_unregister_driver(&pn512_driver);
    
    return(0);
}
