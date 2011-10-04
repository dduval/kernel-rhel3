/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


/* fs/ioctl.c */
extern asmlinkage long sys_ioctl(unsigned int fd, unsigned int cmd, void *);

extern int register_ioctl32_conversion(unsigned int cmd,
    int (*handler)(unsigned int, unsigned int, unsigned long, struct file *));
extern int unregister_ioctl32_conversion(unsigned int cmd);

#if 0
static char qla2200_drvr_loaded_str[] = "qla2200_driver_loaded";
static char qla2300_drvr_loaded_str[] = "qla2300_driver_loaded";
#if defined(ISP2200)
static uint8_t qla2200_driver_loaded = 1;
#elif defined(ISP2300)
static uint8_t qla2300_driver_loaded = 1;
#endif
#endif

typedef struct _INT_LOOPBACK_REQ_32
{
	UINT16	Options;			/* 2   */
	UINT32	TransferCount;			/* 4   */
	UINT32	IterationCount;			/* 4   */
	u32	BufferAddress;			/* 4  */
	UINT32	BufferLength;			/* 4  */
	UINT16	Reserved[9];			/* 18  */
} INT_LOOPBACK_REQ_32, *PINT_LOOPBACK_REQ_32;	/* 36 */

typedef struct _INT_LOOPBACK_RSP_32
{
	u32	BufferAddress;			/* 4  */
	UINT32	BufferLength;			/* 4  */
	UINT16	CompletionStatus;		/* 2  */
	UINT16	CrcErrorCount;			/* 2  */
	UINT16	DisparityErrorCount;		/* 2  */
	UINT16	FrameLengthErrorCount;		/* 2  */
	UINT32	IterationCountLastError;	/* 4  */
	UINT8	CommandSent;			/* 1  */
	UINT8	Reserved1;			/* 1  */
	UINT16	Reserved2[7];			/* 14 */
} INT_LOOPBACK_RSP_32, *PINT_LOOPBACK_RSP_32;	/* 36 */

typedef struct {
	u32	Signature;			/* 4 chars string */
	UINT16	AddrMode;			/* 2 */
	UINT16	Version;			/* 2 */
	UINT16	SubCode;			/* 2 */
	UINT16	Instance;			/* 2 */
	UINT32	Status;				/* 4 */
	UINT32	DetailStatus;			/* 4 */
	UINT32	Reserved1;			/* 4 */
	UINT32	RequestLen;			/* 4 */
	UINT32	ResponseLen;			/* 4 */
	u32	RequestAdr;			/* 4 */
	u32	ResponseAdr;			/* 4 */
	UINT16	HbaSelect;			/* 2 */
	UINT16	VendorSpecificStatus[11];	/* 22 */
	u32	VendorSpecificData;		/* 4 */
} EXT_IOCTL_32, *PEXT_IOCTL_32;			/* 68 / 0x44 */

int
qla2x00_xfr_to_64loopback(EXT_IOCTL *pext, void **preq_32, void **prsp_32);
int
qla2x00_xfr_from_64loopback(EXT_IOCTL *pext, void **preq_32, void **prsp_32);


/************************************/
/* Start of function implementation */
/************************************/
int
qla2x00_ioctl32(unsigned int fd, unsigned int cmd, unsigned long arg,
    struct file *pfile)
{
	EXT_IOCTL_32	ext32;
	EXT_IOCTL_32	*pext32 = &ext32;
	EXT_IOCTL	ext;
	EXT_IOCTL	*pext = &ext;
	void		*preq_32 = NULL; /* request pointer */
	void		*prsp_32 = NULL; /* response pointer */

	mm_segment_t	old_fs;
	int		ret;
	int		tmp_rval;

	/* Catch any non-exioct ioctls */
	if (_IOC_TYPE(cmd) != QLMULTIPATH_MAGIC) {
		return (-EINVAL);
	}

	if (copy_from_user(pext32, (char *)arg, sizeof(EXT_IOCTL_32))) {
		KMEM_FREE(pext32, sizeof(EXT_IOCTL_32));
		return (-EFAULT);
	}

	DEBUG9(printk("%s: got hba instance %d.\n",
	    __func__, pext32->HbaSelect);)

