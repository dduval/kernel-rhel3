#ifndef ISCSI_CRC_H_
#define ISCSI_CRC_H_

extern uint32_t iscsi_crc32c(void *address, unsigned long length);
extern uint32_t iscsi_crc32c_continued(void *address, unsigned long length,
				       uint32_t crc);

#endif
