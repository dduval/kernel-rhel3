/**********************************************************************

        FILE        :       DBG_FN.H

        PURPOSE     :       DebugLog,   Defines values for FileDesc 

        AUTHOR(s)   :       Srikumar and Raja

        DATE        :       28th September 1998

        MODIFICATION HISTORY    :


**********************************************************************/
#ifndef __DBG_FN_H__
#define __DBG_FN_H__


//  The FileDesc value 0x00 & 0xFF are reserved.
  
#define DL_FN_ADMIN     0x01
#define DL_FN_ALERTER   0x02
#define DL_FN_BATRCS    0x03
#define DL_FN_BATTMON   0x04
#define DL_FN_CCBHDLR   0x05
#define DL_FN_CQ        0x06
#define DL_FN_DIALOUT   0x07
#define DL_FN_INTERFAC  0x08
#define DL_FN_PPP       0x09
#define DL_FN_SERCOMM   0x0A
#define DL_FN_SLIP      0x0B
#define DL_FN_SOCKCOMM  0x0C
#define DL_FN_TABLES    0x0D
#define DL_FN_DMEM      0x0E
#define DL_FN_ERRORHDL  0x0F
#define DL_FN_EVTLOG    0x10
#define DL_FN_HBMON     0x11
#define DL_FN_HHF       0x12
#define DL_FN_HHFDATA   0x13
#define DL_FN_HHFTYPE   0x14
#define DL_FN_MON       0x15
#define DL_FN_RCSHHF    0x16
#define DL_FN_MEGARAC   0x17
#define DL_FN_MEGARACT  0x18
#define DL_FN_MG90XX    0x19
#define DL_FN_COMRCS    0x1A
#define DL_FN_NVDATA    0x1B
#define DL_FN_NVRAM     0x1C
#define DL_FN_PCCARD    0x1D
#define DL_FN_PCMCIA    0x1E
#define DL_FN_POST      0x1F
#define DL_FN_AGPINTFC  0x20
#define DL_FN_REDIRECT  0x21
#define DL_FN_RCSAC     0x22
#define DL_FN_RCSCONN   0x23
#define DL_FN_RCSDIAG   0x24
#define DL_FN_RCSDISP   0x25
#define DL_FN_RCSIC     0x26
#define DL_FN_RCSRED    0x27
#define DL_FN_RCSUSR    0x28
#define DL_FN_RTC       0x29
#define DL_FN_MODEM     0x2A
#define DL_FN_PAGE      0x2B
#define DL_FN_SERIAL    0x2C
#define DL_FN_TAP       0x2D
#define DL_FN_SNMPSERV  0x2E
#define DL_FN_SNMPTRAP  0x2F
#define DL_FN_AMIHELP   0x30
#define DL_FN_DBGPRT    0x31
#define DL_FN_DIRPRT    0x32
#define DL_FN_REGDUMP   0x33
#define DL_FN_SIZES     0x34
#define DL_FN_VSPHDLR   0x35
#define DL_FN_RCSHDLR   0x36
#define DL_FN_MISC		0x37

#define MAX_MODULES     0x38

#endif

