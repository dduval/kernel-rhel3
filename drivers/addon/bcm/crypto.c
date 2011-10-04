/*
 *  Broadcom Cryptonet Driver software is distributed as is, without any warranty
 *  of any kind, either express or implied as further specified in the GNU Public
 *  License. This software may be used and distributed according to the terms of
 *  the GNU Public License.
 *
 * Cryptonet is a registered trademark of Broadcom Corporation.
 */
/******************************************************************************
 *
 *  Copyright 2000
 *  Broadcom Corporation
 *  16215 Alton Parkway
 *  PO Box 57013
 *  Irvine CA 92619-7013
 *
 *****************************************************************************/
/* 
 * Broadcom Corporation uBSec SDK 
 */
/*
 * Crypto operations for character driver interface to the ubsec driver
 *
 * Revision History:
 *
 * May 2000 SOR/JTT Created.
 * March 2001 PW Release for Linux 2.4 UP and SMP kernel 
 * 10/09/2001 SRM 64 bit port
 */

#include "cdevincl.h"

#ifdef FILE_DEBUG_TAG
#undef FILE_DEBUG_TAG
#endif
#define FILE_DEBUG_TAG  "BCMCRYPTO"

#define AUTH_BUF_SIZE 4096

#ifdef POLL
#undef GOTOSLEEP
#endif

/* These are useful only for diagnostics. */
#undef SETFRAGMENTS
#undef STATIC_ALLOC_OF_CRYPTO_BUFFERS

#ifdef STATIC_ALLOC
#define STATIC_ALLOC_OF_CRYPTO_BUFFERS 1
#endif


#ifdef SETFRAGMENTS
ubsec_FragmentInfo_t SourceFragments[MAX_COMMANDS][MAX_FRAGMENTS];
ubsec_FragmentInfo_t DestinationFragments[MAX_COMMANDS][MAX_FRAGMENTS];
#endif

int SetupFragmentList(ubsec_FragmentInfo_pt Frags, unsigned char *packet,int packet_len);
unsigned long Page_Size = PAGE_SIZE;

#ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS
unsigned char *kern_source_buf = NULL;
unsigned char *kern_dest_buf   = NULL;
unsigned char *kern_auth_buf   = NULL;
static ubsec_MemAddress_t PhysSourceBuf;
static ubsec_MemAddress_t PhysDestBuf;
static ubsec_MemAddress_t PhysAuthBuf;
#endif

#define ALLOC_PAGE_SIZE ((unsigned long)Page_Size)
#define ALLOC_PAGE_MASK (ALLOC_PAGE_SIZE-1)
#define BYTES_TO_PAGE_BOUNDARY_PHYS(v) \
 ((unsigned long)(ALLOC_PAGE_SIZE-(((unsigned long)v)&(ALLOC_PAGE_MASK))))
#define BYTES_TO_PAGE_BOUNDARY(p) (BYTES_TO_PAGE_BOUNDARY_PHYS(virt_to_bus(p)))

/**************************************************************************
 *
 *  Function:  init_cryptoif
 *   
 *************************************************************************/
int init_cryptoif(void)
{
#ifdef DEBUG
  PRINTK("ubsec library version %d.%x%c\n", UBSEC_VERSION_MAJOR,
	 UBSEC_VERSION_MINOR, UBSEC_VERSION_REV);
#endif

#ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  kern_source_buf = 
    (char *) kmalloc((MAX_FILE_SIZE ),GFP_KERNEL|GFP_ATOMIC);

  if( kern_source_buf == NULL ) {
    PRINTK("no memory for source buffer\n");
    return -ENOMEM;
  }

#ifdef DEBUG
  PRINTK("Allocate %x %x\n",kern_source_buf,vtophys(kern_source_buf));
#endif

/* Destination Buffers are local so no problem in over writing */
  kern_dest_buf=kern_source_buf;
  kern_auth_buf = (char *) kmalloc((AUTH_BUF_SIZE),GFP_KERNEL|GFP_ATOMIC);
  if( kern_auth_buf == NULL ) {
    kfree(kern_source_buf);
    PRINTK("no memory for auth buffer\n");
    return -ENOMEM;
  }

  PhysSourceBuf=(ubsec_MemAddress_t)(virt_to_bus(kern_source_buf));
  PhysDestBuf=(ubsec_MemAddress_t)(virt_to_bus(kern_dest_buf));
  PhysAuthBuf=(ubsec_MemAddress_t)(virt_to_bus(kern_auth_buf));

#ifdef DEBUG
  PRINTK("Memory Alloc source %x %x Dest %x %x for source buffer\n",
	 kern_source_buf,PhysSourceBuf,kern_dest_buf,PhysDestBuf);
#endif

#endif /* STATIC_ALLOC */

  return 0; /* success */
}



