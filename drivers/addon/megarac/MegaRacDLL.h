/****************************************************************************
 *  MegaRacDLL.h
 ****************************************************************************/
#ifndef MEGA_RAC_DLL_H
#define MEGA_RAC_DLL_H

#include <linux/limits.h>

/****************************************************************************
 *      pack all structures in this file
 ****************************************************************************/
#pragma pack(1)                             /* supported by all current compilers */

/*---------------------------
 * begin Firmware
 *---------------------------*/
#ifndef WORD
typedef unsigned short   WORD;
#endif

#include "admin.h"
#include "agpintfc.h"
#include "alerter.h"
#include "batmon.h"
#undef    ABSOLUTE
#include "ccbhdlr.h"
#include "comrcs.h"
#include "crc16.h"
#include "dbglog.h"
#include "dmi.h"
#include "errcod.h"
#include "evtlog.h"
#include "ftp.h"
#include "flash.h"
#include "i2crsa.h"
#include "misc.h"
#include "post.h"
#include "rcs.h"
#include "rcsadmin.h"
#include "rcsdbg.h"
#include "rcsdiag.h"
#include "rcshhf.h"
#include "rcsic.h"
#include "rcsred.h"
#include "rcssdk.h"
#include "rcsusr.h"
#include "redirect.h"
#include "rtc.h"
/* should be in rcsdiag.h, but isn't */
#define RCS_DIAG_FORCE_FAIL                 0x0810
#define FORCE_FAIL_CMD_PCISERR              0
#define FORCE_FAIL_CMD_ABORT_WITH_RESET     1
#define FORCE_FAIL_CMD_ABORT_WITHOUT_RESET  2
#define FORCE_FAIL_CMD_EAT_COMMAND          3
typedef struct _RCS_DIAG_ForceFailArgs {
   unsigned short FailCmd;
   unsigned char	FailPkt[16];
}RCS_DIAG_FORCE_FAIL_ARGS;
/*---------------------------
 * end Firmware
 *---------------------------*/

/****************************************************************************
 *  
 ****************************************************************************/

#include "MegaRacDrvr.h"

/* some drivers or APIs provide control of selected functionality at startup */

#define MEGARAC_SUPPORT_CURSOR      0x0001      /* software cursor position */
#define MEGARAC_SUPPORT_FTP         0x0002      /* file transfer */
#define MEGARAC_SUPPORT_SHUTDOWN    0x0004      /* handle OS restart */


typedef struct {
    CCB_Header  ccb;
    char        data[1];
} DC_INFO_TYPE;

typedef enum {
    megaRacIoctlRcs,
    megaRacIoctlGraphic,
    megaRacIoctlReset,
    megaRacIoctlInternal,
    megaRacIoctlEvent
} MegaRacIoctl;

typedef enum {                              /* see MegaRacFlashUpdate() */
    MegaRacFlashUpdateNone,
    MegaRacFlashUpdateIgnoreVersionAge   = 0x01,
    MegaRacFlashUpdateSkipReset          = 0x02,
    MegaRacFlashUpdateEraseSDK           = 0x04
} MegaRacFlashUpdateOptions;

/* if an error occurs during API processing,
   one of the MEGARAC_ERR_* is returned (without the upper bit set).
   If the firmware returns an error in ccb.status,
   then MEGARAC_ERR_ERROR is or'ed with the ccb.status (setting the upper bit)
   and the resulting value is returned as a typedef'ed MEGARAC_ERROR */
typedef enum {
    MEGARAC_ERR_NONE,
    MEGARAC_ERR_NOT_SUPPORTED,
    MEGARAC_ERR_FAILED,
    MEGARAC_ERR_BAD_HANDLE,
    MEGARAC_ERR_DEAD,
    MEGARAC_ERR_FLASH_IN_PROGRESS,          /* special case, see MegaRacFlashUpdate() */
    MEGARAC_ERR_FLASH_NO_RESOURCES,
    MEGARAC_ERR_FLASH_IMAGE_FILE_NOT_FOUND,
    MEGARAC_ERR_FLASH_IMAGE_FILE_CHECKSUM,
    MEGARAC_ERR_FLASH_IMAGE_FILE_SIZE,
    MEGARAC_ERR_FLASH_IMAGE_FILE_ACCESS,
    MEGARAC_ERR_FLASH_IMAGE_FILE_AGE,
    MEGARAC_ERR_FLASH_FIRMWARE_ACCESS,
    MEGARAC_ERR_FIRMWARE_LOGIC,
    MEGARAC_ERR_FLASH_SDK_FILE
} MEGARAC_ERROR;

#if UINT_MAX>0x0000ffff 
    #define MEGARAC_ERR_ERROR 0x80000000    /* ccb.Status or'd in */
#else
    #define MEGARAC_ERR_ERROR 0x8000
#endif


typedef void* MEGARAC_HANDLE;
#define MEGARAC_INVALID_HANDLE ((MEGARAC_HANDLE)(-1))

    /* group 0 */
