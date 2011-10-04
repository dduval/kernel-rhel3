
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#undef ENTER_TRACE
/*
* Macros use for debugging the driver.
*/
#if defined(ENTER_TRACE)
#define ENTER(x)	do { printk("qla2100 : Entering %s()\n", x); } while (0)
#define LEAVE(x)	do { printk("qla2100 : Leaving %s()\n", x);  } while (0)
#define ENTER_INTR(x)	do { printk("qla2100 : Entering %s()\n", x); } while (0)
#define LEAVE_INTR(x)	do { printk("qla2100 : Leaving %s()\n", x);  } while (0)
#else
#define ENTER(x)	do {} while (0)
#define LEAVE(x)	do {} while (0)
#define ENTER_INTR(x) 	do {} while (0)
#define LEAVE_INTR(x)   do {} while (0)
#endif

#if  QLA2100_COMTRACE
#define COMTRACE(x)     do {printk(x);} while (0);
#else
#define COMTRACE(x)	do {} while (0);
#endif

#if  DEBUG_QLA2100
#define DEBUG(x)	if (extended_error_logging != 0) { do {x;} while (0); }
#else
#define DEBUG(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_1)
#define DEBUG1(x)	do {x;} while (0);
#else
#define DEBUG1(x)	do {} while (0);
#endif

#if 1
#define DEBUG2(x)       if (extended_error_logging != 0) { do {x;} while (0); }
#define DEBUG2_3(x)     if (extended_error_logging != 0) { do {x;} while (0); }
#define DEBUG2_3_11(x)  if (extended_error_logging != 0) { do {x;} while (0); }
#define DEBUG2_9_10(x)  if (extended_error_logging != 0) { do {x;} while (0); }
#define DEBUG2_11(x)    if (extended_error_logging != 0) { do {x;} while (0); }
#define DEBUG2_13(x)    if (extended_error_logging != 0) { do {x;} while (0); }
#else
#define DEBUG2(x)	do {} while (0);
#define DEBUG2_3(x)     do {} while (0); 
#define DEBUG2_3_11(x)  do {} while (0); 
#define DEBUG2_9_10(x)  do {} while (0); 
#define DEBUG2_13(x)	do {} while (0); 
#endif

#if defined(QL_DEBUG_LEVEL_3)
#define DEBUG3(x)	do {x;} while (0);
#define DEBUG3_11(x)	do {x;} while (0);
#else
#define DEBUG3(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_4)
#define DEBUG4(x)	do {x;} while (0);
#else
#define DEBUG4(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_5)
#define DEBUG5(x)          do {x;} while (0);
#else
#define DEBUG5(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_7)
#define DEBUG7(x)          do {x;} while (0);
#else
#define DEBUG7(x)	   do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_9)
#define DEBUG9(x)       do {x;} while (0);
#define DEBUG9_10(x)    do {x;} while (0);
#else
#define DEBUG9(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_10)
#define DEBUG10(x)      do {x;} while (0);
#define DEBUG9_10(x)	do {x;} while (0);
#else
#define DEBUG10(x)	do {} while (0);
  #if !defined(DEBUG9_10)
  #define DEBUG9_10(x)	do {} while (0);
  #endif
#endif

#if defined(QL_DEBUG_LEVEL_11)
#define DEBUG11(x)      do{x;} while(0);
#if !defined(DEBUG3_11)
#define DEBUG3_11(x)    do{x;} while(0);
#endif
#else
#define DEBUG11(x)	do{} while(0);
    #if !defined(QL_DEBUG_LEVEL_3)
  #define DEBUG3_11(x)	do{} while(0);
  #endif
#endif

#if defined(QL_DEBUG_LEVEL_12)
#define DEBUG12(x)      do {x;} while (0)
#else
#define DEBUG12(x)	do {} while (0)
#endif

#if defined(QL_DEBUG_LEVEL_13)
#define DEBUG13(x)      do {x;} while (0);
  #if !defined(DEBUG2_13)
  #define DEBUG2_13(x)      do {x;} while (0);
  #endif
#else
#define DEBUG13(x)	do {} while (0);
  #if !defined(DEBUG2_13)
  #define DEBUG2_13(x)      do {} while (0);
  #endif
#endif