	/* transfer values to EXT_IOCTL */
	memcpy(&pext->Signature, &pext32->Signature, sizeof(pext32->Signature));
	pext->AddrMode = pext32->AddrMode;
	pext->Version = pext32->Version;
	pext->SubCode = pext32->SubCode;
	pext->Instance = pext32->Instance;
	pext->Status = pext32->Status;
	pext->DetailStatus = pext32->DetailStatus;
	pext->Reserved1 = pext32->Reserved1;
	pext->RequestLen = pext32->RequestLen;
	pext->ResponseLen = pext32->ResponseLen;
	pext->RequestAdr = (UINT64)(u64)pext32->RequestAdr;
	pext->ResponseAdr = (UINT64)(u64)pext32->ResponseAdr;
	pext->HbaSelect = pext32->HbaSelect;
	memcpy(pext->VendorSpecificStatus, pext32->VendorSpecificStatus,
	    sizeof(pext32->VendorSpecificStatus));
	pext->VendorSpecificData = (UINT64)(u64)pext32->VendorSpecificData;

	/* transfer values for each individual command as necessary */
	switch (cmd) { /* switch on EXT IOCTL COMMAND CODE */
	case INT_CC_LOOPBACK:
		qla2x00_xfr_to_64loopback(pext, &preq_32, &prsp_32);
		break;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS); /* tell kernel to accept arg in kernel space */

	ret = sys_ioctl(fd, cmd, pext);

	set_fs(old_fs);

	/* transfer values back for each individual command as necessary */
	switch (cmd) { /* switch on EXT IOCTL COMMAND CODE */
	case INT_CC_LOOPBACK:
		qla2x00_xfr_from_64loopback(pext, &preq_32, &prsp_32);
		break;
	}

	/* transfer values back to EXT_IOCTL_32 */
	pext32->Instance = pext->Instance;
	pext32->Status = pext->Status;
	pext32->DetailStatus = pext->DetailStatus;
	pext32->Reserved1 = pext->Reserved1;
	pext32->RequestLen = pext->RequestLen;
	pext32->ResponseLen = pext->ResponseLen;
	pext32->HbaSelect = pext->HbaSelect;
	memcpy(pext32->VendorSpecificStatus, pext->VendorSpecificStatus,
	    sizeof(pext32->VendorSpecificStatus));
	pext32->VendorSpecificData = (u32)(u64)pext->VendorSpecificData;

	/* Always try to copy values back regardless what happened before. */
	tmp_rval = copy_to_user((char *)arg, pext32, sizeof(EXT_IOCTL_32));
	if (ret == 0)
		ret = tmp_rval;

	return (ret);
}

static inline int
apidev_reg_increasing_idx(uint16_t low_idx, uint16_t high_idx)
{
	int	err = 0;
	int	i;
	unsigned int cmd;

	for (i = low_idx; i <= high_idx; i++) {
		cmd = (unsigned int)QL_IOCTL_CMD(i);
		err = register_ioctl32_conversion(cmd, qla2x00_ioctl32);
		if (err) {
			DEBUG9(printk(
			    "%s: error registering cmd %x. err=%d.\n",
			    __func__, cmd, err);)
			break;
		}
		DEBUG9(printk("%s: registered cmd %x.\n", __func__, cmd);)
	}

	return (err);
}

static inline int
apidev_unreg_increasing_idx(uint16_t low_idx, uint16_t high_idx)
{
	int	err = 0;
	int	i;
	unsigned int cmd;

	for (i = low_idx; i <= high_idx; i++) {
		cmd = (unsigned int)QL_IOCTL_CMD(i);
		err = unregister_ioctl32_conversion(cmd);
		if (err) {
			DEBUG9(printk(
			    "%s: error unregistering cmd %x. err=%d.\n",
			    __func__, cmd, err);)
			break;
		}
		DEBUG9(printk("%s: unregistered cmd %x.\n", __func__, cmd);)
	}

	return (err);
}

#if 0
static inline int
apidev_other_qla_drvr_loaded(void)
{
#if defined(ISP2200)
	if (inter_module_get(qla2300_drvr_loaded_str)) {
		/* 2300 is already loaded */
		/* decrement usage count */
		inter_module_put(qla2300_drvr_loaded_str);
		DEBUG9(printk("%s: found 2300 already loaded.\n", __func__);)
		return TRUE;
	}
#elif defined(ISP2300)
	if (inter_module_get(qla2200_drvr_loaded_str)) {
		/* 2200 is already loaded */
		/* decrement usage count */
		inter_module_put(qla2200_drvr_loaded_str);
		DEBUG9(printk("%s: found 2200 already loaded.\n", __func__);)
		return TRUE;
	}
#endif
	return FALSE;
}
#endif

