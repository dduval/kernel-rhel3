/****************************************************************************
 *
 *  MegaRacDebug.h 
 *
 ****************************************************************************/
#ifndef MEGARAC_DEBUG_H_INCLUDED
#define MEGARAC_DEBUG_H_INCLUDED
  
#include "MegaRacWho.h"

#if defined(MEGARAC_DRIVER_SOLARIS) || \
    defined(MEGARAC_DRIVER_LINUX)           /* stdio makes unix kernels unhappy */
#else                                       /* ...so don't include it */
#include <stdio.h>
#include <stdarg.h>
#endif









   /****************************************************************************
    *
    *  The application should choose a unique "prefix" related to its 
    *  module name, e.g. DebugFlagDeclare(xyz,static) 
    *
    *  To make the "storage" class of the bit field empty, i.e. global,
    *         do DebugFlagDeclare(xyz,DEBUG_PRINT_STORAGE_GLOBAL) 
    *         then in other modules use DebugFlagDeclare(xyz,extern)
    *
    *  The usage of the bits in the "prefix##DebugFlags" variable
    *  is currently undefined, but for now...
    *    general users  : least significant bits (0x00000000-0x0000ffff)
    *    system reserved: most  significant bits (0x00010000-0xffff0000)  
    *
    *  Note for the 'prt' parameter which is used for printf(),
    *  the parameter must be specified within parentheses, e.g.
    *  DebugPrint( ("called from %s at line %d\n",__FILE__,__LINE__) );
    *
    *  example:
    *   #define DEBUG_PRINT      ** turn debug printing on **
    *   #include "MegaRacDebug.h"
    *   DebugFlagDeclare(xyz,static);
    *   #define XYZ_ISR 0x0001
    *   ...
    *   DebugFlagSet( xyz, ~0 ); ** activate all bits for initial testing **
    *   DebugFlagPrint( xyz, XYZ_ISR, ("in ISR: datum=%x\n",datum) );
    *
    ***************************************-***********************************/

    /* it is a little complicated but...
       if MegaRacDebug.h was already included, but now the user wants to change
       the state of DEBUG_PRINT and reload the macros, then the user does:
           #ifdef DEBUG_H_INCLUDED 
           #undef DEBUG_H_INCLUDED
           #endif
           #define DEBUG_PRINT
           #include "MegaRacDebug.h"
       and we help here by undefining the existing macros to prevent
       compiler warnings. */

#ifdef DebugFlagDeclare
    #undef  DEBUG_PRINT_STORAGE_GLOBAL
    #undef  DebugFlagDeclare
    #undef  DebugFlagSet
    #undef  DebugFlagClear
    #undef  DebugFlagIsSet
    #undef  DebugFlagPrint
    #undef  DebugFlagCode
    #undef  DebugPrint
    #undef  DebugCode
    #undef  DebugOutputFuncNT
    #undef  DebugOutputFuncNW
    #undef  DebugOutputFuncSU
    #undef  DebugOutputFuncLX
    #undef  DebugOutputFuncSol
#endif


