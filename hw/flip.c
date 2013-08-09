#include "flip.h"

#include "qemu/timer.h"
#include "exec/address-spaces.h"


#define PCI_VENDOR_ID_REDHAT_QUMRANET 0x1af4
#define PCI_FLIP_DEVICE_ID 0x10f0

#define FLIP_REG_CONF  0x0
#define FLIP_REG_STATE 0x1
#define FLIP_REG_IN    0x2
#define FLIP_REG_OUT   0x2 + FLIP_REG_LEN

#define FLIP_CONF_UP   0x0
#define FLIP_CONF_LOW  0x1
#define FLIP_IN_EMPTY  (0x1 << 1)
#define FLIP_OUT_EMPTY (0x1 << 2)


//#define FLIP_SIZE_MASK(x) ((1 << ((x * 8) + 1)) - 1)  

static void flip_update_irq(FLIPState *f)
{

	if(f->state & FLIP_OUT_EMPTY)
		qemu_irq_lower(f->irq);
	else
		qemu_irq_raise(f->irq);

}

static uint64_t flip_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
	FLIPState *f = opaque;
	uint64_t ret;
	int i,j;

	addr &= 0x7;


	switch(addr) {
	case FLIP_REG_CONF:
		ret = f->conf;
		break;
	case FLIP_REG_STATE:
		ret = f->state;
		break;
	case FLIP_REG_OUT:
		if (!size || size > 4)
			return 0;
		ret = 0xffffffff;
		// read from f->out
		if (!(f->state & FLIP_OUT_EMPTY)) {
			size = size > f->out_nr ? f->out_nr : size;
			i = FLIP_REG_LEN - f->out_nr;
			j = 0;
			ret = 0;
			do {
				ret = f->out[i + j] << (j * 8) | ret; 
				j++;
			}while (j < size);
			
			qemu_mutex_lock(&f->lock);

			f->out_nr -= size;

			if (f->out_nr == 0) {
				f->state |= FLIP_OUT_EMPTY;
			}
			
			qemu_mutex_unlock(&f->lock);
			flip_update_irq(f);

		}
		break;
	default:
		ret = 0xffffffff;
		
		break;
	}

	return ret;
}

void flip_ioport_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
	FLIPState *f = opaque;
	int i;

	addr &= 0x7;
	switch (addr) {
	case FLIP_REG_CONF:
		qemu_mutex_lock(&f->lock);
		f->conf = val & 0xff;
		qemu_mutex_unlock(&f->lock);
		break;
	case FLIP_REG_IN:
		if (!size)
			return;
		if (size > 4)
			size = 4;

		if (!(f->state & FLIP_IN_EMPTY))
			usleep(2000);

		qemu_mutex_lock(&f->lock);

		for (i = 0; i < size; i++) {
			f->in[i] = (val >> (i * 8)) & 0xff ;
		}

		f->in_nr = size;
		f->state &= ~FLIP_IN_EMPTY;

		qemu_mutex_unlock(&f->lock);
		qemu_mod_timer(f->flip_timer, qemu_get_clock_ns(vm_clock) + 2);
	
		break;
	default:
		break;
	}
	
}


const MemoryRegionOps flip_io_ops = {
	.read = flip_ioport_read,
	.write = flip_ioport_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
};

static void flip_reset(void *opaque)
{
	FLIPState *f = opaque;

	f->conf = FLIP_CONF_UP;
	f->state = FLIP_IN_EMPTY | FLIP_OUT_EMPTY;
	f->out_nr = 0;
	f->in_nr = 0;
	f->fliped_nr = 0;

	qemu_mutex_init(&f->lock);

	qemu_irq_lower(f->irq);
}


static void flip_callback(void *opaque)
{
	FLIPState *f = opaque;
	int i;
	int step;

	if (!(f->state & FLIP_IN_EMPTY)) {

		// wait for ISR to read away
		if (!(f->state & FLIP_OUT_EMPTY))
			usleep(2000);

		qemu_mutex_lock(&f->lock);

		// convert the character
		if (f->conf == FLIP_CONF_UP)
			step = -32;
		else 
			step = 32;
				       
		for (i = 0; i < f->in_nr; i++) {
			if (((f->in[i] >= 65 && f->in[i] <= 90) && step > 0)
			    || ((f->in[i] >= 97 && f->in[i] <= 122) && step < 0))
					
				f->out[i] = f->in[i] + step;
			else
				f->out[i] = f->in[i];
		}

		// update status
		f->out_nr = f->in_nr;
		f->in_nr = 0;
		f->fliped_nr += f->out_nr;
		f->state |= FLIP_IN_EMPTY;
		f->state &= ~FLIP_OUT_EMPTY;

		qemu_mutex_unlock(&f->lock);

		flip_update_irq(f);

	}
			

}
static int flip_pci_init(PCIDevice *dev)
{
	PCIFLIPState *pf = DO_UPCAST(PCIFLIPState, dev, dev);
	FLIPState *f = &pf->state;

	// connect to INTA
	pf->dev.config[PCI_INTERRUPT_PIN] = 0x01; // PIN A
	f->irq = pf->dev.irq[0];

	f->flip_timer = qemu_new_timer_ns(vm_clock, (QEMUTimerCB *)flip_callback, f);
	qemu_register_reset(flip_reset, f);
	memory_region_init_io(&f->io, &flip_io_ops, f, "flip", 16);
	pci_register_bar(&pf->dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &f->io);

	return 0;

}

static void flip_pci_exit(PCIDevice *dev)
{
	PCIFLIPState *pf = DO_UPCAST(PCIFLIPState, dev, dev);
	FLIPState *f = &pf->state;
	
	qemu_unregister_reset(flip_reset, f);
	memory_region_destroy(&f->io);
	
}


static void flip_pci_class_initfn(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
	pc->init = flip_pci_init;
	pc->exit = flip_pci_exit;
	pc->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
	pc->device_id = PCI_FLIP_DEVICE_ID;
	pc->revision = 1;
	pc->class_id = PCI_CLASS_SYSTEM_OTHER;

	dc->desc = "simple character flip device";
}

static const TypeInfo flip_pci_info = {
	.name           = "pci-flip",
	.parent         = TYPE_PCI_DEVICE,
	.instance_size  = sizeof(PCIFLIPState),
	.class_init     = flip_pci_class_initfn,
};

static void flip_pci_register_types(void)
{
	type_register_static(&flip_pci_info);
}

type_init(flip_pci_register_types)
