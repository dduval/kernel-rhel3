/*******************************************************************

    File Name   :   errcod.H

	Author      :   K.V.Subash & Parthiban Baskar

    Date        :   12 February 1998

    Purpose     :   Error codes returned to RCS Users


	Copyright   :   American Megatrends Inc., 1997-1998

********************************************************************/
#ifndef __ERRCOD_H__
#define __ERRCOD_H__


/*****************************SUCCESS CODE*************************/

#define  RCSERR_SUCCESS 0x0000
//ZERO is the success value usually returned

/******************************************************************/



/***************************SYSTEM ERROR GROUP*********************/
//grp codes go as follows : 0xFFXX;
/******************************************************************/


/***************************NETWORK ERROR GROUP*********************/
//grp codes go as follows : 0x00XX;
/******************************************************************/



/***************************FIRMWARE INIT GROUP*********************/
//grp codes go as follows : 0x01XX;
/******************************************************************/



/***************************FIRMWARE OPERATIONS GROUP**************/
//grp codes go as follows : 0x02XX;
#define DRACERR_LIST_FULL                               0x0201
#define DRACERR_INVALID_INDEX                           0x0202
#define DRACERR_NULL_POINTER_ARGUMENT                   0x0203
#define DRACERR_NO_RECORD                               0x0204
#define DRACERR_INVALID_LOGIN                           0x0205
#define DRACERR_REDIRECTION_ALREADY_ACTIVE              0x0206
#define DRACERR_HOST_ALIVE                              0x0207
#define DRACERR_HOST_DEAD                               0x0208
#define DRACERR_HOST_WEAK                               0x0209
#define DRACERR_HOST_STARTUP_FAILED                     0x020A
#define DRACERR_HOST_IN_STARTUP                         0x020B
#define SDKERR_MON_ABSENT                               0x020C
#define SDKERR_MON_TRY_LATER                            0x020D
#define SDKERR_MON_I2C_BUS_ERROR__LAST_VALUE_READ       0x020E
#define SDKERR_MON_TEMPORARILY_DISABLED                 0x020F
/******************************************************************/






/***************************RCS GROUP******************************/
//grp codes go as follows : 0x04XX;
#define RCSERR_LOGIN_SUCCESSFUL_NO_CALLBACK           0x0400
#define RCSERR_RESOURCES_OCCUPIED_RETRY               0x0401
#define RCSERR_INVALID_INDEX                          0x0402
#define RCSERR_BUSY                                   0x0403
#define RCSERR_INVALID_LOGIN                          0x0404
#define RCSERR_BAD_LENGTH                             0x0405
#define RCSERR_BAD_ARGUMENT                           0x0406
#define RCSERR_OUT_OF_BUFFERS__RETRY                  0x0407
#define RCSERR_REMOTE_CONNECTION_DEAD                 0x0408
#define RCSERR_DEVICE_NO_RESPONSE                     0x0409
#define RCSERR_NVRAM_UPDATE_ERROR                     0x040a
#define RCSERR_REDIRECTION_NOT_ACTIVE                 0x040b
#define RCSERR_NO_PAGERNUMBER_SPECIFIED               0X040c
#define RCSERR_NO_PAGER_TYPE                          0x040d
#define RCSERR_OS_SHUTDOWN_PENDING                    0x040e
#define RCSERR_INFORMATION_NOT_AVAILABLE              0x040f
#define RCSERR_BUFFER_TOO_LONG                        0x0410
#define RCSERR_LIST_FULL                              0x0411
#define RCSERR_NULL_POINTER_ARGUMENT                  0x0412
#define RCSERR_IP_IN_USE                              0x0413
#define RCSERR_SYSTEM_FAILED_TO_POWER_ON              0x0414
#define RCSERR_SYSTEM_FAILED_TO_POWER_OFF             0x0415
#define RCSERR_FUNCTION_NOT_SUPPORTED                 0x04ff


/******************************************************************/
#endif
