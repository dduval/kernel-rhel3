/*******************************************************************

    File Name   :   RCSDIAG.H

    Author      :   K.V.Subash & Parthiban Baskar

    Date        :   8 December 1997

    Purpose     :   RCS DIAg cmd grp codes

	Copyright	:	American Megatrends Inc., 1997-1998

********************************************************************/

#ifndef __RCSDIAG_H__
#define __RCSDIAG_H__



/************************************************************************/
/**************  REMOTE DIAGNOSTICS COMMANDS ****************************/
/************************************************************************/

/*******************************GROUP 080**************************/
#define  RCS_DIAG_READ_BYTE            0x0801
#define  RCS_DIAG_WRITE_BYTE           0x0802
#define  RCS_DIAG_READ_WORD            0x0803
#define  RCS_DIAG_WRITE_WORD           0x0804
#define  RCS_DIAG_READ_DWORD           0x0805
#define  RCS_DIAG_WRITE_DWORD          0x0806
#define  RCS_DIAG_DUMP_BLOCK           0x0807
#define  RCS_DIAG_SCAN_MEMORY          0x0808
/************No of funtions beginning with 080**********************/
#define FNS_080 8
/*******************************************************************/


/*******************************GROUP 081**************************/

/************No of funtions beginning with 081**********************/
#define FNS_081 0
/*******************************************************************/


/*******************************GROUP 082**************************/

/************No of funtions beginning with 082**********************/
#define FNS_082 0
/*******************************************************************/



/*******************************GROUP 083**************************/

/************No of funtions beginning with 083**********************/
#define FNS_083 0
/*******************************************************************/



/*******************************GROUP 084**************************/

/************No of funtions beginning with 084**********************/
#define FNS_084 0
/*******************************************************************/



/*******************************GROUP 085**************************/

/************No of funtions beginning with 085**********************/
#define FNS_085 0
/*******************************************************************/


/*******************************GROUP 086**************************/

/************No of funtions beginning with 086**********************/
#define FNS_086 0
/*******************************************************************/



/*******************************GROUP 087**************************/

/************No of funtions beginning with 087**********************/
#define FNS_087 0
/*******************************************************************/



/*******************************GROUP 088**************************/

/************No of funtions beginning with 088**********************/
#define FNS_088 0
/*******************************************************************/



/*******************************GROUP 089**************************/

/************No of funtions beginning with 089**********************/
#define FNS_089 0
/*******************************************************************/


/*******************************GROUP 08A**************************/

/************No of funtions beginning with 08A**********************/
#define FNS_08A 0
/*******************************************************************/



/*******************************GROUP 08B**************************/

/************No of funtions beginning with 08B**********************/
#define FNS_08B 0
/*******************************************************************/


/*******************************GROUP 08C**************************/

/************No of funtions beginning with 08C**********************/
#define FNS_08C 0
/*******************************************************************/




/*******************************GROUP 08D**************************/

/************No of funtions beginning with 08D**********************/
#define FNS_08D 0
/*******************************************************************/



/*******************************GROUP 08E**************************/

/************No of funtions beginning with 08E**********************/
#define FNS_08E 0
/*******************************************************************/



/*******************************GROUP 08F**************************/

/************No of funtions beginning with 08F**********************/
#define FNS_08F 0
/*******************************************************************/





/************************************************************************/
/**************  REMOTE CONSOLE DIAGNOSTICS COMMANDS END ****************/
/************************************************************************/

/************************************************************************/
/*************  REMOTE CONSOLE DIAGNOSTICS STRUCTURES *******************/
/************************************************************************/

//these structures define arguments to the various functions.
//when a RCS Command Packet is put in the RCS Q , It usually
//is appended with another argument structure for a specific function
//The CCB Handler sees that it is a RCS CCB .But since the total length 
//along with the Arguments for the various RCS Functions is variable
//the length of the total packet is put in the CCB Header.It then writes
//that many bytes into the RCS Q.
//The RCS Dispatcher takes only the RCS COMMAND PACKET header
//and then branches to the neccessary function. 
//Then the function knows how many arguments to read and reads them from
//the RCS Queue.

typedef struct _RCS_DIAG_ReadWriteByteArgs
{
   unsigned long  Addr;
   unsigned short Attributes;
   unsigned char  Data;
}RCS_DIAG_RW_BYTE_ARGS;

typedef struct _RCS_DIAG_ReadWriteByteCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DIAG_RW_BYTE_ARGS Data;
}RCS_DIAG_RW_BYTE_COMMAND;

typedef struct _RCS_DIAG_ReadWriteWordArgs
{
   unsigned long  Addr;
   unsigned short Attributes;
   unsigned short Data;
}RCS_DIAG_RW_WORD_ARGS;

typedef struct _RCS_DIAG_ReadWriteWordCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DIAG_RW_WORD_ARGS Data;
}RCS_DIAG_RW_WORD_COMMAND;

typedef struct _RCS_DIAG_ReadWriteDwordArgs
{
   unsigned long  Addr;
   unsigned short Attributes;
   unsigned long  Data;
}RCS_DIAG_RW_DWORD_ARGS;

typedef struct _RCS_DIAG_ReadWriteDwordCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DIAG_RW_DWORD_ARGS Data;
}RCS_DIAG_RW_DWORD_COMMAND;

typedef struct _RCS_DIAG_DumpBlockArgs
{
   unsigned long  Addr;
   unsigned short Attributes;
   unsigned short Length;
   unsigned char	Data[1];
}RCS_DIAG_DUMP_BLOCK_ARGS;

typedef struct _RCS_DIAG_DumpBlockCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DIAG_DUMP_BLOCK_ARGS Data;
}RCS_DIAG_DUMP_BLOCK_COMMAND;

typedef struct _RCS_DIAG_ScanMemoryArgs
{
   unsigned long  StartAddr;
   unsigned long  EndAddr;
   unsigned short Attributes;
   unsigned char  PatternLength;
   unsigned char  Occurance;
   unsigned long  FoundAddr;
   unsigned char	Data[1];
}RCS_DIAG_SCAN_MEMORY_ARGS;

typedef struct _RCS_DIAG_ScanMemoryCommand
{
   RCS_COMMAND_PACKET RCSCmdPkt;
   RCS_DIAG_SCAN_MEMORY_ARGS Data;
}RCS_DIAG_SCAN_MEMORY_COMMAND;

#endif