/**************************************************************************
 *
 *  Function:  cleanup_module
 *
 *************************************************************************/
void shutdown_cryptoif(void)
{
#ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  if (kern_source_buf != NULL)
    kfree(kern_source_buf);
  if (kern_auth_buf != NULL)
    kfree(kern_auth_buf);
#endif /* STATIC_ALLOC */
}



/**************************************************************************
 *
 *  Function:  do_encrypt
 *
 *************************************************************************/
int
do_encrypt(ubsec_DeviceContext_t pContext,ubsec_io_t *pat, unsigned int features)
{
  ubsec_io_t		at_buf;
  ubsec_io_pt		at = &at_buf;
#ifndef SETFRAGMENTS
  ubsec_FragmentInfo_t *SourceFragments=NULL;
  ubsec_FragmentInfo_t *DestinationFragments=NULL;
#endif
  unsigned int num_packets;
  unsigned int total_packets;
  unsigned int packets_done;
  ubsec_HMAC_State_t HMAC_State;
  ubsec_CipherCommandInfo_pt acmd;
  volatile CommandContext_t CommandContext;
  unsigned int i;
#ifdef SETFRAGMENTS
  unsigned int  j;
#endif
  unsigned int src_pos;
  unsigned int dest_pos;
  unsigned char *user_source_buf;
  unsigned char *user_dest_buf;

  unsigned int source_buf_size;
  unsigned int user_dest_buf_size;	 
  unsigned int dest_buf_size;
  unsigned int in_packet_size;
  unsigned int out_packet_size;
  int MacSize=0;
  unsigned long delay_total_us;
  ubsec_CipherCommandInfo_t *ubsec_commands = NULL;

#ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  unsigned char *kern_source_buf = NULL;
  unsigned char *kern_dest_buf   = NULL;
  unsigned char *kern_auth_buf   = NULL;
  ubsec_MemAddress_t PhysSourceBuf;
  ubsec_MemAddress_t PhysDestBuf;
  ubsec_MemAddress_t PhysAuthBuf=0;
#endif
  int Status=-1;
  /*int CryptoHeaderLen=0; */
  int alignbytes=0; /* Holds the no of bytes to align the encrypt buffer so that the auth buffer starts at 4 byte boundry */ /* This happens only when Auth only with clear message not in 4 byte aligned boundry */ 

	/*
	 * Copy control packet into kernel space.
	 */
  copy_from_user( at, pat , sizeof(*at));
  /*
  if (UBSEC_USING_CRYPT(at->flags) == UBSEC_3DES)
  {
     if (!(features & UBSEC_EXTCHIPINFO_3DES))
        return -EINVAL;
  }
  */

  /* Validation */

  CHECK_SIZE(at->source_buf_size,MAX_CRYPT_LENGTH_BITS);
  CHECK_SIZE(at->dest_buf_size,MAX_CRYPT_LENGTH_BITS);

  user_source_buf = at->source_buf;
  user_dest_buf   = at->dest_buf;
  source_buf_size   = at->source_buf_size;
  user_dest_buf_size     = at->dest_buf_size;
	
  at->time_us =0;

/* Calculate Dest buf size */
dest_buf_size = 0; /* minimum required buf size */
if (UBSEC_USING_CRYPT(at->flags))
	dest_buf_size =source_buf_size - at->crypt_header_skip; /* crypt data */
if ( UBSEC_USING_MAC(at->flags) ){
	if (at->flags & UBSEC_ENCODE)
		dest_buf_size += ( ((at->flags & UBSEC_MAC_MD5) ? MD5_LENGTH : SHA1_LENGTH) * at->num_packets); /* auth data required */
	else if (UBSEC_USING_CRYPT(at->flags)  )
		dest_buf_size -= ( ((at->flags & UBSEC_MAC_MD5) ? MD5_LENGTH : SHA1_LENGTH) * at->num_packets); /* auth data  substract from the size; expect auth data in the in size*/
	}

/* Calculate the size of the packets */
  in_packet_size    = source_buf_size / at->num_packets;
  out_packet_size   = dest_buf_size / at->num_packets;

  if ( at->flags & UBSEC_ENCODE ) {
  	alignbytes = 4 -  (source_buf_size & 0x03) ;  
  	if (alignbytes == 4)
		alignbytes =0; /* 4 - 0 = 4  so fix it here*/
	}
  
#ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  if( source_buf_size > MAX_FILE_SIZE ) {
    PRINTK("input buffer size too large <%d,%d>\n",source_buf_size,MAX_FILE_SIZE);
    Status=EINVAL;
    goto Error_Ret;
  }
  if( dest_buf_size > MAX_FILE_SIZE ) {
    PRINTK("required output buffer too large <%d,%d>\n",dest_buf_size,MAX_FILE_SIZE);
    Status=EINVAL;
    goto Error_Ret;
  }