#ifdef DEBUG_PRINT
    #if defined DEBUG_PRINT_FUNCTION
                                #define DebugPrintFunction DEBUG_PRINT_FUNCTION
    #else
                                #define DebugPrintFunction printf
    #endif
  #define DEBUG_PRINT_STORAGE_GLOBAL       /* empty storage allows global visibility */
  #define DebugFlagDeclare(prefix,storage ) storage unsigned long prefix ## DebugFlags
  #define DebugFlagSet(    prefix,bits    )      prefix ## DebugFlags |=  (bits)
  #define DebugFlagClear(  prefix,bits    )      prefix ## DebugFlags &= ~(bits)
  #define DebugFlagIsSet(  prefix,bits    )     (prefix ## DebugFlags &   (bits))
  #define DebugFlagPrint(  prefix,bits,prt) if ( prefix ## DebugFlags &   (bits) ) DebugPrintFunction prt
  #define DebugFlagCode(   prefix,bits,cod) if ( prefix ## DebugFlags &   (bits) ) cod
  #define DebugPrintf(                 prt) DebugPrintFunction prt
  #define DebugCode(                   cod) cod
  
  #define DebugOutputFuncNT(prefix,storage) /* Windows API/DLL */       \
            storage void prefix ## OutputFunc( char *fmtStr, ... )      \
            {   static unsigned long cnt;                               \
                int                len;                                 \
                char               buf[500];                            \
                va_list            ap;                                  \
                va_start         ( ap,  fmtStr );                       \
          len = vsprintf         ( buf, fmtStr, ap );                   \
                  printf         ( buf );                               \
                OutputDebugString( buf );                               \
                if ( (prefix ## DebugFlags & MEGA_DLL_LOG) ) {              \
                    int i, ii, offset;                                      \
                    char last=0, bux[99];                                   \
                    for( i=offset=0 ; i<=len ; i++ ) {                      \
                        if ( buf[i]=='\n' || (buf[i]==0 && last!='\n') ) {  \
                            last   = buf[i];                                \
                            buf[i] = 0;                                     \
                            ii=sprintf ( bux, "racAPI_" );                  \
                               _strtime( bux + ii );                        \
                               sprintf ( bux + ii + 8, "_%d", cnt++ );      \
                            WritePrivateProfileString("Tester",bux,buf+offset,"racDLL.log");\
                            offset = i + 1;                                 \
                }   }   }                                                   \
                va_end           ( ap );                                    \
            }
  #define DebugOutputFuncNW(prefix,storage) /* Netware Driver */        \
            storage void prefix ## OutputFunc( char *fmtStr, ... )      \
            {   extern unsigned long prefix ## ScreenHandle;            \
                char               buf[500];                            \
                va_list            ap;                                  \
                va_start         ( ap,  fmtStr );                       \
                vsprintf         ( buf, fmtStr, ap );                   \
                OutputToScreen   ( prefix ## ScreenHandle, buf );       \
                va_end           ( ap );                                \
                if ( (prefix ## DebugFlags & MEGA_INSIDE_ISR) == 0 )    \
                    NPA_Delay_Thread(devInfo.osInfo.npaHandle,1);       \
            }
  #define DebugOutputFuncNW_API(prefix,storage) /* Netware API */       \
            storage void prefix ## OutputFunc( char *fmtStr, ... )      \
            {   char               buf[500];                            \
                va_list            ap;                                  \
                va_start         ( ap,  fmtStr );                       \
                vsprintf         ( buf, fmtStr, ap );                   \
                  printf         ( buf );                               \
                va_end           ( ap );                                \
                delay(5);                                               \
            }
  #define DebugOutputFuncSU(prefix,storage) /* SCO Unix Driver */       \
            storage void prefix ## OutputFunc( char *fmtStr, ... )      \
            {   char               buf[500];                            \
                va_list            ap;                                  \
                va_start         ( ap,  fmtStr );                       \
                vsprintf         ( buf, fmtStr, ap );                   \
                cmn_err          ( CE_CONT, buf );                      \
                va_end           ( ap );                                \
            }
  #define DebugOutputFuncLX(prefix,storage) /* Linux Driver */          \
            storage void prefix ## OutputFunc( char *fmtStr, ... )      \
            {   char            buf[500];                               \
                const char      racText[] = KERN_ALERT "RAC: ";         \
                const int       racTextLen=sizeof(racText)-1;           \
                va_list            ap;                                  \
                va_start         ( ap,  fmtStr );                       \
                memcpy           ( buf, racText, racTextLen);           \
                vsprintf         ( buf+racTextLen, fmtStr, ap );        \
                printk           ( buf );                               \
                va_end           ( ap );                                \
            }
  #define DebugOutputFuncLX_API(prefix,storage) /* Linux API */         \
            storage void prefix ## OutputFunc( char *fmtStr, ... )      \
            {   int                len;                                 \
                char               buf[500];                            \
                va_list            ap;                                  \
                va_start         ( ap,  fmtStr );                       \
          len = vsprintf         ( buf, fmtStr, ap );                   \
                 fprintf         ( stderr, buf );                       \
                if ( (prefix ## DebugFlags & MEGA_DLL_LOG) ) {              \
                    static BOOL didOpenLog = FALSE;                         \
                    int i, offset;                                          \
                    char last=0;                                            \
                    if ( !didOpenLog ) {                                    \
                        didOpenLog = TRUE;                                  \
                        openlog( "RAC  ", LOG_PID, LOG_USER );              \
                    }                                                       \
                    for( i=offset=0 ; i<=len ; i++ ) {                      \
                        if ( buf[i]=='\n' || (buf[i]==0 && last!='\n') ) {  \
                            last   = buf[i];                                \
                            buf[i] = 0;                                     \
                            syslog( LOG_USER|LOG_INFO, buf+offset );        \
                            offset = i + 1;                                 \
                }   }   }                                                   \
                va_end           ( ap );                                    \
            }
  #define DebugOutputFuncSol(prefix,storage) /* Solaris Driver */       \
            storage void prefix ## OutputFunc( char *fmtStr, ... )      \
            {   char               buf[500];                            \
                va_list            ap;                                  \
                va_start         ( ap,  fmtStr );                       \
                vsprintf         ( buf, fmtStr, ap );                   \
                cmn_err          ( CE_CONT, buf );                      \
                va_end           ( ap );                                \
            }

#else                                       /* else '#ifdef DEBUG_PRINT' */
    #define DEBUG_PRINT_STORAGE_GLOBAL       
    #define DebugFlagDeclare(  prefix,storage )
    #define DebugFlagSet(      prefix,bits    )
    #define DebugFlagClear(    prefix,bits    )
    #define DebugFlagIsSet(    prefix,bits    ) 0   /* always FALSE */
    #define DebugFlagPrint(    prefix,bits,prt)
    #define DebugFlagCode(     prefix,bits,cod) 
    #define DebugPrintf(                   prt)
    #define DebugCode(                     cod)  
    #define DebugOutputFuncNT( prefix,storage )
    #define DebugOutputFuncNW( prefix,storage )
    #define DebugOutputFuncSU( prefix,storage )
    #define DebugOutputFuncLX( prefix,storage )
    #define DebugOutputFuncSol(prefix,storage )
    #if defined(__WATCOMC__) || defined(__SCO_VERSION__)
        #undef  DebugFlagDeclare
        #define DebugFlagDeclare( prefix,storage)   extern int dummyForCompilation
        #undef  DebugOutputFuncNW
        #define DebugOutputFuncNW(prefix,storage)   extern int dummyForCompilation;
    #endif
#endif                                      /* end '#ifdef/#else DEBUG_PRINT' */


#endif /* MEGARAC_DEBUG_H_INCLUDED */


