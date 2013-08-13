
/* a demo device 
* how to use qdev and QOM 
* <xiaoqiang.zhao@.i-soft.com.cn>
*/

#ifndef HW_FLIP_H
#define HW_FLIP_H

#include "hw.h"
#include "pci/pci.h"
#include "sysemu/sysemu.h"
#include "exec/memory.h"

#define FLIP_REG_LEN   4       /* 32 bits register */

typedef struct FLIPState{
	uint8_t conf;          /* configuration reg */
	uint8_t state;         /* state reg */
	uint64_t fliped_nr;    /* total character fliped */

	uint8_t in_nr;         /* bytes count of input reg */
	uint8_t out_nr;        /* bytes count of output reg */

	uint8_t in[FLIP_REG_LEN];   /* input reg */
	uint8_t out[FLIP_REG_LEN];  /* output reg */

	MemoryRegion io;       /* ioport used */
	qemu_irq irq;          /* irq used */

	QemuMutex lock;        /* write lock */

	struct QEMUTimer *flip_timer;   /* dispatch timer */
}FLIPState;

typedef struct PCIFLIPState {
	PCIDevice dev;         /* inherits from PCIDevice */
	FLIPState state;    
}PCIFLIPState;

extern const MemoryRegionOps flip_io_ops; /* io read / write functions */

#endif