#endif

#ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  kern_source_buf = 
    (char *) kmalloc((((dest_buf_size>source_buf_size)?dest_buf_size:source_buf_size)+alignbytes),
		GFP_KERNEL|GFP_ATOMIC);

  if( kern_source_buf == NULL ) {
    PRINTK("no memory for source buffer %d\n",dest_buf_size);
    Status=-ENOMEM;
    goto Error_Ret;
  }

#ifdef DEBUG
  PRINTK("Allocate %x %x\n",kern_source_buf,vtophys(kern_source_buf));
#endif

  kern_dest_buf=kern_source_buf;

  PhysSourceBuf=(ubsec_MemAddress_t)(virt_to_bus(kern_source_buf));
  PhysDestBuf=(ubsec_MemAddress_t)(virt_to_bus(kern_dest_buf));

#ifdef DEBUG
  PRINTK("Memory Alloc source %x %x Dest %x %x for source buffer\n",
	 kern_source_buf,PhysSourceBuf,kern_dest_buf,PhysDestBuf);
#endif

#endif /* STATIC_ALLOC */

  packets_done = 0;   /* incremented every time callback is called */
  num_packets = at->num_packets;
  if( num_packets > MAX_COMMANDS ) {
    PRINTK("too many packets/commands\n");
    Status=EINVAL;
    goto Error_Ret;
  }

#ifdef SETFRAGMENTS
  if (at->num_fragments > UBSEC_MAX_FRAGMENTS)
  {
    PRINTK("too many fragments\n");
    Status=EINVAL;
    goto Error_Ret;
  }