extern MEGARAC_ERROR MegaRacGetFirmwareVersion   ( MEGARAC_HANDLE handle, GET_VERSION_TYPE                      *pCAM );
extern MEGARAC_ERROR MegaRacGetInfo              ( MEGARAC_HANDLE handle, GET_MEGARAC_INFO_TYPE                 *pCAM );
extern MEGARAC_ERROR MegaRacGetTotalEvents       ( MEGARAC_HANDLE handle, RCS_FN_GET_TOTAL_NO_OF_EVENTS_ARGS    *pCAM );
extern MEGARAC_ERROR MegaRacGetRecentEvents      ( MEGARAC_HANDLE handle, RCS_FN_GET_MOST_RECENT_N_EVENTS_ARGS  *pCAM );
extern MEGARAC_ERROR MegaRacGetOldestEvents      ( MEGARAC_HANDLE handle, RCS_FN_GET_MOST_OLD_N_EVENTS_ARGS     *pCAM );
extern MEGARAC_ERROR MegaRacClearEvents          ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacGetCurrentPostLog    ( MEGARAC_HANDLE handle, RCS_FN_GET_CURRENT_POST_LOG_ARGS      *pCAM );
extern MEGARAC_ERROR MegaRacGetAllPostLogs       ( MEGARAC_HANDLE handle, RCS_FN_GET_CURRENT_POST_LOG_ARGS      *pCAM );
extern MEGARAC_ERROR MegaRacClearAllPostLogs     ( MEGARAC_HANDLE handle );
    /* group 1 */
extern MEGARAC_ERROR MegaRacGetDmi               ( MEGARAC_HANDLE handle, RCS_FN_GET_DMI_INFO_ARGS              *pCAM );
extern MEGARAC_ERROR MegaRacGetIpmi              ( MEGARAC_HANDLE handle, RCS_FN_GET_IPMI_INFO_ARGS             *pCAM );
extern MEGARAC_ERROR MegaRacReportDmi            ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacGetOsStatus          ( MEGARAC_HANDLE handle, RCS_FN_GET_OS_STATUS_ARGS             *pCAM );
extern MEGARAC_ERROR MegaRacGetHostStatus        ( MEGARAC_HANDLE handle, RCS_FN_GET_HOST_STATUS_ARGS           *pCAM );
extern MEGARAC_ERROR MegaRacGetGenericData       ( MEGARAC_HANDLE handle, RCS_FN_GET_GENERIC_DATA_ARGS          *pCAM );
extern MEGARAC_ERROR MegaRacReportGenericData    ( MEGARAC_HANDLE handle, RCS_FN_REPORT_GENERIC_DATA_ARGS       *pCAM );
    /* group 2 */
extern MEGARAC_ERROR MegaRacGetBattery           ( MEGARAC_HANDLE handle, GET_BAT_STATUS_TYPE                   *pCAM );
    /* group 3 */
extern MEGARAC_ERROR MegaRacGetHhfNumber         ( MEGARAC_HANDLE handle, GET_NUM_CATEGORY_TYPE                 *pCAM );
extern MEGARAC_ERROR MegaRacGetHhfName           ( MEGARAC_HANDLE handle, GET_CATEGORY_NAME_TYPE                *pCAM );
extern MEGARAC_ERROR MegaRacGetHhfSubDiv         ( MEGARAC_HANDLE handle, GET_NUM_SUBDIVISIONS_TYPE             *pCAM );
extern MEGARAC_ERROR MegaRacGetHhfSubDivName     ( MEGARAC_HANDLE handle, GET_SUBDIV_NAME_TYPE                  *pCAM );
extern MEGARAC_ERROR MegaRacGetHhfValue          ( MEGARAC_HANDLE handle, GET_HHF_VALUE_TYPE                    *pCAM );
extern MEGARAC_ERROR MegaRacGetSdkCategories     ( MEGARAC_HANDLE handle, SDK_GET_NUM_CATEGORY_TYPE             *pCAM );
extern MEGARAC_ERROR MegaRacGetSdkCategoryName   ( MEGARAC_HANDLE handle, SDK_GET_CATEGORY_NAME_TYPE            *pCAM );
extern MEGARAC_ERROR MegaRacGetSdkSubdivisions   ( MEGARAC_HANDLE handle, SDK_GET_NUM_SUBDIVISIONS_TYPE         *pCAM );
extern MEGARAC_ERROR MegaRacGetSdkSubdivisionName( MEGARAC_HANDLE handle, SDK_GET_SUBDIV_NAME_TYPE              *pCAM );
extern MEGARAC_ERROR MegaRacGetSdkValue          ( MEGARAC_HANDLE handle, GET_SDK_VALUE_TYPE                    *pCAM );
extern MEGARAC_ERROR MegaRacGetSdkConfiguration  ( MEGARAC_HANDLE handle, SDK_CONFIG_TYPE                       *pCAM );
extern MEGARAC_ERROR MegaRacModifySdkConfiguration(MEGARAC_HANDLE handle, SDK_CONFIG_TYPE                       *pCAM );
extern MEGARAC_ERROR MegaRacI2cRepeatedStartAccess(MEGARAC_HANDLE handle, I2C_REPEATED_START_WITH_STATUS_TYPE   *pCAM );
    /* group 4 */
