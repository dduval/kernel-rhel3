/*******************************************************************
    File Name   :   flash.H
        Reversed engineered from the following file in SourceSafe:
          \Rac\Utils\raccfgLite\raccfg\winnt\Inc\Cfg\Flash.h
********************************************************************/
#ifndef AMI_FLASH_H
#define AMI_FLASH_H


/*-------------- Packet structure of the Flash Command Packets ------------*/

typedef struct  {
        char AmiSig[4];
        unsigned char Major;
        unsigned char Minor;
        char Padding[10];           /* 16 Bit Align of i960 Compiler */
        char BuildDate[16];
        char BuildTime[16];
        char SdkVersion[8];
} FwVer;


typedef struct {
    unsigned short Command;
    unsigned short Status;
    unsigned short Length;
    unsigned long  Addr;
    unsigned long  Size;
    unsigned long  ExtStatus;
    unsigned long  Reserved[3];
    unsigned short ResShort;
    BYTE           Data[1];     /* Data Follows the 32 Byte of Header */
} FLASH_CMD_PKT;

typedef struct
{
    RCS_COMMAND_PACKET  RCSCmdPkt;
    FLASH_CMD_PKT       FlashHead;
} FLASH_RCS_HEADER;

/*----------------------- Flash Command Definitions -----------------------*/
//# define NEW_FLASH_CMD          0x40    /* Command used to goto flash mode   */
//# define ISSUE_FLASH_CMD        0x41    /* Command port value for flash cmd  */

# define FLASH_SIZE             0x401
# define FLASH_BLOCKSIZE        0x402
# define FLASH_ERASE            0x403
# define FLASH_READ             0x404
# define FLASH_WRITE            0x405
# define FLASH_VERIFY           0x406

/*-------------------------- Flash Return Values --------------------------*/
# define FLASH_SUCCESS              0x0000
# define FLASH_UNSUPPORTED_COMMAND  0x0001
# define FLASH_OUT_OF_RANGE         0x0002
# define FLASH_ERROR                0x00FF

/*----------- Defines of Extended Status Commands and ErrorCodes ----------*/

# define FLASH_ERR_SUCCESS          0x00
# define FLASH_ERR_FAIL             0x01    /* 'command' against 'block' n failed */
# define FLASH_ERR_VPP_LOW          0x02    /* 'command' against 'block' n failed due to low Vpp */
# define FLASH_ERR_DEV_PROTECT      0x03    /* 'command' against 'block' n failed due to protection */
# define FLASH_ERR_CMDSEQ           0x04    /* 'command' against 'block' n failed due to internal error */
# define FLASH_ERR_RESERVED         0x05
# define FLASH_ERR_STOP             0x06
# define FLASH_ERR_START            0x07

# define FLASH_XCMD_FLASH_OVER      0x00
# define FLASH_XCMD_CLEAR_LOCK      0x01
# define FLASH_XCMD_BLOCK_LOCK      0x02
# define FLASH_XCMD_WRITE           0x03
# define FLASH_XCMD_ERASE           0x04
# define FLASH_XCMD_VERIFY          0x05
# define FLASH_XCMD_FLASH_SIZE      0x06
# define FLASH_XCMD_READ            0x07
# define FLASH_XCMD_DLL_INITIAL     0x0e
# define FLASH_XCMD_DLL_HARD_RESET  0x0f

#define FLASH_STATUS_DISSECT(statusIn,commandOut,errorOut,locationOut)  \
    {   commandOut  = (statusIn >> 28) & 0x0F;                          \
        errorOut    = (statusIn >> 24) & 0x0F;                          \
        locationOut =  statusIn        & 0x00FFFFFFL; }
    
/*----------------------------- Other defines -----------------------------*/
#define FLASH_VERSION_OFFSET        0x00004000L
#define FLASH_INFO_START_LOC        0x00004000L
#define FLASH_CHECKSUM_LOC          0x00004040L
#define FLASH_NOSDK_CRC_START_LOC   0x00004100L
#define FLASH_SDK_CRC_START_LOC     0x00020000L
#define FLASH_BOOT_BLOCK_OFFSET     0x000F0000L
#define FLASH_BOOT_BLOCK_SIZE       (64*1024L)
#define FLASH_SDK_SIGNATURE         "SDK"   /*prabhu 19th Jan 1999. For Sdk support */


# endif     /* #ifndef AMI_FLASH_H */
