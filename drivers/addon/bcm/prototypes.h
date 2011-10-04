#include "cdevincl.h"

int
 ubsec_tlsmac (ubsec_DeviceContext_t pContext, ubsec_tlsmac_io_pt * ppIOInfo);
int
 ubsec_sslmac (ubsec_DeviceContext_t pContext, ubsec_sslmac_io_pt * ppIOInfo);
int
 ubsec_hash (ubsec_DeviceContext_t pContext, ubsec_hash_io_pt * ppIOInfo);
int
 ubsec_sslcipher (ubsec_DeviceContext_t pContext, ubsec_sslcipher_io_pt * ppIOInfo);
int
 ubsec_sslarc4 (ubsec_DeviceContext_t pContext, ubsec_arc4_io_pt * ppIOInfo);
int ubsec_chipinfo (ubsec_DeviceContext_t pContext, ubsec_chipinfo_io_pt * ppIOInfo);
int DumpDeviceInfo (PInt pm);
int FailDevices (PInt pm);
int TestDevice (int SelectedDevice);
int TestDevices (PInt pm);
int GetHardwareVersion (PInt pm);
int init_arc4if (void);
void shutdown_arc4if (void);
int SetupFragmentList (ubsec_FragmentInfo_pt Frags, unsigned char *packet, int packet_len);