#endif

  /*
   * File size is a little different when using MAC
   */
  if(UBSEC_USING_MAC( at->flags ) ) {
    /*
     * We need to initialize the inner and outer hash keys.
     */
    MacSize= (at->flags & UBSEC_MAC_MD5) ? MD5_LENGTH : SHA1_LENGTH;
    if ( at->flags & UBSEC_DECODE ) {
#ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS
      /*kern_auth_buf = (char *) kmalloc((AUTH_BUF_SIZE),GFP_KERNEL|GFP_ATOMIC);*/
      kern_auth_buf = (char *) kmalloc((at->num_packets*MacSize),GFP_KERNEL|GFP_ATOMIC);
      if( kern_auth_buf == NULL ) {
	PRINTK("no memory for auth buffer\n");
	Status=-ENOMEM;
	goto Error_Ret;
      }
#endif /* STATIC_ALLOC_OF_CRYPTO_BUFFERS */
      in_packet_size    = (source_buf_size - (at->num_packets*MacSize)) / at->num_packets;
      PhysAuthBuf=(ubsec_MemAddress_t)(virt_to_bus(kern_auth_buf));
      /*memset(kern_auth_buf,0,AUTH_BUF_SIZE);*/
      memset(kern_auth_buf,0,at->num_packets*MacSize);
    }
    else{
      	out_packet_size   = (dest_buf_size - (at->num_packets*MacSize)) / at->num_packets; 
	
    	if(UBSEC_USING_CRYPT( at->flags ) ) 
     		PhysAuthBuf=(ubsec_MemAddress_t)(virt_to_bus(&kern_dest_buf[(at->num_packets*out_packet_size) + alignbytes])); /* Add to end */   
	else
     		PhysAuthBuf=(ubsec_MemAddress_t)(virt_to_bus(kern_dest_buf)); /* no crypto  */   
	}
   }

#ifndef SETFRAGMENTS
  /* Allocate memory for fragment information. */
  SourceFragments=(ubsec_FragmentInfo_t *) kmalloc(((sizeof(ubsec_FragmentInfo_t)*num_packets*UBSEC_MAX_FRAGMENTS)),GFP_KERNEL|GFP_ATOMIC);
  if( SourceFragments == NULL ) {
    PRINTK("no memory for source fragment buffer\n");
    Status=-ENOMEM;
    goto Error_Ret;
  }
  DestinationFragments=(ubsec_FragmentInfo_t *) kmalloc(((sizeof(ubsec_FragmentInfo_t)*num_packets*UBSEC_MAX_FRAGMENTS)),GFP_KERNEL|GFP_ATOMIC);
  if( DestinationFragments == NULL ) {
    PRINTK("no memory for destination fragment buffer\n");
    Status=-ENOMEM;
    goto Error_Ret;
  }
#endif /* SETFRAGMENTS */

