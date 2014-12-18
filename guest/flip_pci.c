#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/fs.h>

#define KOBJ_NAME_LEN 20
#define FLIP_REG_LEN 4

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

#define FLIP_IO  0xF4
#define FLIP_CMD_DIR  _IOW(FLIP_IO, 1, int)

struct fifo_node {
	unsigned char data;
	struct fifo_node *next;
};

struct flip_fifo {
	struct fifo_node *front;
	struct fifo_node *rear;
};

struct flip_char {

	struct flip_fifo fifo_in;
	struct flip_fifo fifo_out;
	struct cdev cdev;
	struct semaphore sem;
};

struct flip_char *flip_char_dev;

static struct pci_device_id ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_REDHAT_QUMRANET, PCI_FLIP_DEVICE_ID),},
	{0,},
};

u64 ioport;
u32 io_len;
int flip_char_major = 0;
int flip_char_minor = 1;


static int flip_fifo_init(struct flip_fifo *fifo)
{
	fifo->front = fifo->rear = (struct fifo_node *)kmalloc(sizeof(struct fifo_node), GFP_KERNEL);
	if (fifo->rear == NULL)
		return -1;

	fifo->rear->next = NULL;

	return 0;
}


static int flip_fifo_is_empty(struct flip_fifo *fifo) {

	return fifo->front == fifo->rear;

}

/*
static int flip_fifo_is_full(struct flip_fifo *fifo) {
	return  (fifo->rear + 1) % DEFAULT_FIFO_SIZE == fifo->head;
}
*/

static int flip_fifo_get_data(struct flip_fifo *fifo, unsigned char *p)
{
	
	struct fifo_node *n;

	if (flip_fifo_is_empty(fifo))
		return  -1;

	n = fifo->front->next;
	*p = n->data;
	fifo->front->next = n->next;

	if (n == fifo->rear)
		fifo->rear = fifo->front;
	kfree(n);

	return 0;
}

		    
static int flip_fifo_put_data(struct flip_fifo *fifo, unsigned char ch)
{
	struct fifo_node *new;

	new = (struct fifo_node *)kmalloc(sizeof(struct fifo_node), GFP_ATOMIC);
	if (! new)
		return -1;
	
	new->data = ch;
	new->next = NULL;
	fifo->rear->next = new;
	fifo->rear = new;

	return 0;
}

static void flip_fifo_destroy(struct flip_fifo *fifo)
{
	unsigned char data;
	while (flip_fifo_get_data(fifo, &data) != -1);

}

static irqreturn_t flip_handler(int irq, void *dev_id)
{
	u16 device_id;
	u16 vendor_id;
	struct pci_dev *dev;
	u32 in;
	char data;
	int i,ret;


	dev = (struct pci_dev *)dev_id;
	pci_read_config_word(dev, PCI_DEVICE_ID, &device_id);

	pci_read_config_word(dev, PCI_VENDOR_ID, &vendor_id);

	if (!(vendor_id == PCI_VENDOR_ID_REDHAT_QUMRANET && device_id == PCI_FLIP_DEVICE_ID))
		return IRQ_NONE;
	
	printk("handle flip irq\n");
	in = inb(ioport + FLIP_REG_STATE);
	if ( in & FLIP_OUT_EMPTY)
		return IRQ_HANDLED;

	in = inl(ioport + FLIP_REG_OUT);

	if (down_trylock(&flip_char_dev->sem))
		return -ERESTARTSYS;
	
	//printk("write to fifo: %u\n", in);
	for (i = 0; i < FLIP_REG_LEN; i++) {
		data = (in >> i * 8) & 0xff;
		if (data == 0)
			goto fail;
		ret = flip_fifo_put_data(&flip_char_dev->fifo_out, data);
		if (ret < 0) {
			printk(KERN_ERR "flip fifo put data failed !\n");
			goto fail;
		}
	}

fail:	
	up(&flip_char_dev->sem);
	return IRQ_HANDLED;
}

static int flip_pci_probe(struct pci_dev *dev, const struct pci_device_id *ent)
{

	int ret;

	strncpy(dev->dev.kobj.name, "pci-flip", KOBJ_NAME_LEN);
	
	ret = pci_enable_device(dev);
	if (ret)
		return ret;

	if (dev->irq && request_irq(dev->irq, flip_handler, IRQF_SHARED, "pci-flip", dev)) {
		printk(KERN_ERR "pci-flip: IRQ %d not free\n", dev->irq);
		return -EIO;
	}
	if (dev->irq) {
		printk(KERN_INFO "pci-flip: IRQ = %d\n", dev->irq);
	}
	else 
		printk(KERN_INFO "pci-flip: no irq required!\n");

	
	ioport = pci_resource_start(dev, 0);
	io_len = pci_resource_len(dev, 0);
	if (io_len && !request_region(ioport, io_len, dev->dev.kobj.name)) {
		printk(KERN_ERR "can not get io_region for %s\n", dev->dev.kobj.name);
		goto cleanup_irq;
	}
	if (io_len) 
		printk(KERN_INFO "pci-flip: ioport len %d\n", io_len);
	else
		printk(KERN_INFO "pci-flip: ioport not needed!\n");
	
	return 0;

cleanup_irq:
	if (dev->irq)
		free_irq(dev->irq, dev);

	return -EIO;
}


