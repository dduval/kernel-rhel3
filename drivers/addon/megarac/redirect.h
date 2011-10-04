/*******************************************************************

    File Name   :   REDIRECT.H

    Author      :   K.V.Subash & Parthiban Baskar

    Date        :   4 February 1998

    Purpose     :   Equates and data types used for Console redirection

    Copyright   :   American Megatrends Inc., 1997-1998

    TAB Spacing :   4

********************************************************************/

#ifndef  __REDIRECT_H__
#define  __REDIRECT_H__

#include "rcs.h"
#include "rcsred.h"

/*  FRICause codes for various FRI's    */
#define GetDCInfo_FRI       0x00000001  // Host responds with SendDCInfo RCS.
#define GetDCPacket_x_FRI   0x00000002  // where XXXX is the DCPacketNumber.
                // 0x00000002 means GetDCPacket [0]
                // 0x00010002 means GetDCPacket [1]
                // 0x00FF0002 means GetDCPacket [255] and so on..
#define SendDCComplete_FRI  0x00000003  // Host knows that GraphicScreen has transferred.
#define SendKeyEvent_FRI    0x00000004  // Host issues ReadKey RCS to Firmware.
#define SendMouseEvent_FRI  0x00000005  // Host issues ReadMouseEvent RCS to Firmware.
#define SendAlertToHost_FRI 0x00000006  // To report any alert/event to the Host driver,
                // apart from paging/logging etc.
#define HOST_OS_SHUTDOWN_REQUEST_FRI  0x00000007  // Host os shutdown request.
#define ModeChangedToGraphics_FRI   0x00000008  // Tell host driver that video mode
                                                // has changed to Graphics.
#define ServiceDYRM_FRI    0x00000009  // Used by the remote s/w to detect
                // the presence of the RACService on the host.
#define PassThruData_FRI   0x0000000a  // Used by the remote s/w to pass data
                // thru to the OS Driver
#define Maximum_FRI     PassThruData_FRI
                // The above define should be changed if new FRI's are added.

/*  RESERVED FRICause codes for s/w group   */
#define Reserved_FRI_0      0x00000000
#define Reserved_FRI_255    0x000000ff


/*  The following defines for VideoModes are duplicated in
    startup\src\fixddata.c for operational ease.  If they change here,
    they have to change there too     */
/*  VideoModes  */
#define TEXTMODE        0
#define GRAPHICSMODE    1

/*  Keyboard Controller Ports   */
#define HostKBStatusPort        (u8 *)0x0064
#define HostKBCommandPort       (u8 *)0x0064
#define HostKBDataPort          (u8 *)0x0060

/*  Keyboard Controller Commands    */
#define CMDLockLocalKBAccess        0xad
#define CMDEnableLocalKBAccess      0xae
#define CMDWriteKBOutputBuffer      0xd2

#define KBTimeoutDelay              0x1000
#define SendTimeoutDelay            0x1800

/*  Other defines                   */
#define MOUSE_DATA_BUFFER_SIZE      (5*1024)


/*
    Function prototypes
*/
extern  TWOBYTES    StartConsoleRedirection (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    SendKey (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    SendMouseEvent (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    StopConsoleRedirection (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    ReadKey (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    ReadMouseEvent (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    GetDCInfo (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    SendDCInfo (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    GetDCPacket (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    SendDCPacket (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    SendDCComplete (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    ServiceDYRM (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  void        SendMouseEventToHost (u8 * DataPtr, FOURBYTES FRICause);
extern  int         FirmwareRequestInterrupt (FOURBYTES FRICause);
extern  int         IssueFRI (void);
extern  TWOBYTES    Stuff (u8 ScanCode);

extern  int         VGAScreenDump (void);

extern  TWOBYTES    PassThruDataToHost (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    PassThruDataToRemote (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  TWOBYTES    ReadPassThruData (RCS_COMMAND_PACKET * RCSCmdPkt);
extern  void        CleanUpPassThruBuffers (short Entry);

#endif