extern MEGARAC_ERROR MegaRacShutdownHost         ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacResetHost            ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacPowerCycleHost       ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacPowerOnOffHost       ( MEGARAC_HANDLE handle, RCS_FN_POWER_ON_OFF_WITH_DELAY_ARGS     *pCAM );
extern MEGARAC_ERROR MegaRacResetHostDelay       ( MEGARAC_HANDLE handle, RCS_FN_RESET_HOST_AFTER_DELAY_ARGS      *pCAM );
extern MEGARAC_ERROR MegaRacPowerCycleHostDelay  ( MEGARAC_HANDLE handle, RCS_FN_POWERCYCLE_HOST_AFTER_DELAY_ARGS *pCAM );
extern MEGARAC_ERROR MegaRacPowerOnOffHostDelay  ( MEGARAC_HANDLE handle, RCS_FN_POWER_ON_OFF_WITH_DELAY_ARGS     *pCAM );
extern MEGARAC_ERROR MegaRacReportIoBaseAddress  ( MEGARAC_HANDLE handle, RCS_FN_REPORT_IO_BASE_ADDRESS_ARGS      *pCAM );
    /* group 6 */
extern MEGARAC_ERROR MegaRacEvent                ( MEGARAC_HANDLE handle, RCS_FN_LOG_EVENT_ARGS         *pCAM );
extern MEGARAC_ERROR MegaRacAlert                ( MEGARAC_HANDLE handle, RCS_FN_SEND_ALERT_ARGS        *pCAM );
    /* group 7 */
extern MEGARAC_ERROR MegaRacStartHeartbeat       ( MEGARAC_HANDLE handle, RCS_FN_START_HEARTBEAT_ARGS   *pCAM );
extern MEGARAC_ERROR MegaRacStopHeartbeat        ( MEGARAC_HANDLE handle );
    /* group 20 */
extern MEGARAC_ERROR MegaRacReadKey              ( MEGARAC_HANDLE handle, KeyData       *pCAM );
extern MEGARAC_ERROR MegaRacSendDcInfo           ( MEGARAC_HANDLE handle, DC_INFO_TYPE  *pCAM );
extern MEGARAC_ERROR MegaRacSendDcPacket         ( MEGARAC_HANDLE handle, DC_INFO_TYPE  *pCAM );
extern MEGARAC_ERROR MegaRacReadMouseEvent       ( MEGARAC_HANDLE handle, DC_INFO_TYPE  *pCAM );
extern MEGARAC_ERROR MegaRacIDoReadYou           ( MEGARAC_HANDLE handle );
    /* group 21 */
extern MEGARAC_ERROR MegaRacFileTransfer         ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacSendToRemote         ( MEGARAC_HANDLE handle, DC_INFO_TYPE  *pCAM );
extern MEGARAC_ERROR MegaRacReadFromRemote       ( MEGARAC_HANDLE handle, DC_INFO_TYPE  *pCAM );
extern MEGARAC_ERROR MegaRacReportCursorPosition ( MEGARAC_HANDLE handle, int row, int col );
    /* group 40 */
extern MEGARAC_ERROR MegaRacEnableHhfMonitor     ( MEGARAC_HANDLE handle, EN_DIS_HHF_MONITOR_TYPE   *pCAM );
extern MEGARAC_ERROR MegaRacEnableHhfThreshold   ( MEGARAC_HANDLE handle, EN_DIS_HHF_THRES_TYPE     *pCAM );
extern MEGARAC_ERROR MegaRacReadHhfThreshold     ( MEGARAC_HANDLE handle, HHF_THRES_TYPE            *pCAM );
extern MEGARAC_ERROR MegaRacModifyHhfThreshold   ( MEGARAC_HANDLE handle, HHF_THRES_TYPE            *pCAM );
extern MEGARAC_ERROR MegaRacReadHhfSchedule      ( MEGARAC_HANDLE handle, HHF_SCHED_TYPE            *pCAM );
extern MEGARAC_ERROR MegaRacModifyHhfSchedule    ( MEGARAC_HANDLE handle, HHF_SCHED_TYPE            *pCAM );
extern MEGARAC_ERROR MegaRacReadHhfMonitorState  ( MEGARAC_HANDLE handle, EN_DIS_HHF_MONITOR_TYPE   *pCAM );
extern MEGARAC_ERROR MegaRacReadHhfThresholdState( MEGARAC_HANDLE handle, EN_DIS_HHF_THRES_TYPE     *pCAM );
extern MEGARAC_ERROR MegaRacEnableSdkMonitor     ( MEGARAC_HANDLE handle, EN_DIS_SDK_MONITOR_TYPE   *pCAM );
extern MEGARAC_ERROR MegaRacEnableSdkThreshold   ( MEGARAC_HANDLE handle, EN_DIS_SDK_THRES_TYPE     *pCAM );
extern MEGARAC_ERROR MegaRacReadSdkThreshold     ( MEGARAC_HANDLE handle, SDK_THRES_TYPE            *pCAM );
extern MEGARAC_ERROR MegaRacModifySdkThreshold   ( MEGARAC_HANDLE handle, SDK_THRES_TYPE            *pCAM );
extern MEGARAC_ERROR MegaRacReadSdkSchedule      ( MEGARAC_HANDLE handle, SDK_SCHED_TYPE            *pCAM );
extern MEGARAC_ERROR MegaRacModifySdkSchedule    ( MEGARAC_HANDLE handle, SDK_SCHED_TYPE            *pCAM );
extern MEGARAC_ERROR MegaRacReadSdkMonitorState  ( MEGARAC_HANDLE handle, EN_DIS_SDK_MONITOR_TYPE   *pCAM );
extern MEGARAC_ERROR MegaRacReadSdkThresholdState( MEGARAC_HANDLE handle, EN_DIS_SDK_THRES_TYPE     *pCAM );
    /* group 41 */