static void flip_pci_remove(struct pci_dev *dev)
{
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (io_len)
		release_region(ioport, io_len);
}

static struct pci_driver flip_pci_driver = {
	.name = "pci-flip",
	.id_table = ids,
	.probe = flip_pci_probe,
	.remove = flip_pci_remove,
};

static int flip_char_open(struct inode *inode, struct file *flip)
{
	struct flip_char *dev;

	dev = container_of(inode->i_cdev, struct flip_char, cdev);

	flip->private_data = dev;

	return 0;
}



static int flip_char_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t flip_char_read(struct file *flip, char *buff, size_t count, loff_t *f_pos)
{
	struct flip_char *dev = flip->private_data;
	int i, ret;
	char *data;


	//printk("read: count = %d, pos = %lld\n", count, *f_pos);

	if (down_trylock(&dev->sem))
		return -ERESTARTSYS;

	data = (char *)kmalloc(count, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	i = 0;
	while (i < count) {
		if (flip_fifo_get_data(&dev->fifo_out, &data[i]) < 0)
			break;
		i++;
	}
	up(&dev->sem);
	
	if((ret = copy_to_user(buff, data, i))) {
		printk(KERN_ERR "copy_to_user error!\n");
	}
	
	kfree(data);
	f_pos -= i;

	return i - ret;
}

static ssize_t flip_char_write(struct file *flip, __user const char *buff, size_t count, loff_t *f_pos)
{
	int ret;
	u32 d;
	int i, j, n;
	char *data;

	//printk("write: count = %d, pos = %lld\n", count, *f_pos);

	ret = 0;

	data = (char *)kmalloc(count, GFP_KERNEL);
	if (!data)
		goto nomem;

	if ((ret = copy_from_user(data, buff, count))) {
		ret = -EFAULT;
		goto fail_copy;
	}
	
	*f_pos += count;
		
	i = 0;
	while (i < count) {
		d = 0;
		n = (count - i >= 4) ? 4 : count - i;
		for (j = 0; j < n; j++)
			d = data[i + j] << (8 * j) | d;
		
		printk("outl 0x%08x -> %p\n", d, ioport + FLIP_REG_IN);
		outl(d, ioport + FLIP_REG_IN);
		mdelay(1);
		
		i += n;
	}

fail_copy:

	kfree(data);
nomem:	

	return count - ret;
}

int flip_char_ioctl(struct file *flip, unsigned int cmd, unsigned long arg)
{
	int ret, dir;

	ret = 0;

	if (_IOC_TYPE(cmd) != FLIP_IO)
		return -ENOTTY;

	switch (cmd) {
	case FLIP_CMD_DIR:
		ret = __get_user(dir, (int  __user *) arg);
		outb(dir & 0x1, ioport + FLIP_REG_CONF);
		break;
	default:
		return -ENOTTY;
	}
	
	return ret;
}

static struct file_operations flip_char_ops = {
	.owner = THIS_MODULE,
	.read = flip_char_read,
	.write = flip_char_write,
	.unlocked_ioctl = flip_char_ioctl,
	.open = flip_char_open,
	.release = flip_char_close,
};

static int __init flip_pci_init(void)
{
	int ret = 0;
	dev_t dev = MKDEV(flip_char_major, 0);

	int devno;


	printk(KERN_INFO "pci-flip init!\n");
	
	if (flip_char_major)
		ret = register_chrdev_region(dev, 1, "flip-char");
	else {
		ret = alloc_chrdev_region(&dev, 0, 1, "flip-char");
		flip_char_major = MAJOR(dev);
	}

	if (ret < 0)
		return ret;
	
	flip_char_dev = kmalloc(sizeof(struct flip_char), GFP_KERNEL);
	if (!flip_char_dev) {
		ret = -ENOMEM;
		goto fail_char;
	}

	memset(flip_char_dev, 0, sizeof(struct flip_char));
	sema_init(&flip_char_dev->sem, 1);
	if ((ret = flip_fifo_init(&flip_char_dev->fifo_out)) < 0)
		goto fail_mem;

	
	devno = MKDEV(flip_char_major,0);
	cdev_init(&flip_char_dev->cdev, &flip_char_ops);
	flip_char_dev->cdev.owner = THIS_MODULE;

	ret = cdev_add(&flip_char_dev->cdev, devno, 1);
	if (ret) {
		goto fail_cdev;
	}

	return pci_register_driver(&flip_pci_driver);

fail_cdev:
	flip_fifo_destroy(&flip_char_dev->fifo_out);

fail_mem:
	kfree(flip_char_dev);

fail_char:
	unregister_chrdev_region(dev, 1);
	return ret;
}

static void __exit flip_pci_exit(void)
{
	pci_unregister_driver(&flip_pci_driver);
	cdev_del(&flip_char_dev->cdev);
	kfree(flip_char_dev);

	unregister_chrdev_region(MKDEV(flip_char_major,0), 1);
	printk("flip-pci: Bye!\n");
}

MODULE_DEVICE_TABLE(pci,ids);
MODULE_LICENSE("GPL");
module_init(flip_pci_init);
module_exit(flip_pci_exit);
