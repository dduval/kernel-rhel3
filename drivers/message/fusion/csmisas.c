

/******************************************************************************/

#if defined(CPQ_CIM) // HP SAS IOCTL's

#ifndef TRUE
#define TRUE     (1)
#endif
#ifndef FALSE
#define FALSE    (0)
#endif

static u64 reverse_byte_order64(u64 * data64)
{
	int i;
	u64 rc;
	u8  * inWord = (u8 *)data64, * outWord = (u8 *)&rc;

	for (i=0;i<8;i++) outWord[i] = inWord[7-i];

	return rc;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Routine for the HP CSMI Sas Get Driver Info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_driver_info(unsigned long arg)
{

	CSMI_SAS_DRIVER_INFO_BUFFER *uarg = (void *) arg;
	CSMI_SAS_DRIVER_INFO_BUFFER	karg;
	MPT_ADAPTER	*ioc = NULL;
	int		iocnum;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
	      "Unable to read in csmi_sas_get_driver_info_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	/* Fill in the data and return the structure to the calling
	 * program
	 */
	memcpy( karg.Information.szName, MPT_MISCDEV_BASENAME,
	    sizeof(MPT_MISCDEV_BASENAME));
	memcpy( karg.Information.szDescription, MPT_CSMI_DESCRIPTION,
	    sizeof(MPT_CSMI_DESCRIPTION));

	karg.Information.usMajorRevision = MPT_LINUX_MAJOR_VERSION;
	karg.Information.usMinorRevision = MPT_LINUX_MINOR_VERSION;
	karg.Information.usBuildRevision = MPT_LINUX_BUILD_VERSION;
	karg.Information.usReleaseRevision = MPT_LINUX_RELEASE_VERSION;

	karg.Information.usCSMIMajorRevision = CSMI_MAJOR_REVISION;
	karg.Information.usCSMIMinorRevision = CSMI_MINOR_REVISION;

	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
		sizeof(CSMI_SAS_DRIVER_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
		   "Unable to write out csmi_sas_get_driver_info_buffer @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;
}


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI_SAS_GET_CNTLR_CONFIG command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_cntlr_config(unsigned long arg)
{

	CSMI_SAS_CNTLR_CONFIG_BUFFER *uarg = (void *) arg;
	CSMI_SAS_CNTLR_CONFIG_BUFFER	karg;
	MPT_ADAPTER	*ioc = NULL;
	int		iocnum;
	int		ii;
	unsigned int 	reg;
	u32      	l;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
	     "Unable to read in csmi_sas_get_cntlr_config_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
	      "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	/* Clear the struct before filling in data. */
	memset( &karg.Configuration, 0, sizeof(CSMI_SAS_CNTLR_CONFIG));

	/* Fill in the data and return the structure to the calling
	 * program
	 */

	/* Get Base IO and Mem Mapped Addresses. */
	for(ii=0; ii < DEVICE_COUNT_RESOURCE; ii++) {
		reg = PCI_BASE_ADDRESS_0 + (ii << 2);
		pci_read_config_dword(ioc->pcidev, reg, &l);

		if ((l & PCI_BASE_ADDRESS_SPACE) ==
		    PCI_BASE_ADDRESS_SPACE_MEMORY) {
			karg.Configuration.BaseMemoryAddress.uLowPart =
			    l & PCI_BASE_ADDRESS_MEM_MASK;
		}
		else {
			karg.Configuration.uBaseIoAddress =
			    l & PCI_BASE_ADDRESS_IO_MASK;
		}

		if ((l & (PCI_BASE_ADDRESS_SPACE |
		    PCI_BASE_ADDRESS_MEM_TYPE_MASK))
		    == (PCI_BASE_ADDRESS_SPACE_MEMORY |
		    PCI_BASE_ADDRESS_MEM_TYPE_64)) {
			pci_read_config_dword(ioc->pcidev, reg+4, &l);
			karg.Configuration.BaseMemoryAddress.uHighPart = l;
		}
		if ((l & PCI_BASE_ADDRESS_SPACE) ==
		    PCI_BASE_ADDRESS_SPACE_MEMORY) {
			break;
		}
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	karg.Configuration.uBoardID = (ioc->pcidev->subsystem_device << 16) |
	    (ioc->pcidev->subsystem_vendor);
#endif

	/* LMP The physical slot is unknown. */
	karg.Configuration.usSlotNumber = 0;
	karg.Configuration.bControllerClass = CSMI_SAS_CNTLR_CLASS_HBA;
	karg.Configuration.bIoBusType = CSMI_SAS_BUS_TYPE_PCI;
	karg.Configuration.BusAddress.PciAddress.bBusNumber =
	    ioc->pcidev->bus->number;
	karg.Configuration.BusAddress.PciAddress.bDeviceNumber =
	    PCI_SLOT(ioc->pcidev->devfn);
	karg.Configuration.BusAddress.PciAddress.bFunctionNumber =
	    PCI_FUNC(ioc->pcidev->devfn);
	karg.Configuration.BusAddress.PciAddress.bReserved = 0;

	/* LMP  NULL terminated?  */
	memcpy( &karg.Configuration.szSerialNumber,
	    ioc->BoardTracerNumber, 16 );

	karg.Configuration.usMajorRevision = ioc->facts.FWVersion.Struct.Major;
	karg.Configuration.usMinorRevision = ioc->facts.FWVersion.Struct.Minor;
	karg.Configuration.usBuildRevision = ioc->facts.FWVersion.Struct.Unit;
	karg.Configuration.usReleaseRevision = ioc->facts.FWVersion.Struct.Dev;
	karg.Configuration.usBIOSMajorRevision =
	    (ioc->biosVersion & 0xFF000000) >> 24;
	karg.Configuration.usBIOSMinorRevision =
	    (ioc->biosVersion & 0x00FF0000) >> 16;
	karg.Configuration.usBIOSBuildRevision =
	    (ioc->biosVersion & 0x0000FF00) >> 8;
	karg.Configuration.usBIOSReleaseRevision =
	    (ioc->biosVersion & 0x000000FF);
	karg.Configuration.uControllerFlags =
	    CSMI_SAS_CNTLR_SAS_HBA | CSMI_SAS_CNTLR_SAS_RAID | 
	    CSMI_SAS_CNTLR_FWD_SUPPORT | CSMI_SAS_CNTLR_FWD_ONLINE | 
	    CSMI_SAS_CNTLR_FWD_SRESET ;

	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* All Rrom entries will be zero. Skip them. */
	/* bReserved will also be zeros. */
	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
		sizeof(CSMI_SAS_DRIVER_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
		"Unable to write out csmi_sas_get_driver_info_buffer @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI Sas Get Controller Status command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_cntlr_status(unsigned long arg)
{

	CSMI_SAS_CNTLR_STATUS_BUFFER  *uarg = (void *) arg;
	MPT_ADAPTER		*ioc = NULL;
	CSMI_SAS_CNTLR_STATUS_BUFFER	karg;
	int			iocnum;
	int			rc;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
	     "Unable to read in csmi_sas_get_cntlr_status_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	/* Fill in the data and return the structure to the calling
	 * program
	 */

	rc = mpt_GetIocState(ioc, 1);
	switch (rc) {
	case MPI_IOC_STATE_OPERATIONAL:
		karg.Status.uStatus =  CSMI_SAS_CNTLR_STATUS_GOOD;
		karg.Status.uOfflineReason = 0;
		break;

	case MPI_IOC_STATE_FAULT:
		karg.Status.uStatus = CSMI_SAS_CNTLR_STATUS_FAILED;
		karg.Status.uOfflineReason = 0;
		break;

	case MPI_IOC_STATE_RESET:
	case MPI_IOC_STATE_READY:
	default:
		karg.Status.uStatus =  CSMI_SAS_CNTLR_STATUS_OFFLINE;
		karg.Status.uOfflineReason =
		    CSMI_SAS_OFFLINE_REASON_INITIALIZING;
		break;
	}

	memset(&karg.Status.bReserved, 0, 28);

	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
		sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
		    "Unable to write out csmi_sas_get_cntlr_status @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI Sas Get Phy Info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_phy_info(unsigned long arg)
{
	CSMI_SAS_PHY_INFO_BUFFER *uarg = (void *) arg;
	CSMI_SAS_PHY_INFO_BUFFER  karg;
	MPT_ADAPTER		*ioc = NULL;
	ConfigExtendedPageHeader_t  hdr;
	CONFIGPARMS		cfg;
	SasIOUnitPage0_t	*sasIoUnitPg0;
	dma_addr_t		sasIoUnitPg0_dma;
	int			sasIoUnitPg0_data_sz;
	SasPhyPage0_t		*sasPhyPg0;
	dma_addr_t		sasPhyPg0_dma;
	int			sasPhyPg0_data_sz;
	u16			protocol;
	int			iocnum;
	int			rc;
	int			ii;
	u64			SASAddress64;

	dctlprintk((": %s called.\n",__FUNCTION__));
	sasIoUnitPg0=NULL;
	sasPhyPg0=NULL;
	sasIoUnitPg0_data_sz=0;
	sasPhyPg0_data_sz=0;

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_PHY_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
		"Unable to read in csmi_sas_get_phy_info_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	/* Fill in the data and return the structure to the calling
	 * program
	 */
	memset( &karg.Information, 0, sizeof(CSMI_SAS_PHY_INFO));

	/* Issue a config request to get the number of phys
	 */
	hdr.PageVersion = MPI_SASIOUNITPAGE0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = 10;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dctlprintk((
		    ": FAILED: MPI_SASIOUNITPAGE0_PAGEVERSION: HEADER\n"));
		dctlprintk((": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto sas_get_phy_info_exit;
	}

	if (hdr.ExtPageLength == 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dctlprintk((": hdr.ExtPageLength == 0\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto sas_get_phy_info_exit;
	}

	sasIoUnitPg0_data_sz = hdr.ExtPageLength * 4;
	rc = -ENOMEM;

	sasIoUnitPg0 = (SasIOUnitPage0_t *) pci_alloc_consistent(ioc->pcidev,
	    sasIoUnitPg0_data_sz, &sasIoUnitPg0_dma);

	if (!sasIoUnitPg0) {
		dctlprintk((": pci_alloc_consistent: FAILED\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto sas_get_phy_info_exit;
	}

	memset((u8 *)sasIoUnitPg0, 0, sasIoUnitPg0_data_sz);
	cfg.physAddr = sasIoUnitPg0_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {

		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dctlprintk((
		    ": FAILED: MPI_SASIOUNITPAGE0_PAGEVERSION: PAGE\n"));
		dctlprintk((": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto sas_get_phy_info_exit;
	}


	/* Number of Phys. */
	karg.Information.bNumberOfPhys = sasIoUnitPg0->NumPhys;

	/* Fill in information for each phy. */
	for (ii = 0; ii < karg.Information.bNumberOfPhys; ii++) {

/* EDM : dump IO Unit Page 0 data*/
		dsasprintk(("---- IO UNIT PAGE 0 ------------\n"));
		dsasprintk(("Handle=0x%X\n",
		    le16_to_cpu(sasIoUnitPg0->PhyData[ii].AttachedDeviceHandle)));
		dsasprintk(("Controller Handle=0x%X\n",
		    le16_to_cpu(sasIoUnitPg0->PhyData[ii].ControllerDevHandle)));
		dsasprintk(("Port=0x%X\n",
		    sasIoUnitPg0->PhyData[ii].Port));
		dsasprintk(("Port Flags=0x%X\n",
		    sasIoUnitPg0->PhyData[ii].PortFlags));
		dsasprintk(("PHY Flags=0x%X\n",
		    sasIoUnitPg0->PhyData[ii].PhyFlags));
		dsasprintk(("Negotiated Link Rate=0x%X\n",
		    sasIoUnitPg0->PhyData[ii].NegotiatedLinkRate));
		dsasprintk(("Controller PHY Device Info=0x%X\n",
		    le32_to_cpu(sasIoUnitPg0->PhyData[ii].ControllerPhyDeviceInfo)));
		dsasprintk(("DiscoveryStatus=0x%X\n",
		    le32_to_cpu(sasIoUnitPg0->PhyData[ii].DiscoveryStatus)));
		dsasprintk(("\n"));
/* EDM : debug data */

		/* PHY stuff. */
		karg.Information.Phy[ii].bPortIdentifier =
		    sasIoUnitPg0->PhyData[ii].Port;

		/* Get the negotiated link rate for the phy. */
		switch (sasIoUnitPg0->PhyData[ii].NegotiatedLinkRate) {

		case MPI_SAS_IOUNIT0_RATE_PHY_DISABLED:
			karg.Information.Phy[ii].bNegotiatedLinkRate =
			    CSMI_SAS_PHY_DISABLED;
			break;

		case MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION:
			karg.Information.Phy[ii].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_FAILED;
			break;

		case MPI_SAS_IOUNIT0_RATE_SATA_OOB_COMPLETE:
			break;

		case MPI_SAS_IOUNIT0_RATE_1_5:
			karg.Information.Phy[ii].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_1_5_GBPS;
			break;

		case MPI_SAS_IOUNIT0_RATE_3_0:
			karg.Information.Phy[ii].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_3_0_GBPS;
			break;

		case MPI_SAS_IOUNIT0_RATE_UNKNOWN:
		default:
			karg.Information.Phy[ii].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_UNKNOWN;
			break;
		}

		if (sasIoUnitPg0->PhyData[ii].PortFlags &
		    MPI_SAS_IOUNIT0_PORT_FLAGS_DISCOVERY_IN_PROGRESS) {
			karg.Information.Phy[ii].bAutoDiscover =
			    CSMI_SAS_DISCOVER_IN_PROGRESS;
		} else {
			karg.Information.Phy[ii].bAutoDiscover =
			    CSMI_SAS_DISCOVER_COMPLETE;
		}

		/* Issue a config request to get
		 * phy information.
		 */
		hdr.PageVersion = MPI_SASPHY0_PAGEVERSION;
		hdr.ExtPageLength = 0;
		hdr.PageNumber = 0;
		hdr.Reserved1 = 0;
		hdr.Reserved2 = 0;
		hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
		hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_PHY;

		cfg.cfghdr.ehdr = &hdr;
		cfg.physAddr = -1;
		cfg.pageAddr = ii;
		cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
		cfg.dir = 0;	/* read */
		cfg.timeout = 10;

		if ((rc = mpt_config(ioc, &cfg)) != 0) {
			dctlprintk((
			    ": FAILED: MPI_SASPHY0_PAGEVERSION: HEADER\n"));
			dctlprintk((": rc=%x\n",rc));
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto sas_get_phy_info_exit;
		}

		if (hdr.ExtPageLength == 0) {
			dctlprintk((": pci_alloc_consistent: FAILED\n"));
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto sas_get_phy_info_exit;
		}

		sasPhyPg0_data_sz = hdr.ExtPageLength * 4;
		rc = -ENOMEM;

		sasPhyPg0 = (SasPhyPage0_t *) pci_alloc_consistent(
		    ioc->pcidev, sasPhyPg0_data_sz, &sasPhyPg0_dma);

		if (! sasPhyPg0) {
			dctlprintk((": pci_alloc_consistent: FAILED\n"));
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto sas_get_phy_info_exit;
		}

		memset((u8 *)sasPhyPg0, 0, sasPhyPg0_data_sz);
		cfg.physAddr = sasPhyPg0_dma;
		cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

		if ((rc = mpt_config(ioc, &cfg)) != 0) {
			dctlprintk((
			    ": FAILED: MPI_SASPHY0_PAGEVERSION: PAGE\n"));
			dctlprintk((": rc=%x\n",rc));
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			pci_free_consistent(ioc->pcidev, sasPhyPg0_data_sz,
			    (u8 *) sasPhyPg0, sasPhyPg0_dma);
			goto sas_get_phy_info_exit;
		}

		le64_to_cpus((u64 *)&sasPhyPg0->SASAddress);
		memcpy(&SASAddress64, &sasPhyPg0->SASAddress, sizeof(u64));

/* EDM : dump PHY Page 0 data*/
		dsasprintk(("---- SAS PHY PAGE 0 ------------\n"));
		dsasprintk(("Handle=0x%X\n",
		    le16_to_cpu(sasPhyPg0->AttachedDevHandle)));
		dsasprintk(("SAS Address=0x%llX\n",SASAddress64));
		dsasprintk(("Attached PHY Identifier=0x%X\n",
		    sasPhyPg0->AttachedPhyIdentifier));
		dsasprintk(("Attached Device Info=0x%X\n",
		    le32_to_cpu(sasPhyPg0->AttachedDeviceInfo)));
		dsasprintk(("Programmed Link Rate=0x%X\n",
		    sasPhyPg0->ProgrammedLinkRate));
		dsasprintk(("Hardware Link Rate=0x%X\n",
		    ioc->sasPhyInfo[ii].hwLinkRate));
		dsasprintk(("Change Count=0x%X\n",
		    sasPhyPg0->ChangeCount));
		dsasprintk(("PHY Info=0x%X\n",
		    le32_to_cpu(sasPhyPg0->PhyInfo)));
		dsasprintk(("\n"));
/* EDM : debug data */

		/* save the data */

		/* Set Max hardware link rate.
		 * This value is hard coded
		 * because the HW link rate
		 * is currently being
		 * overwritten in FW.
		 */

		/* Set Max hardware link rate. */
		switch (sasPhyPg0->HwLinkRate &
		    MPI_SAS_PHY0_PRATE_MAX_RATE_MASK) {

		case MPI_SAS_PHY0_HWRATE_MAX_RATE_1_5:
			karg.Information.Phy[ii].bMaximumLinkRate =
			    CSMI_SAS_LINK_RATE_1_5_GBPS;
			break;

		case MPI_SAS_PHY0_PRATE_MAX_RATE_3_0:
			karg.Information.Phy[ii].bMaximumLinkRate =
			    CSMI_SAS_LINK_RATE_3_0_GBPS;
			break;
		default:
			break;
		}

		/* Set Max programmed link rate. */
		switch (sasPhyPg0->ProgrammedLinkRate &
		    MPI_SAS_PHY0_PRATE_MAX_RATE_MASK) {

		case MPI_SAS_PHY0_PRATE_MAX_RATE_1_5:
			karg.Information.Phy[ii].bMaximumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_1_5_GBPS << 4);
			break;

		case MPI_SAS_PHY0_PRATE_MAX_RATE_3_0:
			karg.Information.Phy[ii].bMaximumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_3_0_GBPS << 4);
			break;
		default:
			break;
		}

		/* Set Min hardware link rate. */
		switch (sasPhyPg0->HwLinkRate &
		    MPI_SAS_PHY0_HWRATE_MIN_RATE_MASK) {

		case MPI_SAS_PHY0_HWRATE_MIN_RATE_1_5:
			karg.Information.Phy[ii].bMinimumLinkRate =
			    CSMI_SAS_LINK_RATE_1_5_GBPS;
			break;

		case MPI_SAS_PHY0_PRATE_MIN_RATE_3_0:
			karg.Information.Phy[ii].bMinimumLinkRate =
			    CSMI_SAS_LINK_RATE_3_0_GBPS;
			break;
		default:
			break;
		}

		/* Set Min programmed link rate. */
		switch (sasPhyPg0->ProgrammedLinkRate &
		    MPI_SAS_PHY0_PRATE_MIN_RATE_MASK) {

		case MPI_SAS_PHY0_PRATE_MIN_RATE_1_5:
			karg.Information.Phy[ii].bMinimumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_1_5_GBPS << 4);
			break;

		case MPI_SAS_PHY0_PRATE_MIN_RATE_3_0:
			karg.Information.Phy[ii].bMinimumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_3_0_GBPS << 4);
			break;
		default:
			break;
		}

		/* Fill in Attached Device
		 * Initiator Port Protocol.
		 * Bits 6:3
		 * More than one bit can be set.
		 */
		protocol = sasPhyPg0->AttachedDeviceInfo & 0x78;
		karg.Information.Phy[ii].Attached.bInitiatorPortProtocol = 0;
		if (protocol & MPI_SAS_DEVICE_INFO_SSP_INITIATOR)
		      karg.Information.Phy[ii].Attached.bInitiatorPortProtocol =
			    CSMI_SAS_PROTOCOL_SSP;
		if (protocol & MPI_SAS_DEVICE_INFO_STP_INITIATOR)
		     karg.Information.Phy[ii].Attached.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_STP;
		if (protocol & MPI_SAS_DEVICE_INFO_SMP_INITIATOR)
		     karg.Information.Phy[ii].Attached.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_SMP;
		if (protocol & MPI_SAS_DEVICE_INFO_SATA_HOST)
		     karg.Information.Phy[ii].Attached.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_SATA;


		/* Fill in Phy Target Port
		 * Protocol. Bits 10:7
		 * More than one bit can be set.
		 */
		protocol = sasPhyPg0->AttachedDeviceInfo & 0x780;
		karg.Information.Phy[ii].Attached.bTargetPortProtocol = 0;
		if (protocol & MPI_SAS_DEVICE_INFO_SSP_TARGET)
			karg.Information.Phy[ii].Attached.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SSP;
		if (protocol & MPI_SAS_DEVICE_INFO_STP_TARGET)
			karg.Information.Phy[ii].Attached.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_STP;
		if (protocol & MPI_SAS_DEVICE_INFO_SMP_TARGET)
			karg.Information.Phy[ii].Attached.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SMP;
		if (protocol & MPI_SAS_DEVICE_INFO_SATA_DEVICE)
			karg.Information.Phy[ii].Attached.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SATA;


		/* Fill in Attached device type */
		switch (sasIoUnitPg0->PhyData[ii].ControllerPhyDeviceInfo &
		    MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) {

		case MPI_SAS_DEVICE_INFO_NO_DEVICE:
			karg.Information.Phy[ii].Attached.bDeviceType =
			    CSMI_SAS_NO_DEVICE_ATTACHED;
			break;

		case MPI_SAS_DEVICE_INFO_END_DEVICE:
			karg.Information.Phy[ii].Attached.bDeviceType =
			    CSMI_SAS_END_DEVICE;
			break;

		case MPI_SAS_DEVICE_INFO_EDGE_EXPANDER:
			karg.Information.Phy[ii].Attached.bDeviceType =
			    CSMI_SAS_EDGE_EXPANDER_DEVICE;
			break;

		case MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER:
			karg.Information.Phy[ii].Attached.bDeviceType =
			    CSMI_SAS_FANOUT_EXPANDER_DEVICE;
			break;
		}

		/* Identify Info. */
		karg.Information.Phy[ii].Identify.bDeviceType =
		    CSMI_SAS_END_DEVICE;

		/* Fill in Phy Initiator Port Protocol. Bits 6:3
		 * More than one bit can be set, fall through cases.
		 */
		protocol = sasIoUnitPg0->PhyData[ii].ControllerPhyDeviceInfo
		    & 0x78;
		karg.Information.Phy[ii].Identify.bInitiatorPortProtocol = 0;
		if( protocol & MPI_SAS_DEVICE_INFO_SSP_INITIATOR )
		     karg.Information.Phy[ii].Identify.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_SSP;
		if( protocol & MPI_SAS_DEVICE_INFO_STP_INITIATOR )
		     karg.Information.Phy[ii].Identify.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_STP;
		if( protocol & MPI_SAS_DEVICE_INFO_SMP_INITIATOR )
		     karg.Information.Phy[ii].Identify.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_SMP;
		if( protocol & MPI_SAS_DEVICE_INFO_SATA_HOST )
		     karg.Information.Phy[ii].Identify.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_SATA;

		/* Fill in Phy Target Port Protocol. Bits 10:7
		 * More than one bit can be set, fall through cases.
		 */
		protocol = sasIoUnitPg0->PhyData[ii].ControllerPhyDeviceInfo
		    & 0x780;
		karg.Information.Phy[ii].Identify.bTargetPortProtocol = 0;
		if( protocol & MPI_SAS_DEVICE_INFO_SSP_TARGET )
			karg.Information.Phy[ii].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SSP;
		if( protocol & MPI_SAS_DEVICE_INFO_STP_TARGET )
			karg.Information.Phy[ii].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_STP;
		if( protocol & MPI_SAS_DEVICE_INFO_SMP_TARGET )
			karg.Information.Phy[ii].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SMP;
		if( protocol & MPI_SAS_DEVICE_INFO_SATA_DEVICE )
			karg.Information.Phy[ii].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SATA;


		/* Setup Identify SAS Address and Phy Identifier
		 *
		 * Get phy Sas address from device list.
		 * Search the list for the matching
		 * devHandle.
		 */

		/* Setup SAS Address for the Phy */
		SASAddress64 = reverse_byte_order64((u64 *)&ioc->sasPhyInfo[ii].SASAddress);
		memcpy(karg.Information.Phy[ii].Identify.bSASAddress,&SASAddress64,
		    sizeof(u64));

		karg.Information.Phy[ii].Identify.bPhyIdentifier = ii;

		/* Setup SAS Address for the attached device */
		SASAddress64 = reverse_byte_order64((u64 *)&sasPhyPg0->SASAddress);
		memcpy(karg.Information.Phy[ii].Attached.bSASAddress,&SASAddress64,
		    sizeof(u64));

		karg.Information.Phy[ii].Attached.bPhyIdentifier =
		    sasPhyPg0->AttachedPhyIdentifier;

		pci_free_consistent(ioc->pcidev, sasPhyPg0_data_sz,
		    (u8 *) sasPhyPg0, sasPhyPg0_dma);
	}

sas_get_phy_info_exit:

	if (sasIoUnitPg0)
		pci_free_consistent(ioc->pcidev, sasIoUnitPg0_data_sz,
		    (u8 *) sasIoUnitPg0, sasIoUnitPg0_dma);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
	    sizeof(CSMI_SAS_PHY_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
		    "Unable to write out csmi_sas_get_phy_info_buffer @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;
}


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI SAS Set PHY Info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_set_phy_info(unsigned long arg)
{
	CSMI_SAS_SET_PHY_INFO_BUFFER *uarg = (void *) arg;
	CSMI_SAS_SET_PHY_INFO_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_SET_PHY_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_set_phy_info struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		"%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

/* TODO - implement IOCTL here */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
	dctlprintk((": not implemented\n"));

// cim_set_phy_info_exit:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
				sizeof(CSMI_SAS_SET_PHY_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_set_phy_info @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;

}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI Sas Get SCSI Address command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_scsi_address(unsigned long arg)
{
	CSMI_SAS_GET_SCSI_ADDRESS_BUFFER *uarg = (void *) arg;
	CSMI_SAS_GET_SCSI_ADDRESS_BUFFER	 karg;
	MPT_ADAPTER		*ioc = NULL;
	int			iocnum;
	sas_device_info_t	*sasDevice;
	u64			SASAddress64;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg,
	    sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_get_scsi_address struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
	      "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	/* Fill in the data and return the structure to the calling
	 * program
	 */

	/* Copy the SAS address in reverse byte order. */
	SASAddress64 = reverse_byte_order64((u64 *)&karg.bSASAddress);

	/* Search the list for the matching SAS address. */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_NO_SCSI_ADDRESS;
	list_for_each_entry(sasDevice, &ioc->sasDeviceList, list) {

		/* Found the matching device. */
		if ((memcmp(&sasDevice->SASAddress,
		    &SASAddress64, sizeof(u64)) != 0))
			continue;

		karg.bPathId = sasDevice->Bus;
		karg.bTargetId = sasDevice->TargetId;
		karg.bLun = karg.bSASLun[0];
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

		if (((sasDevice->deviceInfo & 0x00000003) ==
			MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER) ||
			((sasDevice->deviceInfo & 0x00000003) ==
			 MPI_SAS_DEVICE_INFO_EDGE_EXPANDER))
			karg.IoctlHeader.ReturnCode =
			    CSMI_SAS_NOT_AN_END_DEVICE;
		break;
	}

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
	    sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to write out csmi_sas_get_scsi_address @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;

}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI Sas Get SCSI Address command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_sata_signature(unsigned long arg)
{
	CSMI_SAS_SATA_SIGNATURE_BUFFER  *uarg = (void *) arg;
	CSMI_SAS_SATA_SIGNATURE_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;
	int				rc, jj;
	ConfigExtendedPageHeader_t	hdr;
	CONFIGPARMS			cfg;
	SasPhyPage0_t			*sasPhyPg0;
	dma_addr_t			sasPhyPg0_dma;
	int				sasPhyPg0_data_sz;
	SasDevicePage1_t		*sasDevicePg1;
	dma_addr_t			sasDevicePg1_dma;
	int				sasDevicePg1_data_sz;
	u8				phyId;

	dctlprintk((": %s called.\n",__FUNCTION__));
	sasPhyPg0=NULL;
	sasPhyPg0_data_sz=0;
	sasDevicePg1=NULL;
	sasDevicePg1_data_sz=0;

	if (copy_from_user(&karg, uarg,
	     sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_sata_signature struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
	    "%s::%s() @%d - ioc%d not found!\n",
		     __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	phyId = karg.Signature.bPhyIdentifier;
	if (phyId >= ioc->numPhys) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_PHY_DOES_NOT_EXIST;
		dctlprintk((": phyId >= ioc->numPhys\n"));
		goto cim_sata_signature_exit;
	}

	/* Default to success.*/
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Issue a config request to get the devHandle of the attached device
	 */

	/* Issue a config request to get phy information. */
	hdr.PageVersion = MPI_SASPHY0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_PHY;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = phyId;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = 10;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dctlprintk((": FAILED: MPI_SASPHY0_PAGEVERSION: HEADER\n"));
		dctlprintk((": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	if (hdr.ExtPageLength == 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dctlprintk((": hdr.ExtPageLength == 0\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}


	sasPhyPg0_data_sz = hdr.ExtPageLength * 4;
	rc = -ENOMEM;

	sasPhyPg0 = (SasPhyPage0_t *) pci_alloc_consistent(ioc->pcidev,
	    sasPhyPg0_data_sz, &sasPhyPg0_dma);

	if (! sasPhyPg0) {
		dctlprintk((": pci_alloc_consistent: FAILED\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	memset((u8 *)sasPhyPg0, 0, sasPhyPg0_data_sz);
	cfg.physAddr = sasPhyPg0_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dctlprintk((": FAILED: MPI_SASPHY0_PAGEVERSION: PAGE\n"));
		dctlprintk((": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	/* Make sure a SATA device is attached. */
	if ((sasPhyPg0->AttachedDeviceInfo &
	    MPI_SAS_DEVICE_INFO_SATA_DEVICE) == 0) {
		dctlprintk((": NOT A SATA DEVICE\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_NO_SATA_DEVICE;
		goto cim_sata_signature_exit;
	}

	/* Get device page 1 for FIS  signature. */
	hdr.PageVersion = MPI_SASDEVICE1_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 1 /* page number 1 */;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;

	cfg.pageAddr = ((MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
	    MPI_SAS_DEVICE_PGAD_FORM_SHIFT) |
	    sasPhyPg0->AttachedDevHandle);
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = 10;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		dctlprintk((": FAILED: MPI_SASDEVICE1_PAGEVERSION: HEADER\n"));
		dctlprintk((": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	if (hdr.ExtPageLength == 0) {
		dctlprintk((": hdr.ExtPageLength == 0\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	sasDevicePg1_data_sz = hdr.ExtPageLength * 4;
	rc = -ENOMEM;

	sasDevicePg1 = (SasDevicePage1_t *) pci_alloc_consistent
	    (ioc->pcidev, sasDevicePg1_data_sz, &sasDevicePg1_dma);

	if (! sasDevicePg1) {
		dctlprintk((": pci_alloc_consistent: FAILED\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	memset((u8 *)sasDevicePg1, 0, sasDevicePg1_data_sz);
	cfg.physAddr = sasDevicePg1_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		dctlprintk((": FAILED: MPI_SASDEVICE1_PAGEVERSION: PAGE\n"));
		dctlprintk((": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

/* EDM : dump Device Page 1 data*/
	dsasprintk(("---- SAS DEVICE PAGE 1 ---------\n"));
	dsasprintk(("Handle=0x%x\n",sasDevicePg1->DevHandle));
	dsasprintk(("SAS Address="));
	for(jj=0;jj<8;jj++)
		dsasprintk(("%02x ",
		((u8 *)&sasDevicePg1->SASAddress)[jj]));
	dsasprintk(("\n"));
	dsasprintk(("Target ID=0x%x\n",sasDevicePg1->TargetID));
	dsasprintk(("Bus=0x%x\n",sasDevicePg1->Bus));
	dsasprintk(("Initial Reg Device FIS="));
	for(jj=0;jj<20;jj++)
		dsasprintk(("%02x ",
		((u8 *)&sasDevicePg1->InitialRegDeviceFIS)[jj]));
	dsasprintk(("\n\n"));
/* EDM : debug data */

	memcpy(karg.Signature.bSignatureFIS,
		sasDevicePg1->InitialRegDeviceFIS,20);

cim_sata_signature_exit:

	if (sasPhyPg0)
		pci_free_consistent(ioc->pcidev, sasPhyPg0_data_sz,
		    (u8 *) sasPhyPg0, sasPhyPg0_dma);

	if (sasDevicePg1)
		pci_free_consistent(ioc->pcidev, sasDevicePg1_data_sz,
		    (u8 *) sasDevicePg1, sasDevicePg1_dma);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
	    sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to write out csmi_sas_get_device_address @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;
}


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI Sas Get SCSI Address command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_device_address(unsigned long arg)
{
	CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER *uarg = (void *) arg;
	CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER	 karg;
	MPT_ADAPTER		*ioc = NULL;
	int			iocnum;
	sas_device_info_t	*sasDevice;
	u64			SASAddress64;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg,
	    sizeof(CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
	   "Unable to read in csmi_sas_get_device_address_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
	    "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	/* Fill in the data and return the structure to the calling
	 * program
	 */

	/* Search the list for the matching SAS address. */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_NO_DEVICE_ADDRESS;
	list_for_each_entry(sasDevice, &ioc->sasDeviceList, list) {

		/* Find the matching device. */
		if ((karg.bPathId == sasDevice->Bus) &&
			(karg.bTargetId == sasDevice->TargetId)) {

			SASAddress64 = reverse_byte_order64(&sasDevice->SASAddress);
			memcpy(&karg.bSASAddress,&SASAddress64,sizeof(u64));
			karg.bSASLun[0] = karg.bLun;
			memset(karg.bSASLun, 0, sizeof(karg.bSASLun));
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
			break;
		} else
			/* Keep looking. */
			continue;
	}

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
	    sizeof(CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		"Unable to write out csmi_sas_get_device_address_buffer @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;

}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI Sas Get Link Errors command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_link_errors(unsigned long arg)
{
	CSMI_SAS_LINK_ERRORS_BUFFER *uarg = (void *) arg;
	CSMI_SAS_LINK_ERRORS_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t			*mpi_hdr;
	int				iocnum;
	int				rc;
	ConfigExtendedPageHeader_t	hdr;
	CONFIGPARMS			cfg;
	SasPhyPage1_t			*sasPhyPage1;
	dma_addr_t			sasPhyPage1_dma;
	int				sasPhyPage1_data_sz;
	SasIoUnitControlRequest_t	*sasIoUnitCntrReq;
	SasIoUnitControlReply_t		*sasIoUnitCntrReply;
	u8				phyId;

	dctlprintk((": %s called.\n",__FUNCTION__));
	sasPhyPage1=NULL;
	sasPhyPage1_data_sz=0;

	if (copy_from_user(&karg, uarg,
	     sizeof(CSMI_SAS_LINK_ERRORS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in mptctl_csmi_sas_get_link_errors struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
	    "%s::%s() @%d - ioc%d not found!\n",
		     __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	phyId = karg.Information.bPhyIdentifier;
	if (phyId >= ioc->numPhys) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_PHY_DOES_NOT_EXIST;
		dctlprintk((": phyId >= ioc->numPhys\n"));
		goto cim_get_link_errors_exit;
	}

	/* Default to success.*/
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Issue a config request to get the devHandle of the attached device
	 */

	/* Issue a config request to get phy information. */
	hdr.PageVersion = MPI_SASPHY1_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 1 /* page number 1*/;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_PHY;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = phyId;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = 10;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dctlprintk((": FAILED: MPI_SASPHY1_PAGEVERSION: HEADER\n"));
		dctlprintk((": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_link_errors_exit;
	}

	if (hdr.ExtPageLength == 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dctlprintk((": hdr.ExtPageLength == 0\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_link_errors_exit;
	}


	sasPhyPage1_data_sz = hdr.ExtPageLength * 4;
	rc = -ENOMEM;

	sasPhyPage1 = (SasPhyPage1_t *) pci_alloc_consistent(ioc->pcidev,
	    sasPhyPage1_data_sz, &sasPhyPage1_dma);

	if (! sasPhyPage1) {
		dctlprintk((": pci_alloc_consistent: FAILED\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_link_errors_exit;
	}

	memset((u8 *)sasPhyPage1, 0, sasPhyPage1_data_sz);
	cfg.physAddr = sasPhyPage1_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dctlprintk((": FAILED: MPI_SASPHY1_PAGEVERSION: PAGE\n"));
		dctlprintk((": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_link_errors_exit;
	}

/* EDM : dump PHY Page 1 data*/
	dsasprintk(("---- SAS PHY PAGE 1 ------------\n"));
	dsasprintk(("Invalid Dword Count=0x%x\n",
	    sasPhyPage1->InvalidDwordCount));
	dsasprintk(("Running Disparity Error Count=0x%x\n",
	    sasPhyPage1->RunningDisparityErrorCount));
	dsasprintk(("Loss Dword Synch Count=0x%x\n",
	    sasPhyPage1->LossDwordSynchCount));
	dsasprintk(("PHY Reset Problem Count=0x%x\n",
	    sasPhyPage1->PhyResetProblemCount));
	dsasprintk(("\n\n"));
/* EDM : debug data */

	karg.Information.uInvalidDwordCount =
		sasPhyPage1->InvalidDwordCount;
	karg.Information.uRunningDisparityErrorCount =
		sasPhyPage1->RunningDisparityErrorCount;
	karg.Information.uLossOfDwordSyncCount =
		sasPhyPage1->LossDwordSynchCount;
	karg.Information.uPhyResetProblemCount =
		sasPhyPage1->PhyResetProblemCount;

	if (karg.Information.bResetCounts ==
	    CSMI_SAS_LINK_ERROR_DONT_RESET_COUNTS ) {
		goto cim_get_link_errors_exit;
	}

	/* Clear Error log
	 *
	 * Issue IOUNIT Control Reqeust Message
	 */

	/* Get a MF for this command.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dctlprintk((": no msg frames!\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_link_errors_exit;
        }

	mpi_hdr = (MPIHeader_t *) mf;
	sasIoUnitCntrReq = (SasIoUnitControlRequest_t *)mf;
	memset(sasIoUnitCntrReq,0,sizeof(SasIoUnitControlRequest_t));
	sasIoUnitCntrReq->Function = MPI_FUNCTION_SAS_IO_UNIT_CONTROL;
	sasIoUnitCntrReq->MsgContext =
	    cpu_to_le32(le32_to_cpu(mpi_hdr->MsgContext));
	sasIoUnitCntrReq->PhyNum = phyId;
	sasIoUnitCntrReq->Operation = MPI_SAS_OP_PHY_CLEAR_ERROR_LOG;


	ioc->ioctl->timer.expires =
	    jiffies + HZ*MPT_IOCTL_DEFAULT_TIMEOUT /* 10 sec */;
	ioc->ioctl->wait_done = 0;
	ioc->ioctl->status |= MPT_IOCTL_STATUS_TIMER_ACTIVE;
	add_timer(&ioc->ioctl->timer);
	mpt_put_msg_frame(mptctl_id, ioc, mf);
	wait_event(mptctl_wait, ioc->ioctl->wait_done);

	/* process the completed Reply Message Frame */
	if (ioc->ioctl->status & MPT_IOCTL_STATUS_RF_VALID) {

		sasIoUnitCntrReply =
		    (SasIoUnitControlReply_t *)ioc->ioctl->ReplyFrame;

		if ( sasIoUnitCntrReply->IOCStatus != MPI_IOCSTATUS_SUCCESS) {
			dctlprintk((": SAS IO Unit Control: "));
			dctlprintk(("IOCStatus=0x%X IOCLogInfo=0x%X\n",
			    sasIoUnitCntrReply->IOCStatus,
			    sasIoUnitCntrReply->IOCLogInfo));
		}
	}

cim_get_link_errors_exit:

	ioc->ioctl->status &= ~(MPT_IOCTL_STATUS_TM_FAILED |
	    MPT_IOCTL_STATUS_COMMAND_GOOD | MPT_IOCTL_STATUS_SENSE_VALID |
	    MPT_IOCTL_STATUS_RF_VALID);

	if (sasPhyPage1)
		pci_free_consistent(ioc->pcidev, sasPhyPage1_data_sz,
		    (u8 *) sasPhyPage1, sasPhyPage1_dma);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
	    sizeof(CSMI_SAS_LINK_ERRORS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to write out mptctl_csmi_sas_get_link_errors @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;

}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI SAS SMP Passthru command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_smp_passthru(unsigned long arg)
{
	CSMI_SAS_SMP_PASSTHRU_BUFFER *uarg = (void *) arg;
	MPT_ADAPTER			*ioc;
	CSMI_SAS_SMP_PASSTHRU_BUFFER	 karg;
	pSmpPassthroughRequest_t	smpReq;
	pSmpPassthroughReply_t		smpReply;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t			*mpi_hdr;
	char				*psge;
	int				iocnum, flagsLength;
	u8				index;
	void *				request_data;
	dma_addr_t			request_data_dma;
	u32				request_data_sz;
	void *				response_data;
	dma_addr_t			response_data_dma;
	u32				response_data_sz;
	u16				ioc_stat;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_SMP_PASSTHRU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_smp_passthru struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	request_data = NULL;
	response_data = NULL;
	response_data_sz = sizeof(CSMI_SAS_SMP_RESPONSE);
	request_data_sz  = karg.Parameters.uRequestLength;

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		"%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	/* Make sure the adapter is not being reset. */
	if (!ioc->ioctl) {
		printk(KERN_ERR "%s@%d::%s - "
		    "No memory available during driver init.\n",
		    __FILE__, __LINE__,__FUNCTION__);
		return -ENOMEM;
	} else if (ioc->ioctl->status & MPT_IOCTL_STATUS_DID_IOCRESET) {
		printk(KERN_ERR "%s@%d::%s - "
		    "Busy with IOC Reset \n",
		    __FILE__, __LINE__,__FUNCTION__);
		return -EBUSY;
	}

	/* Default to success.*/
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Do some error checking on the request. */
	if (karg.Parameters.bPortIdentifier == CSMI_SAS_IGNORE_PORT) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_SELECT_PHY_OR_PORT;
		goto cim_smp_passthru_exit;
	}

	if ((karg.Parameters.uRequestLength > 0xFFFF) ||
	    (!karg.Parameters.uRequestLength)) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_smp_passthru_exit;
	}

	/* Get a free request frame and save the message context.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dctlprintk((": no msg frames!\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_smp_passthru_exit;
        }

	mpi_hdr = (MPIHeader_t *) mf;
	smpReq = (pSmpPassthroughRequest_t ) mf;

	memset(smpReq,0,ioc->req_sz);

	/* Fill in smp request. */
	smpReq->PhysicalPort = karg.Parameters.bPortIdentifier;
	smpReq->Function = MPI_FUNCTION_SMP_PASSTHROUGH;
	smpReq->RequestDataLength =(U16)karg.Parameters.uRequestLength;
	smpReq->ConnectionRate = karg.Parameters.bConnectionRate;
	smpReq->MsgContext =
	    cpu_to_le32(le32_to_cpu(mpi_hdr->MsgContext));
	for ( index = 0; index < 8; index++ ) {
		((u8*)&smpReq->SASAddress)[7 - index] =
		    karg.Parameters.bDestinationSASAddress[index];
	}
	smpReq->Reserved2 = 0;
	smpReq->Reserved3 = 0;

	/*
	 * Prepare the necessary pointers to run
	 * through the SGL generation
	 */

	psge = (char *)&smpReq->SGL;

	/* setup the *Request* payload SGE */
	flagsLength = MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		MPI_SGE_FLAGS_SYSTEM_ADDRESS |
		MPI_SGE_FLAGS_32_BIT_ADDRESSING |
		MPI_SGE_FLAGS_HOST_TO_IOC |
		MPI_SGE_FLAGS_END_OF_BUFFER;

	if (sizeof(dma_addr_t) == sizeof(u64)) {
		flagsLength |= MPI_SGE_FLAGS_64_BIT_ADDRESSING;
	}
	flagsLength = flagsLength << MPI_SGE_FLAGS_SHIFT;
	flagsLength |= request_data_sz;

	request_data = pci_alloc_consistent(
	    ioc->pcidev, request_data_sz, &request_data_dma);

	if (!request_data) {
		dctlprintk((": pci_alloc_consistent: FAILED\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		mpt_free_msg_frame(ioc, mf);
		goto cim_smp_passthru_exit;
	}

	mpt_add_sge(psge, flagsLength, request_data_dma);
	psge += (sizeof(u32) + sizeof(dma_addr_t));

	memcpy(request_data,&karg.Parameters.Request,request_data_sz);

	/* setup the *Response* payload SGE */
	response_data = pci_alloc_consistent(
	    ioc->pcidev, response_data_sz, &response_data_dma);

	if (!response_data) {
		dctlprintk((": pci_alloc_consistent: FAILED\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		mpt_free_msg_frame(ioc, mf);
		goto cim_smp_passthru_exit;
	}

	flagsLength = MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		MPI_SGE_FLAGS_SYSTEM_ADDRESS |
		MPI_SGE_FLAGS_32_BIT_ADDRESSING |
		MPI_SGE_FLAGS_IOC_TO_HOST |
		MPI_SGE_FLAGS_END_OF_BUFFER;

	if (sizeof(dma_addr_t) == sizeof(u64)) {
		flagsLength |= MPI_SGE_FLAGS_64_BIT_ADDRESSING;
	}

	flagsLength = flagsLength << MPI_SGE_FLAGS_SHIFT;
	flagsLength |= response_data_sz;

	mpt_add_sge(psge, flagsLength, response_data_dma);

	/* The request is complete. Set the timer parameters
	 * and issue the request.
	 */
	ioc->ioctl->timer.expires = jiffies + HZ*MPT_IOCTL_DEFAULT_TIMEOUT;
	ioc->ioctl->wait_done = 0;
	ioc->ioctl->status |= MPT_IOCTL_STATUS_TIMER_ACTIVE;
	add_timer(&ioc->ioctl->timer);
	mpt_put_msg_frame(mptctl_id, ioc, mf);
	wait_event(mptctl_wait, ioc->ioctl->wait_done);

	if ((ioc->ioctl->status & MPT_IOCTL_STATUS_RF_VALID) == 0) {
		dctlprintk((": SMP Passthru: oh no, there is no reply!!"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_smp_passthru_exit;
	}

	/* process the completed Reply Message Frame */
	smpReply = (pSmpPassthroughReply_t )ioc->ioctl->ReplyFrame;
	ioc_stat = smpReply->IOCStatus & MPI_IOCSTATUS_MASK;

	if ((ioc_stat != MPI_IOCSTATUS_SUCCESS) &&
	    (ioc_stat != MPI_IOCSTATUS_SCSI_DATA_UNDERRUN)) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		dctlprintk((": SMP Passthru: "));
		dctlprintk(("IOCStatus=0x%X IOCLogInfo=0x%X SASStatus=0x%X\n",
		    smpReply->IOCStatus,
		    smpReply->IOCLogInfo,
		    smpReply->SASStatus));
		goto cim_smp_passthru_exit;
	}

	karg.Parameters.bConnectionStatus =
	    map_sas_status_to_csmi(smpReply->SASStatus);


	if (smpReply->ResponseDataLength) {
		karg.Parameters.uResponseBytes = smpReply->ResponseDataLength;
		memcpy(&karg.Parameters.Response,
		    response_data, smpReply->ResponseDataLength);
	}

cim_smp_passthru_exit:

	ioc->ioctl->status &= ~( MPT_IOCTL_STATUS_TM_FAILED |
	    MPT_IOCTL_STATUS_COMMAND_GOOD | MPT_IOCTL_STATUS_SENSE_VALID |
	    MPT_IOCTL_STATUS_RF_VALID);

	if (request_data)
		pci_free_consistent(ioc->pcidev, request_data_sz,
		    (u8 *)request_data, request_data_dma);

	if (response_data)
		pci_free_consistent(ioc->pcidev, response_data_sz,
		    (u8 *)response_data, response_data_dma);


	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
				sizeof(CSMI_SAS_SMP_PASSTHRU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_smp_passthru @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;

}


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI SAS SSP Passthru command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int mptctl_csmi_sas_ssp_passthru(unsigned long arg)
{
	CSMI_SAS_SSP_PASSTHRU_BUFFER *uarg = (void *) arg;
	CSMI_SAS_SSP_PASSTHRU_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	pSCSIIORequest_t		pScsiRequest;
	pSCSIIOReply_t			pScsiReply;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t 			*mpi_hdr;
	int				iocnum,ii;
	u32				data_sz;
	u64				SASAddress64;
	sas_device_info_t		*sasDevice;
	u16				req_idx;
	char				*psge;
	int				flagsLength;
	void *				request_data;
	dma_addr_t			request_data_dma;
	u32				request_data_sz;
	u8				found;
	u8				bus=0, target=0;
	u16				ioc_stat;


	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_SSP_PASSTHRU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_ssp_passthru struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	request_data=NULL;
	request_data_sz=0;

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		"%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s()"
		    " @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	/* Default to success.
	 */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Do some error checking on the request.
	 */
	request_data_sz = karg.Parameters.uDataLength;

	/* Neither a phy nor a port has been selected.
	 */
	if ((karg.Parameters.bPhyIdentifier == CSMI_SAS_USE_PORT_IDENTIFIER) &&
		(karg.Parameters.bPortIdentifier == CSMI_SAS_IGNORE_PORT)) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_SELECT_PHY_OR_PORT;
		dctlprintk((KERN_ERR
		    "%s::%s()"
		    " @%d - incorrect bPhyIdentifier and bPortIdentifier!\n",
		    __FILE__, __FUNCTION__, __LINE__));
		goto cim_ssp_passthru_exit;
	}

	/* A phy has been selected. Verify that it's valid.
	 */
	if (karg.Parameters.bPortIdentifier == CSMI_SAS_IGNORE_PORT) {

		/* Is the phy in range? */
		if (karg.Parameters.bPhyIdentifier >= ioc->numPhys) {
			karg.IoctlHeader.ReturnCode =
			    CSMI_SAS_PHY_DOES_NOT_EXIST;
			goto cim_ssp_passthru_exit;
		}
	}

	/* some checks of the incoming frame
	 */
	if (karg.Parameters.uDataLength > 0xFFFF) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		dctlprintk((KERN_ERR
		    "%s::%s()"
		    " @%d - uDataLength > 0xFFFF!\n",
		    __FILE__, __FUNCTION__, __LINE__));
		goto cim_ssp_passthru_exit;
	}

	data_sz = sizeof(CSMI_SAS_SSP_PASSTHRU_BUFFER) -
	    sizeof(IOCTL_HEADER) - sizeof(u8*) +
	    karg.Parameters.uDataLength;

	if ( data_sz > karg.IoctlHeader.Length ) {
		karg.IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		dctlprintk((KERN_ERR
		    "%s::%s()"
		    " @%d - expected datalen incorrect!\n",
		    __FILE__, __FUNCTION__, __LINE__));
		goto cim_ssp_passthru_exit;
	}

	/* we will use SAS address to resolve the scsi adddressing
	 */
	memcpy(&SASAddress64,karg.Parameters.bDestinationSASAddress,
	    sizeof(u64));
	SASAddress64 = reverse_byte_order64(&SASAddress64);

	/* Search the list for the matching SAS address.
	 */
	found = FALSE;
	list_for_each_entry(sasDevice, &ioc->sasDeviceList, list) {

		/* Find the matching device.
		 */
		if (sasDevice->SASAddress != SASAddress64)
			continue;
		
		found = TRUE;
		bus = sasDevice->Bus;
		target = sasDevice->TargetId;;
		break;
	}

	/* Invalid SAS address
	 */
	if (found == FALSE) {
		karg.IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		dctlprintk((KERN_ERR
		    "%s::%s()"
		    " @%d - couldn't find associated SASAddress!\n",
		    __FILE__, __FUNCTION__, __LINE__));
		goto cim_ssp_passthru_exit;
	}

	if(karg.Parameters.bAdditionalCDBLength) {
	/* TODO - SCSI IO (32) Request Message support
	 */
		dctlprintk((": greater than 16-byte cdb is not supported!\n"));
		karg.IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto cim_ssp_passthru_exit;
	}

	/* Get a free request frame and save the message context.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dctlprintk((": no msg frames!\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_ssp_passthru_exit;
        }

	mpi_hdr = (MPIHeader_t *) mf;
	pScsiRequest = (pSCSIIORequest_t) mf;
	req_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);

	memset(pScsiRequest,0,sizeof(SCSIIORequest_t));

	/* Fill in SCSI IO (16) request.
	 */
	pScsiRequest->TargetID = target;
	pScsiRequest->Bus = bus;
	memcpy(pScsiRequest->LUN,karg.Parameters.bLun,8);
	pScsiRequest->Function = MPI_FUNCTION_SCSI_IO_REQUEST;
	pScsiRequest->CDBLength = karg.Parameters.bCDBLength;
	pScsiRequest->DataLength = karg.Parameters.uDataLength;
	pScsiRequest->MsgContext = mpi_hdr->MsgContext;
	memcpy(pScsiRequest->CDB,karg.Parameters.bCDB,
	    pScsiRequest->CDBLength);

	/* direction
	 */
	if (karg.Parameters.uFlags & CSMI_SAS_SSP_READ) {
		pScsiRequest->Control = MPI_SCSIIO_CONTROL_READ;
	} else if (karg.Parameters.uFlags & CSMI_SAS_SSP_WRITE) {
		pScsiRequest->Control = MPI_SCSIIO_CONTROL_WRITE;
	} else if ((karg.Parameters.uFlags & CSMI_SAS_SSP_UNSPECIFIED) &&
	    (!karg.Parameters.uDataLength)) {
		/* no data transfer
		 */
		pScsiRequest->Control = MPI_SCSIIO_CONTROL_NODATATRANSFER;
	} else {
		/* no direction specified
		 */
		pScsiRequest->Control = MPI_SCSIIO_CONTROL_READ;
		pScsiRequest->MsgFlags =
		    MPI_SCSIIO_MSGFLGS_CMD_DETERMINES_DATA_DIR;
	}

	/* task attributes
	 */
	if((karg.Parameters.uFlags && 0xFF) == 0) {
		pScsiRequest->Control |= MPI_SCSIIO_CONTROL_SIMPLEQ;
	} else if (karg.Parameters.uFlags &
	    CSMI_SAS_SSP_TASK_ATTRIBUTE_HEAD_OF_QUEUE) {
		pScsiRequest->Control |= MPI_SCSIIO_CONTROL_HEADOFQ;
	} else if (karg.Parameters.uFlags &
	    CSMI_SAS_SSP_TASK_ATTRIBUTE_ORDERED) {
		pScsiRequest->Control |= MPI_SCSIIO_CONTROL_ORDEREDQ;
	} else if (karg.Parameters.uFlags &
	    CSMI_SAS_SSP_TASK_ATTRIBUTE_ACA) {
		pScsiRequest->Control |= MPI_SCSIIO_CONTROL_ACAQ;
	} else {
		pScsiRequest->Control |= MPI_SCSIIO_CONTROL_UNTAGGED;
	}

	/* setup sense
	 */
	pScsiRequest->SenseBufferLength = MPT_SENSE_BUFFER_SIZE;
	pScsiRequest->SenseBufferLowAddr = cpu_to_le32(ioc->sense_buf_low_dma +
	    (req_idx * MPT_SENSE_BUFFER_ALLOC));

	/* setup databuffer sg, assuming we fit everything one contiguous buffer
	 */
	psge = (char *)&pScsiRequest->SGL;

	if (karg.Parameters.uFlags & CSMI_SAS_SSP_WRITE) {
		flagsLength = MPT_SGE_FLAGS_SSIMPLE_WRITE;
	} else if (karg.Parameters.uFlags & CSMI_SAS_SSP_READ) {
		flagsLength = MPT_SGE_FLAGS_SSIMPLE_READ;
	}else {
		flagsLength = ( MPI_SGE_FLAGS_SIMPLE_ELEMENT |
				MPI_SGE_FLAGS_DIRECTION |
				mpt_addr_size() )
				<< MPI_SGE_FLAGS_SHIFT;
	}
	flagsLength |= request_data_sz;
	request_data = pci_alloc_consistent(
	    ioc->pcidev, request_data_sz, &request_data_dma);

	if (request_data == NULL) {
		dctlprintk((": pci_alloc_consistent: FAILED\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		mpt_free_msg_frame(ioc, mf);
		goto cim_ssp_passthru_exit;
	}

	mpt_add_sge(psge, flagsLength, request_data_dma);

	if (karg.Parameters.uFlags & CSMI_SAS_SSP_WRITE) {

		if (copy_from_user(request_data,
		    karg.bDataBuffer,
		    request_data_sz)) {
			printk(KERN_ERR
			"%s@%d::%s - Unable "
			    "to read user data "
			    "struct @ %p\n",
			    __FILE__, __LINE__,__FUNCTION__,
			    (void*)karg.bDataBuffer);
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			mpt_free_msg_frame(ioc, mf);
			goto cim_ssp_passthru_exit;
		}
	}

	/* The request is complete. Set the timer parameters
	 * and issue the request.
	 */
	ioc->ioctl->timer.expires = jiffies + HZ*MPT_IOCTL_DEFAULT_TIMEOUT;
	ioc->ioctl->wait_done = 0;
	ioc->ioctl->status |= MPT_IOCTL_STATUS_TIMER_ACTIVE;
	add_timer(&ioc->ioctl->timer);
	mpt_put_msg_frame(mptctl_id, ioc, mf);
	wait_event(mptctl_wait, ioc->ioctl->wait_done);

	memset(&karg.Status,0,sizeof(CSMI_SAS_SSP_PASSTHRU_STATUS));
	karg.Status.bConnectionStatus = CSMI_SAS_OPEN_ACCEPT;
	karg.Status.bDataPresent = CSMI_SAS_SSP_NO_DATA_PRESENT;
	karg.Status.bStatus = GOOD;
	karg.Status.bResponseLength[0] = 0;
	karg.Status.bResponseLength[1] = 0;
	karg.Status.uDataBytes = karg.Parameters.uDataLength;

	/* process the completed Reply Message Frame */
	if (ioc->ioctl->status & MPT_IOCTL_STATUS_RF_VALID) {

		pScsiReply = (pSCSIIOReply_t ) ioc->ioctl->ReplyFrame;
		karg.Status.bStatus = pScsiReply->SCSIStatus;
		karg.Status.uDataBytes = pScsiReply->TransferCount;
		ioc_stat = pScsiReply->IOCStatus & MPI_IOCSTATUS_MASK;

		if (pScsiReply->SCSIState ==
		    MPI_SCSI_STATE_AUTOSENSE_VALID) {
			karg.Status.bConnectionStatus =
			    CSMI_SAS_SSP_SENSE_DATA_PRESENT;
			karg.Status.bResponseLength[0] =
				(u8)pScsiReply->SenseCount & 0xFF;
			memcpy(karg.Status.bResponse,
			    ioc->ioctl->sense, pScsiReply->SenseCount);
		} else if(pScsiReply->SCSIState ==
		    MPI_SCSI_STATE_RESPONSE_INFO_VALID) {
			karg.Status.bDataPresent =
			    CSMI_SAS_SSP_RESPONSE_DATA_PRESENT;
			karg.Status.bResponseLength[0] =
				sizeof(pScsiReply->ResponseInfo);
			for (ii=0;ii<sizeof(pScsiReply->ResponseInfo);ii++) {
				karg.Status.bResponse[ii] =
				((u8*)&pScsiReply->ResponseInfo)[
				    (sizeof(pScsiReply->ResponseInfo)-1)-ii];
			}
		} else if ((ioc_stat != MPI_IOCSTATUS_SUCCESS) &&
		    (ioc_stat !=  MPI_IOCSTATUS_SCSI_RECOVERED_ERROR) &&
		    (ioc_stat != MPI_IOCSTATUS_SCSI_DATA_UNDERRUN)) {
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			dctlprintk((": SCSI IO : "));
			dctlprintk(("IOCStatus=0x%X IOCLogInfo=0x%X\n",
			    pScsiReply->IOCStatus,
			    pScsiReply->IOCLogInfo));
		}
	}

	if ((karg.Status.uDataBytes) && (request_data) &&
	    (karg.Parameters.uFlags & CSMI_SAS_SSP_READ)) {
		if (copy_to_user((char *)uarg->bDataBuffer,
		    request_data, karg.Status.uDataBytes)) {
			printk(KERN_ERR "%s@%d::%s - "
			    "Unable to write data to user %p\n",
			    __FILE__, __LINE__,__FUNCTION__,
			    (void*)karg.bDataBuffer);
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		}
	}

cim_ssp_passthru_exit:

	ioc->ioctl->status &= ~(  MPT_IOCTL_STATUS_TM_FAILED |
	    MPT_IOCTL_STATUS_COMMAND_GOOD | MPT_IOCTL_STATUS_SENSE_VALID |
	    MPT_IOCTL_STATUS_RF_VALID);

	if (request_data)
		pci_free_consistent(ioc->pcidev, request_data_sz,
		    (u8 *)request_data, request_data_dma);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
	    offsetof(CSMI_SAS_SSP_PASSTHRU_BUFFER,bDataBuffer))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_ssp_passthru @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI SAS STP Passthru command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_stp_passthru(unsigned long arg)
{
	CSMI_SAS_STP_PASSTHRU_BUFFER *uarg = (void *) arg;
	CSMI_SAS_STP_PASSTHRU_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	pSataPassthroughRequest_t  	pSataRequest;
	pSataPassthroughReply_t		pSataReply;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t 			*mpi_hdr;
	int				iocnum;
	u32				data_sz;
	u64				SASAddress64;
	sas_device_info_t		*sasDevice=NULL;
	u16				req_idx;
	char				*psge;
	int				flagsLength;
	void *				request_data;
	dma_addr_t			request_data_dma;
	u32				request_data_sz;
	u8				found;
	u8				bus=0, target=0;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_STP_PASSTHRU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	request_data=NULL;
	request_data_sz=0;

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		"%s::%s @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	/* Default to success.
	 */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Do some error checking on the request.
	 */
	request_data_sz = karg.Parameters.uDataLength;

	/* Neither a phy nor a port has been selected.
	 */
	if ((karg.Parameters.bPhyIdentifier == CSMI_SAS_USE_PORT_IDENTIFIER) &&
		(karg.Parameters.bPortIdentifier == CSMI_SAS_IGNORE_PORT)) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_SELECT_PHY_OR_PORT;
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - incorrect bPhyIdentifier and bPortIdentifier!\n",
		    __FILE__,__FUNCTION__, __LINE__));
		goto cim_stp_passthru_exit;
	}

	/* A phy has been selected. Verify that it's valid.
	 */
	if (karg.Parameters.bPortIdentifier == CSMI_SAS_IGNORE_PORT) {

		/* Is the phy in range? */
		if (karg.Parameters.bPhyIdentifier >= ioc->numPhys) {
			karg.IoctlHeader.ReturnCode =
			    CSMI_SAS_PHY_DOES_NOT_EXIST;
			goto cim_stp_passthru_exit;
		}
	}

	/* some checks of the incoming frame
	 */
	if (karg.Parameters.uDataLength > 0xFFFF) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - uDataLength > 0xFFFF!\n",
		    __FILE__, __FUNCTION__, __LINE__));
		goto cim_stp_passthru_exit;
	}

	data_sz = sizeof(CSMI_SAS_STP_PASSTHRU_BUFFER) -
	    sizeof(IOCTL_HEADER) - sizeof(u8*) +
	    karg.Parameters.uDataLength;

	if ( data_sz > karg.IoctlHeader.Length ) {
		karg.IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - expected datalen incorrect!\n",
		    __FILE__, __FUNCTION__,__LINE__));
		goto cim_stp_passthru_exit;
	}

	/* we will use SAS address to resolve the scsi adddressing
	 */
	memcpy(&SASAddress64,karg.Parameters.bDestinationSASAddress,
	    sizeof(u64));
	SASAddress64 = reverse_byte_order64(&SASAddress64);

	/* Search the list for the matching SAS address.
	 */
	found = FALSE;
	list_for_each_entry(sasDevice, &ioc->sasDeviceList, list) {

		/* Find the matching device.
		 */
		if (sasDevice->SASAddress != SASAddress64)
			continue;

		found = TRUE;
		bus = sasDevice->Bus;
		target = sasDevice->TargetId;;
		break;
	}

	/* Invalid SAS address
	 */
	if (found == FALSE) {
		karg.IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - couldn't find associated SASAddress!\n",
		    __FILE__, __FUNCTION__, __LINE__));
		goto cim_stp_passthru_exit;
	}

	/* check that this is an STP or SATA target device
	 */
	if ( !(sasDevice->deviceInfo & MPI_SAS_DEVICE_INFO_STP_TARGET ) &&
	     !(sasDevice->deviceInfo & MPI_SAS_DEVICE_INFO_SATA_DEVICE )) {
		karg.IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto cim_stp_passthru_exit;
	}

	/* Get a free request frame and save the message context.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dctlprintk((": no msg frames!\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_stp_passthru_exit;
        }

	mpi_hdr = (MPIHeader_t *) mf;
	pSataRequest = (pSataPassthroughRequest_t) mf;
	req_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);

	memset(pSataRequest,0,sizeof(pSataPassthroughRequest_t));

	pSataRequest->TargetID = target;
	pSataRequest->Bus = bus;
	pSataRequest->Function = MPI_FUNCTION_SATA_PASSTHROUGH;
	pSataRequest->PassthroughFlags = (u16)karg.Parameters.uFlags;
	pSataRequest->ConnectionRate = karg.Parameters.bConnectionRate;
	pSataRequest->MsgContext = mpi_hdr->MsgContext;
	pSataRequest->DataLength = request_data_sz;
	pSataRequest->MsgFlags = 0;
	memcpy( pSataRequest->CommandFIS,karg.Parameters.bCommandFIS, 20);

	psge = (char *)&pSataRequest->SGL;
	if (karg.Parameters.uFlags & CSMI_SAS_STP_WRITE) {
		flagsLength = MPT_SGE_FLAGS_SSIMPLE_WRITE;
	} else if (karg.Parameters.uFlags & CSMI_SAS_STP_READ) {
		flagsLength = MPT_SGE_FLAGS_SSIMPLE_READ;
	}else {
		flagsLength = ( MPI_SGE_FLAGS_SIMPLE_ELEMENT |
				MPI_SGE_FLAGS_DIRECTION |
				mpt_addr_size() )
				<< MPI_SGE_FLAGS_SHIFT;
	}

	flagsLength |= request_data_sz;
	request_data = pci_alloc_consistent(
	    ioc->pcidev, request_data_sz, &request_data_dma);

	if (request_data == NULL) {
		dctlprintk((": pci_alloc_consistent: FAILED\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		mpt_free_msg_frame(ioc, mf);
		goto cim_stp_passthru_exit;
	}

	mpt_add_sge(psge, flagsLength, request_data_dma);

	if (karg.Parameters.uFlags & CSMI_SAS_STP_WRITE) {
		if (copy_from_user(request_data,
		    karg.bDataBuffer,
		    request_data_sz)) {
			printk(KERN_ERR
			    "%s::%s() @%d - Unable to read user data "
			    "struct @ %p\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    (void*)karg.bDataBuffer);
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			mpt_free_msg_frame(ioc, mf);
			goto cim_stp_passthru_exit;
		}
	}

	/* The request is complete. Set the timer parameters
	 * and issue the request.
	 */
	ioc->ioctl->timer.expires = jiffies + HZ*MPT_IOCTL_DEFAULT_TIMEOUT;
	ioc->ioctl->wait_done = 0;
	ioc->ioctl->status |= MPT_IOCTL_STATUS_TIMER_ACTIVE;
	add_timer(&ioc->ioctl->timer);
	mpt_put_msg_frame(mptctl_id, ioc, mf);
	wait_event(mptctl_wait, ioc->ioctl->wait_done);

	memset(&karg.Status,0,sizeof(CSMI_SAS_STP_PASSTHRU_STATUS));

	if ((ioc->ioctl->status & MPT_IOCTL_STATUS_RF_VALID) == 0) {
		dctlprintk((": STP Passthru: oh no, there is no reply!!"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_stp_passthru_exit;
	}

	/* process the completed Reply Message Frame */
	pSataReply = (pSataPassthroughReply_t ) ioc->ioctl->ReplyFrame;

	if ((pSataReply->IOCStatus != MPI_IOCSTATUS_SUCCESS) &&
	    (pSataReply->IOCStatus != MPI_IOCSTATUS_SCSI_DATA_UNDERRUN )) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		dctlprintk((": STP Passthru: "));
		dctlprintk(("IOCStatus=0x%X IOCLogInfo=0x%X SASStatus=0x%X\n",
		    pSataReply->IOCStatus,
		    pSataReply->IOCLogInfo,
		    pSataReply->SASStatus));
	}

	karg.Status.bConnectionStatus =
	    map_sas_status_to_csmi(pSataReply->SASStatus);

	memcpy(karg.Status.bStatusFIS,pSataReply->StatusFIS, 20);

	/*
	 * for now, just zero out uSCR array,
	 * then copy the one dword returned
	 * in the reply frame into uSCR[0]
	 */
	memset( karg.Status.uSCR, 0, 64);
	karg.Status.uSCR[0] = pSataReply->StatusControlRegisters;

	if((pSataReply->TransferCount) && (request_data) &&
	    (karg.Parameters.uFlags & CSMI_SAS_STP_READ)) {
		karg.Status.uDataBytes = pSataReply->TransferCount;
		if (copy_to_user((char *)uarg->bDataBuffer,
		    request_data, karg.Status.uDataBytes)) {
			printk(KERN_ERR "%s::%s() @%d - "
			    "Unable to write data to user %p\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    (void*)karg.bDataBuffer);
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		}
	}

cim_stp_passthru_exit:

	ioc->ioctl->status &= ~( MPT_IOCTL_STATUS_TM_FAILED |
	    MPT_IOCTL_STATUS_COMMAND_GOOD | MPT_IOCTL_STATUS_SENSE_VALID |
	    MPT_IOCTL_STATUS_RF_VALID );

	if (request_data)
		pci_free_consistent(ioc->pcidev, request_data_sz,
		    (u8 *)request_data, request_data_dma);


	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
	    offsetof(CSMI_SAS_STP_PASSTHRU_BUFFER,bDataBuffer))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_ssp_passthru @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;

}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI SAS Firmware Download command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_firmware_download(unsigned long arg)
{
	CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER *uarg = (void *) arg;
	CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;
	pMpiFwHeader_t			pFwHeader=NULL;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg,
		sizeof(CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_firmware_download struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		"%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	/* Default to success.*/
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
	karg.Information.usStatus = CSMI_SAS_FWD_SUCCESS;
	karg.Information.usSeverity = CSMI_SAS_FWD_INFORMATION;

	/* some checks of the incoming frame */
	if ((karg.Information.uBufferLength +
	    sizeof(CSMI_SAS_FIRMWARE_DOWNLOAD)) >
	    karg.IoctlHeader.Length) {
		karg.IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		karg.Information.usStatus = CSMI_SAS_FWD_FAILED;
		goto cim_firmware_download_exit;
	}

	if ( karg.Information.uDownloadFlags &
	    (CSMI_SAS_FWD_SOFT_RESET | CSMI_SAS_FWD_VALIDATE)) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		karg.Information.usStatus = CSMI_SAS_FWD_REJECT;
		karg.Information.usSeverity = CSMI_SAS_FWD_ERROR;
		goto cim_firmware_download_exit;
	}

	/* now we need to alloc memory so we can pull in the
	 * fw image attached to end of incomming packet.
	 */
	pFwHeader = kmalloc(karg.Information.uBufferLength, GFP_KERNEL);
	if(pFwHeader==NULL){
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		karg.Information.usStatus = CSMI_SAS_FWD_REJECT;
		karg.Information.usSeverity = CSMI_SAS_FWD_ERROR;
		goto cim_firmware_download_exit;
	}

	if (copy_from_user(pFwHeader, uarg->bDataBuffer,
		karg.Information.uBufferLength)) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in pFwHeader @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, (void*)uarg);
		return -EFAULT;
	}

	if ( !((pFwHeader->Signature0 == MPI_FW_HEADER_SIGNATURE_0) &&
	    (pFwHeader->Signature1 == MPI_FW_HEADER_SIGNATURE_1) &&
	    (pFwHeader->Signature2 == MPI_FW_HEADER_SIGNATURE_2))) {
		// the signature check failed
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		karg.Information.usStatus = CSMI_SAS_FWD_REJECT;
		karg.Information.usSeverity = CSMI_SAS_FWD_ERROR;
		goto cim_firmware_download_exit;
	}

	if ( mptctl_do_fw_download(karg.IoctlHeader.IOControllerNumber,
	    uarg->bDataBuffer, karg.Information.uBufferLength)
	    != 0) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		karg.Information.usStatus = CSMI_SAS_FWD_FAILED;
		karg.Information.usSeverity = CSMI_SAS_FWD_FATAL;
		goto cim_firmware_download_exit;
	}

	if((karg.Information.uDownloadFlags & CSMI_SAS_FWD_SOFT_RESET) ||
	    (karg.Information.uDownloadFlags & CSMI_SAS_FWD_HARD_RESET)) {
		if (mpt_HardResetHandler(ioc, CAN_SLEEP) != 0) {
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			karg.Information.usStatus = CSMI_SAS_FWD_FAILED;
			karg.Information.usSeverity = CSMI_SAS_FWD_FATAL;
		}
	}

cim_firmware_download_exit:

	if(pFwHeader)
		kfree(pFwHeader);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
				sizeof(CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_firmware_download @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;
}


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI SAS Get RAID Info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_raid_info(unsigned long arg)
{
	CSMI_SAS_RAID_INFO_BUFFER *uarg =  (void *) arg;
	CSMI_SAS_RAID_INFO_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_RAID_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_get_raid_info struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		"%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}


	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
	if( !mpt_findImVolumes(ioc)) {
		if ( ioc->spi_data.pIocPg2 ) {
			karg.Information.uNumRaidSets = ioc->spi_data.pIocPg2->NumActiveVolumes;
			karg.Information.uMaxDrivesPerSet = ioc->spi_data.pIocPg2->MaxPhysDisks;
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
		}
	}

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
				sizeof(CSMI_SAS_RAID_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_get_raid_info @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;

}


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*	mptscsih_do_raid - Format and Issue a RAID volume request message.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@action: What do be done.
 *	@PhysDiskNum: Logical target id.
 *	@VolumeBus: Target locations bus.
 *	@VolumeId: Volume id
 *
 *	Returns: < 0 on a fatal error
 *		0 on success
 *
 *	Remark: Wait to return until reply processed by the ISR.
 */
static int
mptctl_do_raid(MPT_ADAPTER *ioc, u8 action, u8 PhysDiskNum, u8 VolumeBus, u8 VolumeId, pMpiRaidActionReply_t reply)
{
	MpiRaidActionRequest_t	*pReq;
	MpiRaidActionReply_t	*pReply;
	MPT_FRAME_HDR		*mf;
	
	/* Get and Populate a free Frame
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dctlprintk((": no msg frames!\n"));
		return -EAGAIN;
	}
	pReq = (MpiRaidActionRequest_t *)mf;
	pReq->Action = action;
	pReq->Reserved1 = 0;
	pReq->ChainOffset = 0;
	pReq->Function = MPI_FUNCTION_RAID_ACTION;
	pReq->VolumeID = VolumeId;
	pReq->VolumeBus = VolumeBus;
	pReq->PhysDiskNum = PhysDiskNum;
	pReq->MsgFlags = 0;
	pReq->Reserved2 = 0;
	pReq->ActionDataWord = 0; /* Reserved for this action */
	//pReq->ActionDataSGE = 0;

	mpt_add_sge((char *)&pReq->ActionDataSGE,
		MPT_SGE_FLAGS_SSIMPLE_READ | 0, (dma_addr_t) -1);

	ioc->ioctl->tmPtr = mf;
	ioc->ioctl->timer.expires = jiffies + HZ*MPT_IOCTL_DEFAULT_TIMEOUT;
	ioc->ioctl->wait_done = 0;
	ioc->ioctl->status |= MPT_IOCTL_STATUS_TIMER_ACTIVE;
	add_timer(&ioc->ioctl->timer);
	mpt_put_msg_frame(mptctl_id, ioc, mf);
	wait_event(mptctl_wait, ioc->ioctl->wait_done);
	
	if ((ioc->ioctl->status & MPT_IOCTL_STATUS_RF_VALID) && 
	    (reply != NULL)){
		pReply = (MpiRaidActionReply_t *)&(ioc->ioctl->ReplyFrame);
		memcpy(reply, pReply,
			min(ioc->reply_sz,
			4*pReply->MsgLength));
	}
	
	ioc->ioctl->status &= ~MPT_IOCTL_STATUS_TIMER_ACTIVE; 
	ioc->ioctl->tmPtr = NULL;
	mpt_free_msg_frame(ioc, mf);
			
	return 0;
}


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI SAS Get RAID Config command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_raid_config(unsigned long arg)
{
	CSMI_SAS_RAID_CONFIG_BUFFER *uarg = (void *) arg;
	CSMI_SAS_RAID_CONFIG_BUFFER	 karg,*pKarg=NULL;
	CONFIGPARMS		 	cfg;
	ConfigPageHeader_t	 	header;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;
	u8				volumeID, VolumeBus, physDiskNum, physDiskNumMax, found;
	int			 	volumepage0sz = 0, physdiskpage0sz = 0;
	dma_addr_t			volume0_dma, physdisk0_dma;
	pRaidVolumePage0_t		pVolume0 = NULL; 
	pRaidPhysDiskPage0_t		pPhysDisk0 = NULL;
	pMpiRaidActionReply_t 		pRaidActionReply = NULL;
	int 				i, csmi_sas_raid_config_buffer_sz;
	sas_device_info_t		*sasDevice;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(IOCTL_HEADER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_get_raid_config struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	csmi_sas_raid_config_buffer_sz = karg.IoctlHeader.Length;
	pKarg = kmalloc(csmi_sas_raid_config_buffer_sz, GFP_KERNEL);
	if(!pKarg){
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to malloc @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__,pKarg);
		return -EFAULT;
	}

	if (copy_from_user(pKarg, uarg, csmi_sas_raid_config_buffer_sz)) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_get_raid_config struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		kfree(pKarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(pKarg->IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		"%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		kfree(pKarg);
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		kfree(pKarg);
		return -ENODEV;
	}

	if(( mpt_findImVolumes(ioc) != 0 ) || ( ioc->spi_data.pIocPg2 == NULL)) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	// check to see if the input uRaidSetIndex is greater than the number of RAID sets
	if(pKarg->Configuration.uRaidSetIndex >
	    ioc->spi_data.pIocPg2->NumActiveVolumes) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_RAID_SET_OUT_OF_RANGE;
		goto cim_get_raid_config_exit;
	}

	/*
	 * get RAID Volume Page 0
	 */
	volumeID = ioc->spi_data.pIocPg2->RaidVolume[pKarg->Configuration.uRaidSetIndex-1].VolumeID;
	VolumeBus = ioc->spi_data.pIocPg2->RaidVolume[pKarg->Configuration.uRaidSetIndex-1].VolumeBus;

	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 0;
	header.PageType = MPI_CONFIG_PAGETYPE_RAID_VOLUME;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = volumeID;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = 0;
	if (mpt_config(ioc, &cfg) != 0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	if (header.PageLength == 0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	volumepage0sz = header.PageLength * 4;
	pVolume0 = pci_alloc_consistent(ioc->pcidev, volumepage0sz,
	    &volume0_dma);
	if (!pVolume0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	cfg.physAddr = volume0_dma;
	if (mpt_config(ioc, &cfg) != 0){
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	pKarg->Configuration.uCapacity = 
		(le32_to_cpu(pVolume0->MaxLBA)+1)/2048;
	pKarg->Configuration.uStripeSize = 
		le32_to_cpu(pVolume0->StripeSize/2);

	switch(pVolume0->VolumeType) {
	case MPI_RAID_VOL_TYPE_IS:
		pKarg->Configuration.bRaidType = CSMI_SAS_RAID_TYPE_0;
		break;
	case MPI_RAID_VOL_TYPE_IME:
		pKarg->Configuration.bRaidType = CSMI_SAS_RAID_TYPE_10;
		break;
	case MPI_RAID_VOL_TYPE_IM:
		pKarg->Configuration.bRaidType = CSMI_SAS_RAID_TYPE_1;
		break;
	default:
		pKarg->Configuration.bRaidType = CSMI_SAS_RAID_TYPE_OTHER;
		break;
	}

	pKarg->Configuration.bInformation = 0;
	switch (pVolume0->VolumeStatus.State) {
	case MPI_RAIDVOL0_STATUS_STATE_OPTIMAL:
		pKarg->Configuration.bStatus = CSMI_SAS_RAID_SET_STATUS_OK;
		break;
	case MPI_RAIDVOL0_STATUS_STATE_DEGRADED:
		pKarg->Configuration.bStatus = CSMI_SAS_RAID_SET_STATUS_DEGRADED;
		break;
	case MPI_RAIDVOL0_STATUS_STATE_FAILED:
		pKarg->Configuration.bStatus = CSMI_SAS_RAID_SET_STATUS_FAILED;
		break;
	}

	if(pVolume0->VolumeStatus.Flags &
	    MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS ) {

		uint64_t 	* ptrUint64;
		uint64_t	totalBlocks64, blocksRemaining64;
		uint32_t	totalBlocks32, blocksRemaining32;

		pKarg->Configuration.bStatus =
		    CSMI_SAS_RAID_SET_STATUS_REBUILDING;

		/* get percentage complete */
		pRaidActionReply = kmalloc( sizeof(MPI_RAID_VOL_INDICATOR) +
		    offsetof(MSG_RAID_ACTION_REPLY,ActionData),
		    GFP_KERNEL);

		if(pRaidActionReply == NULL){
			printk(KERN_ERR "%s@%d::%s() - "
			    "Unable to malloc @ %p\n",
			    __FILE__, __LINE__, __FUNCTION__,pKarg);
			goto cim_get_raid_config_exit;
		}

		mptctl_do_raid(ioc,
		    MPI_RAID_ACTION_INDICATOR_STRUCT,
		    0, VolumeBus, volumeID, pRaidActionReply);

		ptrUint64       = (uint64_t *)&pRaidActionReply->ActionData;
		totalBlocks64     = *ptrUint64;
		ptrUint64++;
		blocksRemaining64 = *ptrUint64;
		while(totalBlocks64 > 0xFFFFFFFFUL){
			totalBlocks64 = totalBlocks64 >> 1;
			blocksRemaining64 = blocksRemaining64 >> 1;
		}
		totalBlocks32 = (uint32_t)totalBlocks64;
		blocksRemaining32 = (uint32_t)blocksRemaining64;
		pKarg->Configuration.bInformation =
		    (totalBlocks32 - blocksRemaining32) / (totalBlocks32 / 100);

		kfree(pRaidActionReply);
    
	}

	pKarg->Configuration.bDriveCount = pVolume0->NumPhysDisks;

	/*
	 * get RAID Physical Disk Page 0
	 */
	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 0;
	header.PageType = MPI_CONFIG_PAGETYPE_RAID_PHYSDISK;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = 0;
	if (mpt_config(ioc, &cfg) != 0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	if (header.PageLength == 0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	physdiskpage0sz = header.PageLength * 4;
	pPhysDisk0 = pci_alloc_consistent(ioc->pcidev, physdiskpage0sz,
	    &physdisk0_dma);
	if (!pPhysDisk0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}
	cfg.physAddr = physdisk0_dma;

	physDiskNumMax = (csmi_sas_raid_config_buffer_sz -
	    sizeof(CSMI_SAS_RAID_CONFIG_BUFFER)) / sizeof(CSMI_SAS_RAID_DRIVES);

	for (i=0; i< min(pVolume0->NumPhysDisks, physDiskNumMax); i++) {

		physDiskNum = pVolume0->PhysDisk[i].PhysDiskNum;
		cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
		cfg.pageAddr = physDiskNum;
		if (mpt_config(ioc, &cfg) != 0){
			pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto cim_get_raid_config_exit;
		}

		memset(&pKarg->Configuration.Drives[i],0,
		    sizeof(CSMI_SAS_RAID_DRIVES));

		memcpy(pKarg->Configuration.Drives[i].bModel,
		    pPhysDisk0->InquiryData.VendorID,
		    offsetof(RAID_PHYS_DISK0_INQUIRY_DATA,ProductRevLevel));

		memcpy(pKarg->Configuration.Drives[i].bFirmware,
			pPhysDisk0->InquiryData.ProductRevLevel,
			sizeof(pPhysDisk0->InquiryData.ProductRevLevel));

		memcpy(pKarg->Configuration.Drives[i].bSerialNumber,
			pPhysDisk0->DiskIdentifier,
			sizeof(pPhysDisk0->DiskIdentifier));

		switch(pPhysDisk0->PhysDiskStatus.State) {
		case MPI_PHYSDISK0_STATUS_ONLINE:
			pKarg->Configuration.Drives[i].bDriveStatus =
			    CSMI_SAS_DRIVE_STATUS_OK;
			pKarg->Configuration.Drives[i].bDriveUsage =
			    CSMI_SAS_DRIVE_CONFIG_MEMBER;
			break;
		case MPI_PHYSDISK0_STATUS_FAILED:
		case MPI_PHYSDISK0_STATUS_FAILED_REQUESTED:
			pKarg->Configuration.Drives[i].bDriveStatus =
			    CSMI_SAS_DRIVE_STATUS_FAILED;
			pKarg->Configuration.Drives[i].bDriveUsage =
			    CSMI_SAS_DRIVE_CONFIG_MEMBER;
			pKarg->Configuration.bInformation = i;
			break;
		case MPI_PHYSDISK0_STATUS_INITIALIZING:
			pKarg->Configuration.Drives[i].bDriveStatus =
			    CSMI_SAS_DRIVE_STATUS_REBUILDING;
			pKarg->Configuration.Drives[i].bDriveUsage =
			    CSMI_SAS_DRIVE_CONFIG_MEMBER;
			break;
		case MPI_PHYSDISK0_STATUS_OTHER_OFFLINE:
		case MPI_PHYSDISK0_STATUS_MISSING:
		case MPI_PHYSDISK0_STATUS_NOT_COMPATIBLE:
		case MPI_PHYSDISK0_STATUS_OFFLINE_REQUESTED:
		default:
			pKarg->Configuration.Drives[i].bDriveStatus =
			    CSMI_SAS_DRIVE_STATUS_FAILED;
			pKarg->Configuration.Drives[i].bDriveUsage =
			    CSMI_SAS_DRIVE_CONFIG_NOT_USED;
			break;
		}

		/* Search the list for the matching SAS address. */
		found = FALSE;
		list_for_each_entry(sasDevice, &ioc->sasDeviceList, list) {

			/* Found the matching device. */
			if ((pPhysDisk0->PhysDiskIOC == sasDevice->Bus) &&
				(pPhysDisk0->PhysDiskID == sasDevice->TargetId)) {
				u64 SASAddress64;
				found = TRUE;

				SASAddress64 = reverse_byte_order64(&sasDevice->SASAddress);
				memcpy(pKarg->Configuration.Drives[i].bSASAddress,
				   &SASAddress64,sizeof(u64));
				memset(pKarg->Configuration.Drives[i].bSASLun, 0,
				     sizeof(pKarg->Configuration.Drives[i].bSASLun));
				break;
			} else
				/* Keep looking. */
				continue;
		}
	}

	pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

cim_get_raid_config_exit:

	if (pVolume0 != NULL)
		pci_free_consistent(ioc->pcidev, volumepage0sz, pVolume0,
		    volume0_dma);

	if(pPhysDisk0 != NULL)
		pci_free_consistent(ioc->pcidev, physdiskpage0sz, pPhysDisk0,
		    physdisk0_dma);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, pKarg,
				csmi_sas_raid_config_buffer_sz)) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_get_raid_config @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		kfree(pKarg);
		return -EFAULT;
	}

	kfree(pKarg);

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI SAS Task Managment Config command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_task_managment(unsigned long arg)
{
	CSMI_SAS_SSP_TASK_IU_BUFFER *uarg = (void *) arg;
	CSMI_SAS_SSP_TASK_IU_BUFFER	 karg;
	pSCSITaskMgmt_t			pScsiTm;
	pSCSITaskMgmtReply_t		pScsiTmReply;
	MPT_ADAPTER			*ioc = NULL;
	MPT_SCSI_HOST			*hd;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t			*mpi_hdr;
	int				iocnum;
	u8				taskType;
	u8				path;
	u8				target;
	u8				lun;
	u8				queueTag;
	u32				msgContext = 0;
	int				retval;
	int				i;
	u8 				found_qtag;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_SSP_TASK_IU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_task_managment struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		"%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	/* try to catch an error
	 */
	if ((karg.Parameters.uFlags & CSMI_SAS_TASK_IU) &&
	    (karg.Parameters.uFlags & CSMI_SAS_HARD_RESET_SEQUENCE)) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto cim_get_task_managment_exit;
	}

	if (karg.Parameters.uFlags & CSMI_SAS_TASK_IU) {
		switch (karg.Parameters.bTaskManagementFunction) {

		case CSMI_SAS_SSP_ABORT_TASK:
			taskType = MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK;
			break;
		case CSMI_SAS_SSP_ABORT_TASK_SET:
			taskType = MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET;
			break;
		case CSMI_SAS_SSP_CLEAR_TASK_SET:
			taskType = MPI_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET;
			break;
		case CSMI_SAS_SSP_LOGICAL_UNIT_RESET:
			taskType = MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET;
			break;
		case CSMI_SAS_SSP_CLEAR_ACA:
		case CSMI_SAS_SSP_QUERY_TASK:
		default:
			karg.IoctlHeader.ReturnCode =
			    CSMI_SAS_STATUS_INVALID_PARAMETER;
			goto cim_get_task_managment_exit;
		}
	}else if (karg.Parameters.uFlags & CSMI_SAS_HARD_RESET_SEQUENCE) {
		/* set the code up to do a hard reset
		 */
		taskType = MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	}else {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto cim_get_task_managment_exit;
	}

	path = karg.Parameters.bPathId;
	target = karg.Parameters.bTargetId;
	lun = karg.Parameters.bLun;
	queueTag = (u8)karg.Parameters.uQueueTag & 0xFF;

	if ((ioc->sh == NULL) || (ioc->sh->hostdata == NULL)) {
		karg.IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto cim_get_task_managment_exit;
	}
	else
		hd = (MPT_SCSI_HOST *) ioc->sh->hostdata;

	switch ( karg.Parameters.uInformation ) {
		case CSMI_SAS_SSP_TEST:
			dsasprintk(("TM request for test purposes\n"));
			break;
		case CSMI_SAS_SSP_EXCEEDED:
			dsasprintk(("TM request due to timeout\n"));
			break;
		case CSMI_SAS_SSP_DEMAND:
			dsasprintk(("TM request demanded by app\n"));
			break;
		case CSMI_SAS_SSP_TRIGGER:
			dsasprintk(("TM request sent to trigger event\n"));
			break;
	}

	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	switch (taskType) {

	case MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK:
	/*
	 * look up qtag in the ScsiLookup[] table
	 */
		for (i=0,found_qtag=0;i<hd->ioc->req_depth;i++) {
			if ((hd->ScsiLookup[i]) &&
			    (hd->ScsiLookup[i]->tag == queueTag)) {
				mf = MPT_INDEX_2_MFPTR(hd->ioc, i);
				msgContext =
				    mf->u.frame.hwhdr.msgctxu.MsgContext;
				found_qtag=1;
				break;
			}
		}

		if(!found_qtag) {
			karg.IoctlHeader.ReturnCode =
			    CSMI_SAS_STATUS_INVALID_PARAMETER;
			goto cim_get_task_managment_exit;
		}

	case MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
	case MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
	case MPI_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET:
	/* for now, this should work
	 */
	case MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET:

		/* Single threading ....
		 */
		if (mptctl_set_tm_flags(hd) != 0) {
			karg.IoctlHeader.ReturnCode =
			    CSMI_SAS_STATUS_FAILED;
			goto cim_get_task_managment_exit;
		}

		/* Send request
		 */
		if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
			dctlprintk((": no msg frames!\n"));
			mptctl_free_tm_flags(ioc);
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto cim_get_task_managment_exit;
		}

		mpi_hdr = (MPIHeader_t *) mf;
		pScsiTm = (pSCSITaskMgmt_t ) mf;

		memset(pScsiTm,0,sizeof(SCSITaskMgmt_t));
		pScsiTm->TaskType = taskType;
		pScsiTm->Bus = path;
		pScsiTm->TargetID = target;
		pScsiTm->LUN[1] = lun;
		pScsiTm->MsgContext = mpi_hdr->MsgContext;
		pScsiTm->TaskMsgContext = msgContext;
		pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;

		ioc->ioctl->wait_done = 0;
		ioc->ioctl->tmPtr = mf;
		ioc->ioctl->TMtimer.expires = jiffies + HZ*MPT_IOCTL_DEFAULT_TIMEOUT;
		ioc->ioctl->status |= MPT_IOCTL_STATUS_TMTIMER_ACTIVE;
		add_timer(&ioc->ioctl->TMtimer);

		DBG_DUMP_TM_REQUEST_FRAME((u32 *)mf);
		
		if ((retval = mpt_send_handshake_request(mptctl_id, ioc->ioctl->ioc,
		     sizeof(SCSITaskMgmt_t), (u32*)pScsiTm, CAN_SLEEP)) != 0) {
			dfailprintk((MYIOC_s_ERR_FMT "_send_handshake FAILED!"
				" (hd %p, ioc %p, mf %p) \n", hd->ioc->name, hd,
				hd->ioc, mf));
			goto cim_get_task_managment_exit;
		}

		if (retval == 0) {
			wait_event(mptctl_wait, ioc->ioctl->wait_done);
		} else {
			mptctl_free_tm_flags(ioc);
			del_timer(&ioc->ioctl->TMtimer);
			mpt_free_msg_frame(ioc, mf);
			ioc->ioctl->tmPtr = NULL;
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			ioc->ioctl->status &= ~MPT_IOCTL_STATUS_TMTIMER_ACTIVE;
			goto cim_get_task_managment_exit;
		}

		if (ioc->ioctl->status & MPT_IOCTL_STATUS_RF_VALID) {
			pScsiTmReply =
			    (pSCSITaskMgmtReply_t ) ioc->ioctl->ReplyFrame;

			memset(&karg.Status,0,
			    sizeof(CSMI_SAS_SSP_PASSTHRU_STATUS));

			if(pScsiTmReply->IOCStatus == MPI_IOCSTATUS_SUCCESS) {
				karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
				karg.Status.bSSPStatus = CSMI_SAS_SSP_STATUS_COMPLETED;
			}else if(pScsiTmReply->IOCStatus == MPI_IOCSTATUS_INSUFFICIENT_RESOURCES) {
				karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
				karg.Status.bSSPStatus = CSMI_SAS_SSP_STATUS_RETRY;
			}else {
				karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
				karg.Status.bSSPStatus = CSMI_SAS_SSP_STATUS_FATAL_ERROR;
			}
		}else{
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		}

		break;

	default:
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;
		break;
	}

cim_get_task_managment_exit:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
				sizeof(CSMI_SAS_SSP_TASK_IU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_task_managment @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	map_sas_status_to_csmi - Conversion  for HP Connection Status
 *	@mpi_sas_status: Sas status returned by the firmware
 *
 *	Returns converted connection status
 *
 */
static u8
map_sas_status_to_csmi(u8 mpi_sas_status)
{
	u8  csmi_connect_status;

	switch (mpi_sas_status) {

	case MPI_SASSTATUS_SUCCESS:
		csmi_connect_status = CSMI_SAS_OPEN_ACCEPT;
		break;

	case MPI_SASSTATUS_UTC_BAD_DEST:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_BAD_DESTINATION;
		break;

	case MPI_SASSTATUS_UTC_CONNECT_RATE_NOT_SUPPORTED:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RATE_NOT_SUPPORTED;
		break;

	case MPI_SASSTATUS_UTC_PROTOCOL_NOT_SUPPORTED:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_PROTOCOL_NOT_SUPPORTED;
		break;

	case MPI_SASSTATUS_UTC_STP_RESOURCES_BUSY:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_STP_RESOURCES_BUSY;
		break;

	case MPI_SASSTATUS_UTC_WRONG_DESTINATION:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_WRONG_DESTINATION;
		break;

	case MPI_SASSTATUS_SDSF_NAK_RECEIVED:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RETRY;
		break;

	case MPI_SASSTATUS_SDSF_CONNECTION_FAILED:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_PATHWAY_BLOCKED;
		break;

	case MPI_SASSTATUS_INITIATOR_RESPONSE_TIMEOUT:
		csmi_connect_status =  CSMI_SAS_OPEN_REJECT_NO_DESTINATION;
		break;

	case MPI_SASSTATUS_UNKNOWN_ERROR:
	case MPI_SASSTATUS_INVALID_FRAME:
	case MPI_SASSTATUS_UTC_BREAK_RECEIVED:
	case MPI_SASSTATUS_UTC_PORT_LAYER_REQUEST:
	case MPI_SASSTATUS_SHORT_INFORMATION_UNIT:
	case MPI_SASSTATUS_LONG_INFORMATION_UNIT:
	case MPI_SASSTATUS_XFER_RDY_INCORRECT_WRITE_DATA:
	case MPI_SASSTATUS_XFER_RDY_REQUEST_OFFSET_ERROR:
	case MPI_SASSTATUS_XFER_RDY_NOT_EXPECTED:
	case MPI_SASSTATUS_DATA_INCORRECT_DATA_LENGTH:
	case MPI_SASSTATUS_DATA_TOO_MUCH_READ_DATA:
	case MPI_SASSTATUS_DATA_OFFSET_ERROR:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RESERVE_STOP;
		break;

	default:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RESERVE_STOP;
		break;
	}

	return csmi_connect_status;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI SAS Phy Control command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_phy_control(unsigned long arg)
{
	CSMI_SAS_PHY_CONTROL_BUFFER *uarg = (void *) arg;
	CSMI_SAS_PHY_CONTROL_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_PHY_CONTROL_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_phy_control_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		"%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

/* TODO - implement IOCTL here */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
	dctlprintk((": not implemented\n"));

// cim_sas_phy_control_exit:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
				sizeof(CSMI_SAS_PHY_CONTROL_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_phy_control_buffer @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;

}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI SAS Get Connector info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_connector_info(unsigned long arg)
{
	CSMI_SAS_CONNECTOR_INFO_BUFFER *uarg = (void *) arg;
	CSMI_SAS_CONNECTOR_INFO_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_CONNECTOR_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_connector_info_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		"%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

/* TODO - implement IOCTL here */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
	dctlprintk((": not implemented\n"));

// cim_sas_get_connector_info_exit:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
				sizeof(CSMI_SAS_CONNECTOR_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_connector_info_buffer @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;

}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Prototype Routine for the HP CSMI SAS Get location command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
mptctl_csmi_sas_get_location(unsigned long arg)
{
	CSMI_SAS_CONNECTOR_INFO_BUFFER *uarg = (void *) arg;
	CSMI_SAS_CONNECTOR_INFO_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;

	dctlprintk((": %s called.\n",__FUNCTION__));

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_GET_LOCATION_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_get_location_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		dctlprintk((KERN_ERR
		"%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

	if (!mptctl_is_this_sas_cntr(ioc)) {
		dctlprintk((KERN_ERR
		    "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum));
		return -ENODEV;
	}

/* TODO - implement IOCTL here */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
	dctlprintk((": not implemented\n"));

// cim_sas_get_location_exit:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user((char *)arg, &karg,
				sizeof(CSMI_SAS_GET_LOCATION_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_get_location_buffer @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	return 0;

}
#endif // CPQ_CIM