/*
  memset(ubsec_commands,0, sizeof(ubsec_commands));
*/
  memset((CommandContext_t *)&CommandContext,0, sizeof(CommandContext_t));
  src_pos = 0;
  dest_pos = 0;

  ubsec_commands=(ubsec_CipherCommandInfo_t *) kmalloc((sizeof(ubsec_CipherCommandInfo_t)*num_packets),GFP_KERNEL|GFP_ATOMIC);

  if( ubsec_commands == NULL ) {
    PRINTK("no memory for source buffer\n");
    return -ENOMEM;
  }



  for(i = 0; i < num_packets; i++) {
    acmd = &ubsec_commands[i];
    acmd->InitialVector = (ubsec_IV_pt) at->initial_vector;
    acmd->CryptKey = (ubsec_CryptKey_pt) at->crypt_key;
    acmd->Command=at->flags;

/* Expect the states in mac_key */
    if (USING_MAC_STATES(at->flags)){
      memcpy(&HMAC_State,at->mac_key,sizeof(ubsec_HMAC_State_t));	
    }
    else if (UBSEC_USING_MAC(at->flags)) 
      ubsec_InitHMACState(&HMAC_State,UBSEC_USING_MAC(acmd->Command),at->mac_key);

    acmd->HMACState=&HMAC_State;
    if ((int)(at->crypt_header_skip & 0x03)) {
		PRINTK("Invalid crypto offset length %08x\n",at->crypt_header_skip);
		Status=UBSEC_STATUS_INVALID_PARAMETER;
    		goto Error_Ret;
		}
    acmd->CryptHeaderSkip = at->crypt_header_skip/4; /* Done here so the SRL does not do */
    /*CryptoHeaderLen= at->crypt_header_skip ;*/

#ifdef SETFRAGMENTS
    acmd->NumSource = at->num_fragments;
    acmd->NumDestination = at->num_fragments;
    for( j = 0; j < at->num_fragments; j++ ) {
      SourceFragments[i][j].FragmentAddress = PhysSourceBuf+src_pos;
      SourceFragments[i][j].FragmentLength = in_packet_size / at->num_fragments;
      if( j+1 == at->num_fragments )
	SourceFragments[i][j].FragmentLength += in_packet_size % at->num_fragments;
      src_pos += SourceFragments[i][j].FragmentLength;
    }
    acmd->SourceFragments=&SourceFragments[i][0];

    /*
     * Keep destination fragments separate since there
     * are more restrictions on them
     */
    if ((at->num_fragments == 1) || (!(at->num_fragments % 4)))
      acmd->NumDestination = at->num_fragments;
    else
      acmd->NumDestination=4;  /* Default. */

    for( j = 0; j < acmd->NumDestination; j++ ) {
      DestinationFragments[i][j].FragmentAddress =  PhysDestBuf+dest_pos;
      DestinationFragments[i][j].FragmentLength = out_packet_size / acmd->NumDestination; /*at->num_fragments; */
      if( j+1 == acmd->NumDestination)
	DestinationFragments[i][j].FragmentLength += out_packet_size % acmd->NumDestination;
      dest_pos += DestinationFragments[i][j].FragmentLength;
    }
    acmd->DestinationFragments=&DestinationFragments[i][0];


#else
    /* Since we do not have a large enough contiguous buffer, we override
       the fragment num setting and set the fragment accordingly. */
    acmd->SourceFragments=&SourceFragments[i*UBSEC_MAX_FRAGMENTS];
    if ((acmd->NumSource=SetupFragmentList(acmd->SourceFragments,&kern_source_buf[src_pos],in_packet_size)) == 0) {
      /* The input data requires more fragments than the current driver build can provide; return error */
      Status=UBSEC_STATUS_NO_RESOURCE;
      goto Error_Ret;
    }

    src_pos += in_packet_size;
    acmd->DestinationFragments=&DestinationFragments[i*UBSEC_MAX_FRAGMENTS];
    if(UBSEC_USING_CRYPT( at->flags ) ) {
      if ((acmd->NumDestination=SetupFragmentList(acmd->DestinationFragments,&kern_dest_buf[dest_pos],out_packet_size)) == 0) {
	/* The output data would require more fragments than the current driver build can provide; return error */
	Status=UBSEC_STATUS_NO_RESOURCE;
	goto Error_Ret;
      }

/* We donot expect Header offset for output data */
#if 0 
 	if (CryptoHeaderLen > 0)  {
		for ( k=0;k < UBSEC_MAX_FRAGMENTS;i++){
		if (CryptoHeaderLen > out_packet_size)	{
    			acmd->DestinationFragments[k].FragmentAddress = virt_to_bus(&kern_dest_buf[dest_pos+out_packet_size]);
    			acmd->DestinationFragments[k].FragmentLength -= out_packet_size;
			CryptoHeaderLen -=out_packet_size;
			} else{
    			acmd->DestinationFragments[k].FragmentAddress = virt_to_bus(&kern_dest_buf[dest_pos+CryptoHeaderLen]);
    			acmd->DestinationFragments[k].FragmentLength -= CryptoHeaderLen;
			CryptoHeaderLen -=CryptoHeaderLen;
			}
			if (CryptoHeaderLen <=0)
				break;
			}
		}
#endif /* 0 */
   	 dest_pos+= out_packet_size;
	}else{
	/* To satisfy the SRL, Donot use it Chip */
    	acmd->NumDestination=1; 
    	DestinationFragments[i].FragmentLength = 1;
    	DestinationFragments[i].FragmentAddress = PhysAuthBuf; /* Nothing official about it, satisfys alignment checks */
   	dest_pos+= out_packet_size;
	}
#endif

    /*
     * If we are doing authentication then we need to allocate
     * another fragment for the output
     */
    if(UBSEC_USING_MAC( acmd->Command ) ) {
      acmd->AuthenticationInfo.FragmentAddress = PhysAuthBuf+(MacSize*i);
    }
    if (i==(num_packets-1)) {
	/*
	 * We only set the callback for the last one since that
	 * they are completed in batch mode
	 */
      acmd->CompletionCallback = CmdCompleteCallback;
      acmd->CommandContext = (unsigned long) &CommandContext;
    }
    else {
      acmd->CompletionCallback = NULL;
      acmd->CommandContext=0;
    }
  }

  if(UBSEC_USING_MAC( at->flags )) {
    if (at->flags & UBSEC_DECODE) {
      src_pos+=(MacSize*num_packets);
      }
    else {
      dest_pos+=(MacSize*num_packets);
    }
  }
  if( src_pos != source_buf_size ) {
    PRINTK("invalid source buffer size -- "
	   "given size %u -- total used/needed %u\n", 
	   source_buf_size, src_pos);
    Status=EINVAL;
    goto Error_Ret;
  }
  if(dest_pos != dest_buf_size ) {
    PRINTK("invalid dest buffer size -- "
	   "given size %u -- total used/needed %u\n", 
	   dest_buf_size, dest_pos);
    Status=EINVAL;
    goto Error_Ret;
  }

  /*memset(kern_source_buf,0,dest_buf_size);*/
  memset(kern_source_buf,0,(((dest_buf_size>source_buf_size)?dest_buf_size:source_buf_size)+alignbytes));
  copy_from_user(kern_source_buf, user_source_buf, source_buf_size);

  /* Sync the DMA memory so that the CryptoNetX device can access it. */
  OS_SyncToDevice(kern_source_buf, 0, source_buf_size); /* (MemHandle, offset, bytes) */

  /*
   *  Let the system do anything it may want/need to do before we begin
   *  timing.
   */
  start_time((struct timeval *)&CommandContext.tv_start);
  total_packets=num_packets;  