static inline void
apidev_init_ppc64(void)
{
	int	err;

#if 0
#if defined(ISP2200)
	inter_module_register(qla2200_drvr_loaded_str, THIS_MODULE,
	    &qla2200_driver_loaded);
#elif defined(ISP2300)
	inter_module_register(qla2300_drvr_loaded_str, THIS_MODULE,
	    &qla2300_driver_loaded);
#endif

	if (apidev_other_qla_drvr_loaded()) {
		/* ioctl registered before */
		return;
	}
#endif

	DEBUG9(printk("qla2x00: going to register ioctl32 cmds.\n");)
	err = apidev_reg_increasing_idx(EXT_DEF_LN_REG_CC_START_IDX,
	    EXT_DEF_LN_REG_CC_END_IDX);
	if (!err) {
		err = apidev_reg_increasing_idx(EXT_DEF_LN_INT_CC_START_IDX,
		    EXT_DEF_LN_INT_CC_END_IDX);
	}
	if (!err) {
		err = apidev_reg_increasing_idx(EXT_DEF_LN_ADD_CC_START_IDX,
		    EXT_DEF_LN_ADD_CC_END_IDX);
	}
	if (!err) {
		err = apidev_reg_increasing_idx(FO_CC_START_IDX, FO_CC_END_IDX);
	}
	if (!err) {
		/* Linux specific cmd codes are defined in decreasing order. */
		err = apidev_reg_increasing_idx(EXT_DEF_LN_SPC_CC_END_IDX,
		    EXT_DEF_LN_SPC_CC_START_IDX);
	}
}

static inline void
apidev_cleanup_ppc64(void)
{
	int	err;

#if 0
#if defined(ISP2200)
	inter_module_unregister(qla2200_drvr_loaded_str);
#elif defined(ISP2300)
	inter_module_unregister(qla2300_drvr_loaded_str);
#endif

	if (apidev_other_qla_drvr_loaded()) {
		/* don't unregister yet */
		return;
	}
#endif

	DEBUG9(printk("qla2x00: going to unregister ioctl32 cmds.\n");)
	err = apidev_unreg_increasing_idx(EXT_DEF_LN_REG_CC_START_IDX,
	    EXT_DEF_LN_REG_CC_END_IDX);
	if (!err) {
		err = apidev_unreg_increasing_idx(EXT_DEF_LN_INT_CC_START_IDX,
		    EXT_DEF_LN_INT_CC_END_IDX);
	}
	if (!err) {
		err = apidev_unreg_increasing_idx(EXT_DEF_LN_ADD_CC_START_IDX,
		    EXT_DEF_LN_ADD_CC_END_IDX);
	}
	if (!err) {
		err = apidev_unreg_increasing_idx(FO_CC_START_IDX,
		    FO_CC_END_IDX);
	}
	if (!err) {
		/* Linux specific cmd codes are defined in decreasing order. */
		err = apidev_unreg_increasing_idx(EXT_DEF_LN_SPC_CC_END_IDX,
		    EXT_DEF_LN_SPC_CC_START_IDX);
	}
}

int
qla2x00_xfr_to_64loopback(EXT_IOCTL *pext, void **preq_32, void **prsp_32)
{
	int status;

	INT_LOOPBACK_REQ_32 lb_req_32;
	INT_LOOPBACK_REQ_32 *plb_req_32 = &lb_req_32;
	INT_LOOPBACK_REQ    *plb_req;
	INT_LOOPBACK_RSP_32 lb_rsp_32;
	INT_LOOPBACK_RSP_32 *plb_rsp_32 = &lb_rsp_32;
	INT_LOOPBACK_RSP    *plb_rsp;

	plb_req = (UINT64)KMEM_ZALLOC(sizeof(INT_LOOPBACK_REQ), 50);
	if (plb_req == NULL) {
		/* error */
		pext->Status = EXT_STATUS_NO_MEMORY;
		printk(KERN_WARNING
		    "qla2x00: ERROR in ioctl loopback request conversion "
		    "allocation.\n");
		return QL_STATUS_ERROR;
	}
	plb_rsp = (UINT64)KMEM_ZALLOC(sizeof(INT_LOOPBACK_RSP), 51);
	if (plb_rsp == NULL) {
		/* error */
		pext->Status = EXT_STATUS_NO_MEMORY;
		printk(KERN_WARNING
		    "qla2x00: ERROR in ioctl loopback response conversion "
		    "allocation.\n");
		return QL_STATUS_ERROR;
	}

	if (pext->RequestLen != sizeof(INT_LOOPBACK_REQ_32)) {
		pext->Status = EXT_STATUS_INVALID_PARAM;
		DEBUG9_10(printk(
		    "%s: invalid RequestLen =%d.\n",
		    __func__, pext->RequestLen);)
		return QL_STATUS_ERROR;
	}

	if (pext->ResponseLen != sizeof(INT_LOOPBACK_RSP_32)) {
		pext->Status = EXT_STATUS_INVALID_PARAM;
		DEBUG9_10(printk(
		    "%s: invalid ResponseLen =%d.\n",
		    __func__, pext->ResponseLen);)
		return QL_STATUS_ERROR;
	}

	status = copy_from_user(plb_req_32, pext->RequestAdr,
	    pext->RequestLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR copy "
		    "request buffer.\n", __func__);)
		return QL_STATUS_ERROR;
	}

	status = copy_from_user(plb_rsp_32, pext->ResponseAdr,
	    pext->ResponseLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR copy "
		    "response buffer.\n", __func__);)
		return QL_STATUS_ERROR;
	}

	/* Save for later */
	*preq_32 = pext->RequestAdr;
	*prsp_32 = pext->ResponseAdr;

	/* Transfer over values */
	plb_req->Options = plb_req_32->Options;
	plb_req->TransferCount = plb_req_32->TransferCount;
	plb_req->IterationCount = plb_req_32->IterationCount;
	plb_req->BufferAddress = (UINT64)(u64)plb_req_32->BufferAddress;
	plb_req->BufferLength = plb_req_32->BufferLength;

	plb_rsp->BufferAddress = (UINT64)(u64)plb_rsp_32->BufferAddress;
	plb_rsp->BufferLength = plb_rsp_32->BufferLength;
	plb_rsp->CompletionStatus = plb_rsp_32->CompletionStatus;
	plb_rsp->CrcErrorCount = plb_rsp_32->CrcErrorCount;
	plb_rsp->DisparityErrorCount = plb_rsp_32->DisparityErrorCount;
	plb_rsp->FrameLengthErrorCount = plb_rsp_32->FrameLengthErrorCount;
	plb_rsp->IterationCountLastError = plb_rsp_32->IterationCountLastError;
	plb_rsp->CommandSent = plb_rsp_32->CommandSent;

	/* Assign new values */
	pext->RequestAdr = plb_req;
	pext->ResponseAdr = plb_rsp;
	pext->RequestLen = sizeof(INT_LOOPBACK_REQ);
	pext->ResponseLen = sizeof(INT_LOOPBACK_RSP);

	return 0;
}