extern MEGARAC_ERROR MegaRacAddAdministrator     ( MEGARAC_HANDLE handle, AdminEntry              *pCAM );
extern MEGARAC_ERROR MegaRacReadAdministrator    ( MEGARAC_HANDLE handle, AdminEntry              *pCAM );
extern MEGARAC_ERROR MegaRacDeleteAdministrator  ( MEGARAC_HANDLE handle, AdminEntry              *pCAM );
extern MEGARAC_ERROR MegaRacModifyAdministrator  ( MEGARAC_HANDLE handle, AdminEntry              *pCAM );
    /* group 42 */
extern MEGARAC_ERROR MegaRacReadNetwork          ( MEGARAC_HANDLE handle, NET_CFG_TYPE            *pCAM );
extern MEGARAC_ERROR MegaRacModifyNetwork        ( MEGARAC_HANDLE handle, NET_CFG_TYPE            *pCAM );
extern MEGARAC_ERROR MegaRacReadModem            ( MEGARAC_HANDLE handle, MODEM_CFG_TYPE          *pCAM );
extern MEGARAC_ERROR MegaRacModifyModem          ( MEGARAC_HANDLE handle, MODEM_CFG_TYPE          *pCAM );
extern MEGARAC_ERROR MegaRacAddSnmpDestination   ( MEGARAC_HANDLE handle, SNMP_DEST_TYPE          *pCAM );
extern MEGARAC_ERROR MegaRacReadSnmpDestination  ( MEGARAC_HANDLE handle, SNMP_DEST_TYPE          *pCAM );
extern MEGARAC_ERROR MegaRacModifySnmpDestination( MEGARAC_HANDLE handle, SNMP_DEST_TYPE          *pCAM );
extern MEGARAC_ERROR MegaRacDeleteSnmpDestination( MEGARAC_HANDLE handle, SNMP_DEST_TYPE          *pCAM );
extern MEGARAC_ERROR MegaRacAddManagedConsole    ( MEGARAC_HANDLE handle, MGNT_CON_TYPE           *pCAM );
extern MEGARAC_ERROR MegaRacReadManagedConsole   ( MEGARAC_HANDLE handle, MGNT_CON_TYPE           *pCAM );
extern MEGARAC_ERROR MegaRacModifyManagedConsole ( MEGARAC_HANDLE handle, MGNT_CON_TYPE           *pCAM );
extern MEGARAC_ERROR MegaRacDeleteManagedConsole ( MEGARAC_HANDLE handle, MGNT_CON_TYPE           *pCAM );
extern MEGARAC_ERROR MegaRacReadAlertTable       ( MEGARAC_HANDLE handle, ALERT_ORDER_TABLE       *pCAM );
extern MEGARAC_ERROR MegaRacModifyAlertTable     ( MEGARAC_HANDLE handle, ALERT_ORDER_TABLE       *pCAM );
extern MEGARAC_ERROR MegaRacForceSaveConfig      ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacSendTestPage         ( MEGARAC_HANDLE handle, AdminEntry              *pCAM);
    /* group 43 */
