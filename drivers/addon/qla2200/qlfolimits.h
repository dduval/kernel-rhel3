/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


/*
 *  Minimums, maximums, defaults, and other definitions for MC_PARAMS.
 */

#define FO_INSPECTION_INTERVAL_MIN                     0
#define FO_INSPECTION_INTERVAL_MAX               1000000
#define FO_INSPECTION_INTERVAL_DEF                   600

#define FO_MAX_PATHS_PER_DEVICE_MIN                    1
#define FO_MAX_PATHS_PER_DEVICE_MAX                    64
#define FO_MAX_PATHS_PER_DEVICE_DEF                    64

#define FO_MAX_RETRIES_PER_PATH_MIN                    1
#define FO_MAX_RETRIES_PER_PATH_MAX                    8
#define FO_MAX_RETRIES_PER_PATH_DEF                    3

#define FO_MAX_RETRIES_PER_IO_MIN          ((FO_MAX_PATHS_PER_DEVICE_MIN * FO_MAX_RETRIES_PER_PATH_MIN) + 1)
#define FO_MAX_RETRIES_PER_IO_MAX          ((FO_MAX_PATHS_PER_DEVICE_MAX * FO_MAX_RETRIES_PER_PATH_MAX) + 1)
#define FO_MAX_RETRIES_PER_IO_DEF          ((FO_MAX_PATHS_PER_DEVICE_DEF * FO_MAX_RETRIES_PER_PATH_DEF) + 1)

#define FO_DEVICE_ERROR_THRESHOLD_MIN                  1
#define FO_DEVICE_ERROR_THRESHOLD_MAX                255
#define FO_DEVICE_ERROR_THRESHOLD_DEF                  4

#define FO_DEVICE_TIMEOUT_THRESHOLD_MIN                1
#define FO_DEVICE_TIMEOUT_THRESHOLD_MAX              255
#define FO_DEVICE_TIMEOUT_THRESHOLD_DEF                4

#define FO_FRAME_ERROR_THRESHOLD_MIN                   1
#define FO_FRAME_ERROR_THRESHOLD_MAX                 255
#define FO_FRAME_ERROR_THRESHOLD_DEF                   4

#define FO_LINK_ERROR_THRESHOLD_MIN                    1
#define FO_LINK_ERROR_THRESHOLD_MAX                  255
#define FO_LINK_ERROR_THRESHOLD_DEF                    4

#define FO_ROLLING_AVERAGE_INTERVALS_MIN               1
#define FO_ROLLING_AVERAGE_INTERVALS_MAX              10
#define FO_ROLLING_AVERAGE_INTERVALS_DEF               1

#define FO_MAX_DEVICES_TO_MIGRATE_MIN                  0
#define FO_MAX_DEVICES_TO_MIGRATE_MAX                255
#define FO_MAX_DEVICES_TO_MIGRATE_DEF                  4

#define FO_BALANCE_METHOD_NONE                         0
#define FO_BALANCE_METHOD_IOS                          1
#define FO_BALANCE_METHOD_MBS                          2

#define FO_BALANCE_METHOD_MIN                      FO_BALANCE_METHOD_NONE
#define FO_BALANCE_METHOD_MAX                      FO_BALANCE_METHOD_MBS
#define FO_BALANCE_METHOD_DEF                      FO_BALANCE_METHOD_IOS

#define FO_LOAD_SHARE_MIN_PERCENTAGE_MIN              25
#define FO_LOAD_SHARE_MIN_PERCENTAGE_MAX              99
#define FO_LOAD_SHARE_MIN_PERCENTAGE_DEF              75

#define FO_LOAD_SHARE_MAX_PERCENTAGE_MIN             101
#define FO_LOAD_SHARE_MAX_PERCENTAGE_MAX             500
#define FO_LOAD_SHARE_MAX_PERCENTAGE_DEF             150

#define FO_NOTIFY_TYPE_NONE                   0
#define FO_NOTIFY_TYPE_LUN_RESET              1
#define FO_NOTIFY_TYPE_CDB                    2
#define FO_NOTIFY_TYPE_LOGOUT_OR_LUN_RESET    3
#define FO_NOTIFY_TYPE_LOGOUT_OR_CDB          4
#define FO_NOTIFY_TYPE_SPINUP		      5
#define FO_NOTIFY_TYPE_TPGROUP_CDB            6 /* Set Target Port Group Cdb */

#define FO_NOTIFY_TYPE_MIN                FO_NOTIFY_TYPE_NONE
#define FO_NOTIFY_TYPE_MAX                FO_NOTIFY_TYPE_LOGOUT_OR_CDB
#define FO_NOTIFY_TYPE_DEF                FO_NOTIFY_TYPE_NONE

#define FO_NOTIFY_CDB_LENGTH_MIN              6
#define FO_NOTIFY_CDB_LENGTH_MAX             16