#ifndef LINUX2dot2
  init_waitqueue_head((wait_queue_head_t *)&CommandContext.WaitQ);
#else
   CommandContext.WaitQ         = 0; 
#endif    

  Status=  ubsec_CipherCommand(pContext,ubsec_commands,&num_packets) ; 
  switch ( Status ) {
  case UBSEC_STATUS_SUCCESS:
    break;
  case UBSEC_STATUS_TIMEOUT:
    PRINTK("ubsec  ubsec_Command() Timeout\n");
    ubsec_ResetDevice(pContext);
    Status=ETIMEDOUT;
    goto Error_Ret;
    break;
  case UBSEC_STATUS_INVALID_PARAMETER:
    PRINTK(" ubsec_Command() Invalid parameter\n");
    Status=EINVAL;
    goto Error_Ret;
    break;
  case UBSEC_STATUS_NO_RESOURCE:
    PRINTK("ubsec  ubsec_Command() No crypto resource. Num Done %d\n",num_packets);
  default:
    Status=ENOMSG;
    goto Error_Ret;
    break;
  }

#ifdef GOTOSLEEP
  if (!(CommandContext.CallBackStatus)) { /* Just in case completed on same thread. */
#ifndef LINUX2dot2
     Gotosleep((wait_queue_head_t *)&CommandContext.WaitQ);
#else
     Gotosleep(&CommandContext.WaitQ);
#endif
     if (!CommandContext.CallBackStatus) { /* interrupt never happened? */
        CommandContext.Status=UBSEC_STATUS_TIMEOUT;
#ifdef DEBUG_TIMEOUT
    	udelay(2);
	/* This loop is to push all the remaining MCR's,  missed by specific  rare interrupt preemting a PushMCR condition */
	while(1)	
		{
		ubsec_PollDevice(pContext);
		PushMCR(pContext);
		if (CommandContext.CallBackStatus)
			{
        		CommandContext.Status=0;
			Status=0;
			goto skip_error_ret;
			}
    		udelay(2);
		}
#else
     PRINTK(" Gotosleep timedout.\n");
     ubsec_ResetDevice(pContext);
     Status = ETIMEDOUT;
     goto Error_Ret;
#endif
     }
  }