extern MEGARAC_ERROR MegaRacReadPppConfig        ( MEGARAC_HANDLE handle, PPP_CFG_TYPE                   *pCAM );
extern MEGARAC_ERROR MegaRacModifyPppConfig      ( MEGARAC_HANDLE handle, PPP_CFG_TYPE                   *pCAM );
extern MEGARAC_ERROR MegaRacReadDialOutConfig    ( MEGARAC_HANDLE handle, DIALOUT_CFG_TYPE               *pCAM );
extern MEGARAC_ERROR MegaRacModifyDialOutConfig  ( MEGARAC_HANDLE handle, DIALOUT_CFG_TYPE               *pCAM );
extern MEGARAC_ERROR MegaRacReadTcpIpConfig      ( MEGARAC_HANDLE handle, RCS_TCPIP_CFG                  *pCAM );
extern MEGARAC_ERROR MegaRacModifyTcpIpConfig    ( MEGARAC_HANDLE handle, RCS_TCPIP_CFG                  *pCAM );
extern MEGARAC_ERROR MegaRacReadDhcpConfig       ( MEGARAC_HANDLE handle, RCS_DHCP_CFG                   *pCAM );
extern MEGARAC_ERROR MegaRacModifyDhcpConfig     ( MEGARAC_HANDLE handle, RCS_DHCP_CFG                   *pCAM );
extern MEGARAC_ERROR MegaRacReadAdminMailID      ( MEGARAC_HANDLE handle, RCS_ADMIN_MAIL_ID_PKT          *pCAM );
extern MEGARAC_ERROR MegaRacModifyAdminMailID    ( MEGARAC_HANDLE handle, RCS_ADMIN_MAIL_ID_PKT          *pCAM );
extern MEGARAC_ERROR MegaRacReadMailServerIP     ( MEGARAC_HANDLE handle, RCS_ADMIN_MAIL_SERVER_IP_PKT   *pCAM );
extern MEGARAC_ERROR MegaRacModifyMailServerIP   ( MEGARAC_HANDLE handle, RCS_ADMIN_MAIL_SERVER_IP_PKT   *pCAM );
extern MEGARAC_ERROR MegaRacSendTestEmail        ( MEGARAC_HANDLE handle, RCS_TEST_EMAIL_ARGS            *pCAM );
    /* group 44 */
extern MEGARAC_ERROR MegaRacSetAdminHourMask     ( MEGARAC_HANDLE handle, RCS_ADMIN_HOURLY_MASKS_PKT     *pCAM );
extern MEGARAC_ERROR MegaRacGetAdminHourMask     ( MEGARAC_HANDLE handle, RCS_ADMIN_HOURLY_MASKS_PKT     *pCAM );
extern MEGARAC_ERROR MegaRacSetSnmpSeverityMask  ( MEGARAC_HANDLE handle, RCS_SNMP_SEVERITY_MASKS_PKT    *pCAM );
extern MEGARAC_ERROR MegaRacGetSnmpSeverityMask  ( MEGARAC_HANDLE handle, RCS_SNMP_SEVERITY_MASKS_PKT    *pCAM );
extern MEGARAC_ERROR MegaRacSetManConSeverityMask( MEGARAC_HANDLE handle, RCS_MGNTCON_SEVERITY_MASKS_PKT *pCAM );
extern MEGARAC_ERROR MegaRacGetManConSeverityMask( MEGARAC_HANDLE handle, RCS_MGNTCON_SEVERITY_MASKS_PKT *pCAM );
    /* group 45 */
extern MEGARAC_ERROR MegaRacReadMiscConfig            ( MEGARAC_HANDLE handle, MISC_CFG_TYPE             *pCAM );
extern MEGARAC_ERROR MegaRacModifyMiscConfig          ( MEGARAC_HANDLE handle, MISC_CFG_TYPE             *pCAM );
extern MEGARAC_ERROR MegaRacReadAutoRecoveryConfig    ( MEGARAC_HANDLE handle, RCS_AUTO_RECOVERY_CFG     *pCAM );
extern MEGARAC_ERROR MegaRacModifyAutoRecoveryConfig  ( MEGARAC_HANDLE handle, RCS_AUTO_RECOVERY_CFG     *pCAM );
extern MEGARAC_ERROR MegaRacReadTerminalServerConfig  ( MEGARAC_HANDLE handle, RCS_TERMINAL_SERVER_CFG   *pCAM );
extern MEGARAC_ERROR MegaRacModifyTerminalServerConfig( MEGARAC_HANDLE handle, RCS_TERMINAL_SERVER_CFG   *pCAM );
    /* group 46 */
extern MEGARAC_ERROR MegaRacResetCardSoft         ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacShutdownCard          ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacResetBatteryCharge    ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacSetDateTime           ( MEGARAC_HANDLE handle, DATE_TIME *pCAM );
extern MEGARAC_ERROR MegaRacGetDateTime           ( MEGARAC_HANDLE handle, DATE_TIME *pCAM );
extern MEGARAC_ERROR MegaRacRestoreFactoryDefaults( MEGARAC_HANDLE handle );
    /* group 47 */
extern MEGARAC_ERROR MegaRacFlashUpdate         ( MEGARAC_HANDLE handle, 
                                                          MegaRacFlashUpdateOptions     options, 
                                                          char                         *filenameImage, 
                                                          char                         *filenameSDK, 
                                                          unsigned long                *flashExtStatus,
                                                          MEGARAC_ERROR                *mrStatus );
extern MEGARAC_ERROR MegaRacFlashGetVersion     ( MEGARAC_HANDLE handle, FwVer *romVer,
                                                                                 char  *filename,
                                                                                 FwVer *fileVer );
