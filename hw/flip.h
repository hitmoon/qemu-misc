
/* a demo device */
/* how to use QOM */

#ifndef HW_FLIP_H
#define HW_FLIP_H

#include "hw.h"
#include "pci/pci.h"
#include "sysemu/sysemu.h"
#include "exec/memory.h"

#define FLIP_REG_LEN   4

typedef struct FLIPState{
	uint8_t conf;
	uint8_t state;
	uint64_t fliped_nr;

	uint8_t in_nr;
	uint8_t out_nr;

	uint8_t in[FLIP_REG_LEN];
	uint8_t out[FLIP_REG_LEN];

	MemoryRegion io;
	qemu_irq irq;

	QemuMutex lock;

	struct QEMUTimer *flip_timer;
}FLIPState;

typedef struct PCIFLIPState {
	PCIDevice dev;
	FLIPState state;
}PCIFLIPState;

extern const MemoryRegionOps flip_io_ops;

#endif