#else
  for (delay_total_us=1  ; !(CommandContext.CallBackStatus); delay_total_us++) {
#ifdef POLL /* We need to poll the device if we are operating in POLL mode. */
    ubsec_PollDevice(pContext);
#endif
    if (delay_total_us >= 3000000) {
    Status=ETIMEDOUT;
#ifdef DEBUG_TIMEOUT
	/* This loop is to push all the remaining MCR's,  missed by specific  rare interrupt preemting a PushMCR condition */
	while(1)	
		{
		ubsec_PollDevice(pContext);
		PushMCR(pContext);
		if (CommandContext.CallBackStatus)
			{
			Status=0;
			goto skip_error_ret;
			}
    		udelay(1);
		}
#else
      	PRINTK(" Poll timedout.\n");
     goto Error_Ret;
#endif
    }
    udelay(1);
  }
#endif
skip_error_ret:

  if (UBSEC_USING_MAC( at->flags ) && (at->flags & UBSEC_DECODE)) {

    /* Sync the Auth output memory so that the CPU sees newly DMA'd memory. */
    OS_SyncToCPU(kern_auth_buf, 0, MacSize*at->num_packets); /* (MemHandle, offset, bytes) */

    /* Check the Auth */
    if (memcmp(kern_auth_buf,&kern_source_buf[at->num_packets*in_packet_size],MacSize*at->num_packets)) { /*at->num_packets*MacSize)) */
      	PRINTK(" Authentication error.\n");
#ifdef PRINT_AUTH_ERROR_INFO
	PRINTK("Decode type :%s\n", ((at->flags & UBSEC_DECODE_3DES) ? "3DES" : "DES"));
	PRINTK("Authentication type :%s\n", ((at->flags & UBSEC_MAC_MD5) ? "MD5" : "SHA1"));
	PRINTK("Total Packets  -- %d\n",at->num_packets);
	PRINTK("Total Auth size -- %d\n",MacSize*at->num_packets);
	{
	num_packets = 0;
	while(num_packets < at->num_packets) {
		if(memcmp(kern_auth_buf+(num_packets*MacSize), &kern_source_buf[(at->num_packets*in_packet_size)+(num_packets*MacSize)],MacSize) !=0) { 
			PRINTK("Auth Error in Packet  %d\n",num_packets);
      			for (i=0; i < MacSize ; i++)
				PRINTK("<%x,%x>",(kern_auth_buf+(num_packets*MacSize))[i],kern_source_buf[((at->num_packets*in_packet_size)+(num_packets*MacSize))+i]);
        		PRINTK("\n");
			}
		num_packets++;
		} /* While */
	}
#endif
	Status=-1;
	goto Error_Ret;
    }
  }

  /* Sync the crypto output memory so that the CPU sees newly DMA'd memory. */
  OS_SyncToCPU(kern_dest_buf, 0, user_dest_buf_size); /* (MemHandle, offset, bytes) */

/* FIX for align bubble */
  if (alignbytes ==0 )  /* expect alignbytes == 0 in DECODE */
  	copy_to_user(user_dest_buf, kern_dest_buf, user_dest_buf_size);
  else {
	/* Copy what ever the user wants */
	if (user_dest_buf_size <= (dest_buf_size-MacSize) ){
  		copy_to_user(user_dest_buf, kern_dest_buf, user_dest_buf_size);
		goto Error_Ret;
		}
	
  	copy_to_user(user_dest_buf, kern_dest_buf, (dest_buf_size-MacSize)); /* copy encrypt */
  	copy_to_user(user_dest_buf+(dest_buf_size-MacSize), bus_to_virt((long)PhysAuthBuf),
			((user_dest_buf_size < dest_buf_size)?(user_dest_buf_size-(dest_buf_size-MacSize)):MacSize ) );  /* copy whatever auth the user wants */
	}

  at->time_us = CommandContext.tv_start.tv_sec * 1000000 + CommandContext.tv_start.tv_usec;

 Error_Ret:

#ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  if (kern_source_buf != NULL)
    kfree(kern_source_buf);
  if ( at->flags & UBSEC_DECODE ) {
	if ( UBSEC_USING_MAC(at->flags) ){
  		if (kern_auth_buf != NULL)
    			kfree(kern_auth_buf);
	}
   }
#endif /* STATIC_ALLOC */

#ifndef SETFRAGMENTS
    if (SourceFragments != NULL)
     kfree(SourceFragments);
    if (DestinationFragments != NULL)
     kfree(DestinationFragments);
#endif

    if(ubsec_commands != NULL)
     kfree( ubsec_commands);
  /*
   * Copy back time to user space.
   */
#if 0
  copy_to_user((unsigned char *)&(pat->time_us), (unsigned char *)&time_us, sizeof(time_us));
#else
  copy_to_user(pat, at, sizeof(*at));
#endif

  return(Status);
}



/*
 * Setup Fragment list: Initializes a fragment list for a logically
 * contigous but maybe physically fragmented buffer
 *
 * Return the number of fragments allocated, or 0 if fragmentation failed.
 */
int
SetupFragmentList(ubsec_FragmentInfo_pt Frags, unsigned char *packet,int packet_len)
{
  int NumFrags=1;
  int i;
  
  /* Initial case. */
  Frags[0].FragmentLength=packet_len;
  Frags[0].FragmentAddress=(ubsec_MemAddress_t)(virt_to_bus(packet));
  
  if (packet_len > BYTES_TO_PAGE_BOUNDARY(packet)) {
    /* First case is special since we are dealing with offsets. Readjust length */
    Frags[0].FragmentLength=BYTES_TO_PAGE_BOUNDARY(packet);
    packet_len -= Frags[0].FragmentLength;
    packet += Frags[0].FragmentLength;
    
#ifdef DEBUG_FRAGS
    PRINTK("Frag %d <%x,%x>",0,Frags[0].FragmentLength,Frags[0].FragmentAddress);
#endif
    /* From now on the Frags should be equal and of length ALLOC_PAGE_SIZE until the last. */
    for  (i=1;packet_len > 0;i++) {
      Frags[i].FragmentAddress=(ubsec_MemAddress_t)(virt_to_bus(packet));
#ifdef DEBUG_FRAGS
      if (packet_len > ALLOC_PAGE_SIZE) {
	Frags[i].FragmentLength = ALLOC_PAGE_SIZE;
      } else {
	Frags[i].FragmentLength = packet_len;
      }
#else
      if (packet_len > BYTES_TO_PAGE_BOUNDARY(packet)) {
	Frags[i].FragmentLength = BYTES_TO_PAGE_BOUNDARY(packet);
      } else {
	Frags[i].FragmentLength = packet_len;
      }
#endif
#ifdef DEBUG_FRAGS
      PRINTK("Frag %d <%x,%x>",i,Frags[i].FragmentLength,Frags[i].FragmentAddress);
#endif
      NumFrags++;
      packet+=Frags[i].FragmentLength;
      packet_len-= Frags[i].FragmentLength;
      if ((NumFrags >= UBSEC_MAX_FRAGMENTS) && packet_len) {
 	PRINTK("CryptoNet: Maximum number of fragments (%d) exceeded\n",NumFrags);
 	NumFrags = 0;
	break;
      }
    } /* Each fragment for loop */
  } /* Need-to-fragment block */
#ifdef DEBUG_FRAGS
  PRINTK("Returning %d fragments\n",NumFrags);
#endif
  return(NumFrags);
}