extern MEGARAC_ERROR MegaRacFlashGetFileSize    (  char  *filename, unsigned long *fileSize );
    /* group 48 */
extern MEGARAC_ERROR MegaRacReadSnmpServerConfig ( MEGARAC_HANDLE handle, RCS_SNMPSERVER_CONFIG_PKT *pCAM );
extern MEGARAC_ERROR MegaRacModifySnmpServerConfig(MEGARAC_HANDLE handle, RCS_SNMPSERVER_CONFIG_PKT *pCAM );
    /* group 80 */
extern MEGARAC_ERROR MegaRacDiagReadByte        ( MEGARAC_HANDLE handle, RCS_DIAG_RW_BYTE_ARGS      *pCAM );
extern MEGARAC_ERROR MegaRacDiagWriteByte       ( MEGARAC_HANDLE handle, RCS_DIAG_RW_BYTE_ARGS      *pCAM );
extern MEGARAC_ERROR MegaRacDiagReadWord        ( MEGARAC_HANDLE handle, RCS_DIAG_RW_WORD_ARGS      *pCAM );
extern MEGARAC_ERROR MegaRacDiagWriteWord       ( MEGARAC_HANDLE handle, RCS_DIAG_RW_WORD_ARGS      *pCAM );
extern MEGARAC_ERROR MegaRacDiagReadDword       ( MEGARAC_HANDLE handle, RCS_DIAG_RW_DWORD_ARGS     *pCAM );
extern MEGARAC_ERROR MegaRacDiagWriteDword      ( MEGARAC_HANDLE handle, RCS_DIAG_RW_DWORD_ARGS     *pCAM );
extern MEGARAC_ERROR MegaRacDiagDumpBlock       ( MEGARAC_HANDLE handle, RCS_DIAG_DUMP_BLOCK_ARGS   *pCAM );
extern MEGARAC_ERROR MegaRacDiagScanMemory      ( MEGARAC_HANDLE handle, RCS_DIAG_SCAN_MEMORY_ARGS  *pCAM );
extern MEGARAC_ERROR MegaRacDiagForceFail       ( MEGARAC_HANDLE handle, RCS_DIAG_FORCE_FAIL_ARGS   *pCAM );
    /* group 100 */
extern MEGARAC_ERROR MegaRacDebugSwitchVideoPrint( MEGARAC_HANDLE handle, RCS_DBG_SWITCH_VIDEO_PRINT_ARGS       *pCAM );
extern MEGARAC_ERROR MegaRacDebugSetVideoMode    ( MEGARAC_HANDLE handle, RCS_DBG_SET_VIDEO_DEBUG_MODE_ARGS     *pCAM );
extern MEGARAC_ERROR MegaRacGetLatestErrorLog    ( MEGARAC_HANDLE handle, RCS_DBG_GET_LATEST_ERROR_LOG_ARGS     *pCAM );
extern MEGARAC_ERROR MegaRacGetErrorLogEntries   ( MEGARAC_HANDLE handle, RCS_DBG_GET_ERROR_LOG_ENTRIES_ARGS    *pCAM );
extern MEGARAC_ERROR MegaRacGetCheckPointHeader  ( MEGARAC_HANDLE handle, RCS_DBG_GET_CHECKPT_HEADER_ARGS       *pCAM );
extern MEGARAC_ERROR MegaRacClearCheckPointLog   ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacGetNumberOfModules   ( MEGARAC_HANDLE handle, RCS_DBG_GET_NO_OF_MODULES_ARGS        *pCAM );
extern MEGARAC_ERROR MegaRacGetModuleNames       ( MEGARAC_HANDLE handle, RCS_DBG_GET_MODULE_NAMES_ARGS         *pCAM );
extern MEGARAC_ERROR MegaRacGetDescriptiveStrings( MEGARAC_HANDLE handle, RCS_DBG_GET_DESC_STRINGS_ARGS         *pCAM );
extern MEGARAC_ERROR MegaRacDebugSetMasks        ( MEGARAC_HANDLE handle, RCS_DBG_SET_DBGPRINT_MASKS_ARGS       *pCAM );
extern MEGARAC_ERROR MegaRacClearVideoScreen     ( MEGARAC_HANDLE handle );
    /* group 800, RCS_USR */ 