int
qla2x00_xfr_from_64loopback(EXT_IOCTL *pext, void **preq_32, void **prsp_32)
{
	int status;

	INT_LOOPBACK_REQ_32 lb_req_32;
	INT_LOOPBACK_REQ_32 *plb_req_32 = &lb_req_32;
	INT_LOOPBACK_REQ    *plb_req;
	INT_LOOPBACK_RSP_32 lb_rsp_32;
	INT_LOOPBACK_RSP_32 *plb_rsp_32 = &lb_rsp_32;
	INT_LOOPBACK_RSP    *plb_rsp;

	plb_req = (INT_LOOPBACK_REQ *)pext->RequestAdr;
	plb_rsp = (INT_LOOPBACK_RSP *)pext->ResponseAdr;

	plb_req_32->Options = plb_req->Options;
	plb_req_32->TransferCount = plb_req->TransferCount;
	plb_req_32->IterationCount = plb_req->IterationCount;
	plb_req_32->BufferAddress = (u32)(u64)plb_req->BufferAddress;
	plb_req_32->BufferLength = plb_req->BufferLength;

	plb_rsp_32->BufferAddress = (u32)(u64)plb_rsp->BufferAddress;
	plb_rsp_32->BufferLength = plb_rsp->BufferLength;
	plb_rsp_32->CompletionStatus = plb_rsp->CompletionStatus;
	plb_rsp_32->CrcErrorCount = plb_rsp->CrcErrorCount;
	plb_rsp_32->DisparityErrorCount = plb_rsp->DisparityErrorCount;
	plb_rsp_32->FrameLengthErrorCount = plb_rsp->FrameLengthErrorCount;
	plb_rsp_32->IterationCountLastError = plb_rsp->IterationCountLastError;
	plb_rsp_32->CommandSent = plb_rsp->CommandSent;

	KMEM_FREE(plb_req, sizeof(INT_LOOPBACK_REQ));
	KMEM_FREE(plb_rsp, sizeof(INT_LOOPBACK_RSP));

	pext->RequestAdr = *preq_32;
	pext->ResponseAdr = *prsp_32;
	pext->RequestLen = sizeof(INT_LOOPBACK_REQ_32);
	pext->ResponseLen = sizeof(INT_LOOPBACK_RSP_32);

	status = copy_to_user(pext->RequestAdr, plb_rsp_32, pext->RequestLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR "
		    "write of request data buffer.\n", __func__);)
		return QL_STATUS_ERROR;
	}

	/* put loopback return data in user buffer */
	status = copy_to_user(pext->ResponseAdr, plb_rsp_32, pext->ResponseLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR "
		    "write of response data buffer.\n", __func__);)
		return QL_STATUS_ERROR;
	}

	return 0;
}

