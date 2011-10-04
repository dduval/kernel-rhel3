
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
/*
 * This file set some defines that are required to compile the 
 * command source for 2100 module
 */
#define ISP2100

#if !defined(LINUX)
#define LINUX
#endif  /* LINUX not defined */
#if !defined(linux)
#define linux
#endif  /* linux not defined */
#if !defined(INTAPI)
#define INTAPI
#endif  /* INTAPI not defined */
/*
 * Include common setting 
 */
#include "qla_settings.h"

/*
 * Include common source 
 */
#include "qla2x00.c"