extern MEGARAC_ERROR MegaRacGetEsmInfo          ( MEGARAC_HANDLE handle, GET_ESMII_INFO_DATA                    *pCAM );
extern MEGARAC_ERROR MegaRacGetEsmEventLog      ( MEGARAC_HANDLE handle, RCS_USR_GET_ESMII_EVENT_LOG_ARGS       *pCAM );
extern MEGARAC_ERROR MegaRacGetEsmPostLog       ( MEGARAC_HANDLE handle, RCS_USR_GET_ESMII_POST_LOG_ARGS        *pCAM );
extern MEGARAC_ERROR MegaRacScanEsm             ( MEGARAC_HANDLE handle, RCS_USR_SCAN_FOR_ESMIIS_ARGS           *pCAM );
extern MEGARAC_ERROR MegaRacSendEsmCommand      ( MEGARAC_HANDLE handle, RCS_USR_SEND_COMMAND_TO_ESMII_ARGS     *pCAM );
extern MEGARAC_ERROR MegaRacReadEsmConfig       ( MEGARAC_HANDLE handle, RCS_USR_CONFIG_ESMII_ARGS              *pCAM );
extern MEGARAC_ERROR MegaRacModifyEsmConfig     ( MEGARAC_HANDLE handle, RCS_USR_CONFIG_ESMII_ARGS              *pCAM );
extern MEGARAC_ERROR MegaRacDumpEsmEventLog     ( MEGARAC_HANDLE handle, RCS_USR_GET_ESMII_EVENT_LOG_ARGS       *pCAM );
extern MEGARAC_ERROR MegaRacDumpEsmPostLog      ( MEGARAC_HANDLE handle, RCS_USR_GET_ESMII_POST_LOG_ARGS        *pCAM );
extern MEGARAC_ERROR MegaRacGetNumberEsm        ( MEGARAC_HANDLE handle, RCS_USR_CONFIG_ESMII_ARGS              *pCAM );
extern MEGARAC_ERROR MegaRacGetEsmServerInfo    ( MEGARAC_HANDLE handle, RCS_USR_GET_SERVER_INFO_ARGS           *pCAM );
extern MEGARAC_ERROR MegaRacClearEsmEventLog    ( MEGARAC_HANDLE handle, RCS_USR_CLEAR_ESMII_EVENT_LOG_ARGS     *pCAM );
extern MEGARAC_ERROR MegaRacClearEsmPostLog     ( MEGARAC_HANDLE handle, RCS_USR_CLEAR_ESMII_POST_LOG_ARGS      *pCAM );
extern MEGARAC_ERROR MegaRacGetUserAlertSeverity( MEGARAC_HANDLE handle, RCS_USR_GET_GLOBAL_ALERT_SEVERITY_ARGS *pCAM );
extern MEGARAC_ERROR MegaRacReportServerInfo    ( MEGARAC_HANDLE handle, RCS_USR_REPORT_SERVER_INFO_ARGS        *pCAM );
    /* group 801, RCS_USR */
extern MEGARAC_ERROR MegaRacReportHostInfo      ( MEGARAC_HANDLE handle, RCS_USR_REPORT_HOST_INFO_ARGS           *pCAM );
extern MEGARAC_ERROR MegaRacGetHostInfo         ( MEGARAC_HANDLE handle, RCS_USR_GET_HOST_INFO_ARGS              *pCAM );
extern MEGARAC_ERROR MegaRacGetCurrentEsmPostLog( MEGARAC_HANDLE handle, RCS_USR_GET_CURRENT_ESMII_POST_LOG_ARGS *pCAM );
extern MEGARAC_ERROR MegaRacGetAllEsmPostLogs   ( MEGARAC_HANDLE handle, RCS_USR_GET_ALL_ESMII_POST_LOGS_ARGS    *pCAM );
extern MEGARAC_ERROR MegaRacClearAllEsmPostLogs ( MEGARAC_HANDLE handle );
    /* group 804, RCS_USR */
extern MEGARAC_ERROR MegaRacGetIpmiEventLog     ( MEGARAC_HANDLE handle, RCS_USR_GET_IPMI_EVENT_LOG_ARGS        *pCAM );
extern MEGARAC_ERROR MegaRacClearIpmiEventLog   ( MEGARAC_HANDLE handle, RCS_USR_CLEAR_IPMI_EVENT_LOG_ARGS      *pCAM );
extern MEGARAC_ERROR MegaRacGetSdrRecord        ( MEGARAC_HANDLE handle, RCS_USR_SDR_RECORD_ARGS                *pCAM );
extern MEGARAC_ERROR MegaRacGetFruInfo          ( MEGARAC_HANDLE handle, RCS_USR_ALT_GET_FRU_DATA_ARGS          *pCAM );
extern MEGARAC_ERROR MegaRacRescanFru           ( MEGARAC_HANDLE handle, RCS_USR_ALT_RESCAN_FRU_DATA_ARGS       *pCAM );
extern MEGARAC_ERROR MegaRacStartStopIpmiMonitor( MEGARAC_HANDLE handle, RCS_USR_ALT_START_STOP_IPMI_MON_ARGS   *pCAM );
extern MEGARAC_ERROR MegaRacDumpIpmiSdr         ( MEGARAC_HANDLE handle, RCS_USR_ALT_DUMP_SDR_RECORDS_ARGS      *pCAM );
    /* group internal to MegaRacDLL */
//#define                      MEGARAC_API_CMD_OS_RESTART     0xfff8
#define                      MEGARAC_API_CMD_WAIT_EVENTS    0xfff9 
#define                      MEGARAC_API_CMD_ATTACHMENTS    0xfffa  /* internal (static) to API */
#define                      MEGARAC_API_CMD_ISSUE_CMD      0xfffb
#define                      MEGARAC_API_CMD_GET_REG        0xfffc
#define                      MEGARAC_API_CMD_SET_EVENTS     0xfffd
#define                      MEGARAC_CMD_RESET_HARD         0xfffe
//extern MEGARAC_ERROR MegaRacOsRestart           ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacWaitEvents          ( MEGARAC_HANDLE handle, RAC_EVENT_NOTIFICATION *pCAM );
extern MEGARAC_ERROR MegaRacSetEvents           ( MEGARAC_HANDLE handle, RAC_EVENT_NOTIFICATION *pCAM );
extern MEGARAC_ERROR MegaRacResetCardHard       ( MEGARAC_HANDLE handle );
extern MEGARAC_ERROR MegaRacIsCardAlive         ( MEGARAC_HANDLE handle );
extern void          MegaRacKludgeLoop          ( int doCursor, int doFtp );

/*------------------------------------------------------
 * for MegaRacCAM.c, mostly caused by old MegaRacApi.h
 *-----------------------------------------------------*/

typedef struct  _MEGARAC_INFO {
    unsigned char   cpuDescr[30]; 
    unsigned char   cpuSpeed[8];
    unsigned char   memSize[8]; 
    unsigned char   romSize[8]; 
    unsigned char   nvramSize[8];
    unsigned char   pciBridge[30];
    unsigned char   networkController[30];
    unsigned char   modemType;
    unsigned char   pcmciaHostAdapter[30];
    unsigned char   pcCardManufacturer[30];
    unsigned char   pcCardProductName[30];
    unsigned char   battery[30];
    unsigned char   fwMajorNum;
    unsigned char   fwMinorNum;
    unsigned char   fwBuildDate[12];
    unsigned short  voltCount;
    unsigned short  tempCount;
    unsigned short  fanCount;
    unsigned short  switchCount;
    unsigned short  faultCount;
    unsigned char   ethernetAddress[18];
    MEGARAC_HANDLE  handle;     
    unsigned char   rsvd[64];
} MEGARAC_INFO;

typedef struct _MEGARAC_VARIABLE  {
    unsigned short  index;
    unsigned short  type;     
    unsigned char   descr[32];
    unsigned char   reading[32];  
    unsigned char   unit[16];
    unsigned char   limitLowAlert[32];
    unsigned char   limitHighAlert[32];
    unsigned char   limitLowWarn[32];
    unsigned char   limitHighWarn[32];
             long   rsvd[32];
} MEGARAC_VAR;

#define MEGARAC_MAX_VARIABLES  32
typedef struct _MEGARAC_VARS {
    unsigned short  count;
    MEGARAC_VAR     var[MEGARAC_MAX_VARIABLES];
} MEGARAC_VARS;

typedef struct _MEGARAC_ALERT_CONFIG  {
    unsigned char   index;
    unsigned char   sendIPAddr[4];
    unsigned char   sendCommunity[64];
    unsigned char   callbackNumber[32];
} MEGARAC_ALERT_CONFIG;

extern MEGARAC_ERROR MegaRacHhfFindCat    ( MEGARAC_HANDLE handle,                           char         *cat, unsigned char *catIndex );
extern MEGARAC_ERROR MegaRacHhfFindSub    ( MEGARAC_HANDLE handle, int             category, char         *sub, unsigned char *subIndex );
extern MEGARAC_ERROR MegaRacGetVolts      ( MEGARAC_HANDLE handle, int             count,    MEGARAC_VARS *pOut );
extern MEGARAC_ERROR MegaRacGetVolt       ( MEGARAC_HANDLE handle, int             index,    MEGARAC_VAR  *pOut);
extern MEGARAC_ERROR MegaRacSetVolt       ( MEGARAC_HANDLE handle, MEGARAC_VAR    *pIn );
extern MEGARAC_ERROR MegaRacGetTemps      ( MEGARAC_HANDLE handle, int             count,    MEGARAC_VARS *pOut );
extern MEGARAC_ERROR MegaRacGetTemp       ( MEGARAC_HANDLE handle, int             index,    MEGARAC_VAR  *pOut );
extern MEGARAC_ERROR MegaRacSetTemp       ( MEGARAC_HANDLE handle, MEGARAC_VAR    *pIn );
extern MEGARAC_ERROR MegaRacGetInformation( MEGARAC_HANDLE handle, MEGARAC_INFO         *pInfo ); 
extern MEGARAC_ERROR MegaRacGetAlertConfig( MEGARAC_HANDLE handle, MEGARAC_ALERT_CONFIG *pAlert );
extern MEGARAC_ERROR MegaRacSetAlertConfig( MEGARAC_HANDLE handle, MEGARAC_ALERT_CONFIG *pAlert );


#pragma pack()

#endif /* MEGA_RAC_DLL_H */
