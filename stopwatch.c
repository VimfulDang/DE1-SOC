#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "../address_map_arm.h"
#include "../interrupt_ID.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dang Nguyen");
MODULE_DESCRIPTION("DE1SoC 100MHz MINUTES:SECONDS:HUNDREDTH_SECONDS Stopwatch");
MODULE_VERSION("0.01");

/*
  Kernel Driver implemented on DE1-SOC for stopwatch
  Writing MINUTE:SECONDS:HUNDREDTH_SECOND in form of (MM:SS:DD) to module to set the timer
  Read returns the current time
  Current time also displayed on (6) 7-Segment Displays.

*/

#define SUCCESS 0
#define DEVICE_NAME "stopwatch"
#define CENTISECOND_PER_MINUTE 6000
#define CENTISECOND_PER_SECOND 100

/* Kernel character device driver /dev/chardev. */
static int device_open (struct inode * inode, struct file * file);
static int device_release (struct inode * inode, struct file * filp);
static ssize_t stopwatch_read (struct file * filp, char * buffer, size_t length, loff_t *offset);
static ssize_t stopwatch_write (struct file * filp, const char * buffer, size_t length, loff_t *offset);

/* Module Function Prototypes */
static int bcd(int hexPosition, int binaryInput);
static void hex_timer_update(void);
static irq_handler_t irq_timer_handler(int irq, void * dev_id, struct pt_regs *reg);
static void config_timer(volatile int * timer_base_ptr,int start_value, int cont, int interrupt);
static int __init init_stopwatch(void);
static void __exit stop_stopwatch(void);
static void timer_update (char * arr);
static int get_command(char * arr, int len);

/* Module Pointer */
char reg_write[256];
static void *LW_virtual;
static volatile int *TIMER0_ptr, *HEX30_ptr, *HEX54_ptr; //Pointers to hardware register
static unsigned int timer_count = 359999;
static unsigned ind_read, ind_write, disp;
static char * write_command[4] = {"stop", "run", "disp", "nodisp"}; 

static struct hex_timer {
	int minute;
	int second;
	int hundredth_second;
} hex_timer0;

/* Kernel Pointer */

static dev_t stopwatch_no = 0;
static struct cdev * stopwatch_cdev = NULL;
static struct class * stopwatch_class = NULL;

static struct file_operations stopwatch_fops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.release = device_release,
	.read = stopwatch_read,
	.write = stopwatch_write
};


/* Configure Timer Control Register */
static void config_timer(volatile int * timer_base_ptr, int start_value, int cont, int interrupt) {
	
	//*(timer_base_ptr) = 0x0; // status
	*(timer_base_ptr + 2) = (0x0000FFFF & start_value); //low START
	*(timer_base_ptr + 3) = (0x0000FFFF & (start_value >> 16)); //high START
	*(timer_base_ptr + 1) = (0x4 | (cont << 1) | interrupt); //[Unused|Stop|Start|Cont|Interrupt];
	printk("Timer Control: %#x\n", *(timer_base_ptr + 1));
}

/* Hardware Clock timer0, 100 MHz */

static irq_handler_t irq_timer_handler(int irq, void * dev_id, struct pt_regs *reg) {
	if (timer_count > 0) 
		timer_count--;
	else
		*(TIMER0_ptr + 1) = 0xB;
	hex_timer_update();
	*(TIMER0_ptr) &= 0xFFFFFFFE;
	return (irq_handler_t) IRQ_HANDLED;
}

/* Convert int timer_count to seconds */
static void hex_timer_update(void) {

	hex_timer0.minute = timer_count / CENTISECOND_PER_MINUTE;
	hex_timer0.second = (timer_count - (hex_timer0.minute * CENTISECOND_PER_MINUTE)) / CENTISECOND_PER_SECOND;
	hex_timer0.hundredth_second = timer_count % 100;

	//Set maximum value to 60:60:99
	hex_timer0.minute = (hex_timer0.minute > 60) ? 60 : hex_timer0.minute;
	hex_timer0.second = (hex_timer0.second > 60) ? 60: hex_timer0.second;

	*HEX54_ptr = 0;
	*HEX30_ptr = 0;
	if (disp) {
		//MM
		*HEX54_ptr = (bcd(1, (hex_timer0.minute / 10 ) % 10)) 
		| (bcd(0, hex_timer0.minute % 10));

		//SS & DD
		*HEX30_ptr = (bcd(3, (hex_timer0.second / 10) % 10)) 
		| (bcd(2, hex_timer0.second % 10))
		| (bcd(1, (hex_timer0.hundredth_second / 10) % 10)) 
		| (bcd(0, (hex_timer0.hundredth_second) % 10));
	}
}

/* Binary to Decimal conversion for 7-Segment Display */
static int bcd(int hexPosition, int binaryInput) {
	int segment_value = 0xFFFFFFFF;

	switch (binaryInput) {
		case 0 : segment_value = 0x3F; //0011_1111
				break;
		case 1 : segment_value = 0x06; //0000_0110
				break;
		case 2 : segment_value = 0x5B; //0101_1011
				break;
		case 3 : segment_value = 0x4F; //0100_1111
				break;
		case 4 : segment_value = 0x66; //0110_0110
				break;
		case 5 : segment_value = 0x6D; //0110_1101
				break;
		case 6 : segment_value = 0x7D; //0111_1101
				break;
		case 7 : segment_value = 0x07; //0000_0111
				break;
		case 8 : segment_value = 0x7F; //0111_1111
				break;
		case 9 : segment_value = 0x67; //0110_0111
				break;
		default : segment_value = 0xFFFFFFFF;
	}
	segment_value = (segment_value << (hexPosition * 8));
	return segment_value;
}

static int __init init_stopwatch(void) {

	int err = 0;
	int timer0_value = 1;
	//allocate driver no
	if ((err = alloc_chrdev_region (&stopwatch_no, 0, 1, DEVICE_NAME)) < 0) {
		printk(KERN_ERR "chardev: alloc_chrdev_region() error %d\n", err);
		return err;
	}

	stopwatch_class = class_create(THIS_MODULE, DEVICE_NAME);

	stopwatch_cdev = cdev_alloc();
	stopwatch_cdev->ops = &stopwatch_fops;
	stopwatch_cdev->owner = THIS_MODULE;
	//Add the character device to the kernel
	if ((err = cdev_add(stopwatch_cdev, stopwatch_no, 1)) < 0) {
		printk(KERN_ERR "chardev: cdev_add() error %d\n", err);
		return err;
	}

	device_create(stopwatch_class, NULL, stopwatch_no, NULL, DEVICE_NAME);

	//Stopwatch Hardware Initialize
	printk("Initializing stopwatch\n");

	//Virtual address for Light-weight Bridge
	LW_virtual = ioremap_nocache(LW_BRIDGE_BASE, LW_BRIDGE_SPAN);
	TIMER0_ptr = LW_virtual + TIMER0_BASE;
	HEX30_ptr = LW_virtual + HEX3_HEX0_BASE;
	HEX54_ptr = LW_virtual + HEX5_HEX4_BASE;

	//Initialize variables
	hex_timer0.minute = 0;
	hex_timer0.second = 0;
	hex_timer0.hundredth_second = 0;
	*HEX30_ptr = 0;
	*HEX54_ptr = 0;
	disp = 1;
	hex_timer_update();

	//cfg timer parallel port
	config_timer(TIMER0_ptr, 1000000, 1, 1);

	timer0_value = request_irq(INTERVAL_TIMER_IRQ, (irq_handler_t) irq_timer_handler, 
		IRQF_SHARED, "timer0_irq_handler", (void *) (irq_timer_handler));

	return timer0_value;

}

//__exit
static void __exit stop_stopwatch(void) {	

	device_destroy(stopwatch_class, stopwatch_no);
	cdev_del(stopwatch_cdev);
	class_destroy(stopwatch_class);
	unregister_chrdev_region(stopwatch_no, 1);

	*HEX30_ptr = 0;
	*HEX54_ptr = 0;

	*(TIMER0_ptr + 1) &= 0;
	hex_timer0.minute = 0;
	hex_timer0.second = 0;
	hex_timer0.hundredth_second = 0;
	timer_count = 0;

	free_irq (INTERVAL_TIMER_IRQ, (void *) irq_timer_handler);	
}

static int device_open(struct inode * inode, struct file * file) {
	return SUCCESS;
}

static int device_release(struct inode * inode, struct file * file) {
	return 0;
}

static ssize_t stopwatch_read (struct file * filp, char * buffer, size_t length, loff_t *offset) {
	if (!ind_write) {
		sprintf(reg_write, "%02d:%02d:%02d\n", hex_timer0.minute, hex_timer0.second, hex_timer0.hundredth_second);
	}
	if ((reg_write[ind_write] != '\0') && length) {
		put_user(reg_write[ind_write++], buffer++);
		length--;
	}
	else
		ind_write = 0;
	return ind_write;
}

/* Check against commands or MM:SS:DD format */
static int get_command(char * arr, int len) {
	int i = 0;
	int command_ind = -1;
	//Check for commands
	for( i = 0; i < 4; i++) {
		if (!(strcmp(write_command[i], arr))) {
			command_ind = i;
			i = 5;
		}
	}
	//Check for MM:SS:DD format if not found command
	if ((command_ind < 0) && (len == 8) && (i = 5)) {
		for (i = 0; i < 8; i++) {
			if ((*(arr + i) < 47) || (*(arr + i) > 59))
				i = 9;
			}

		if (i == 8) 
			command_ind = 4;
	}
	return command_ind;
}

/* Change timer */

static void timer_update (char * arr) {

	char minstr[3], secstr[3], hunSecStr[3];
	unsigned int min, sec, hunSec, tempStatus;

	//stop Stopwatch
	tempStatus = *(TIMER0_ptr);
	*(TIMER0_ptr + 1) = 0xB;
	timer_count = 0;

	//Format input
	minstr[0] = *(arr);
	minstr[1] = *(arr + 1);
	minstr[2] = '\0';
	secstr[0] = *(arr + 3);
	secstr[1] = *(arr + 4);
	secstr[2] = '\0';
	hunSecStr[0] = *(arr + 6);
	hunSecStr[1] = *(arr + 7);
	hunSecStr[2] = '\0';

	//convert to int
	kstrtouint(minstr, 10, &min);
	kstrtouint(secstr, 10, &sec);
	kstrtouint(hunSecStr, 10, &hunSec);

	//to count format
	timer_count = min * 6000;
	timer_count += sec * 100;
	timer_count += hunSec;
	printk("min: %d, sec: %d, hunSec: %d, timer_count: %d\n", min, sec, hunSec, timer_count);
	printk("regRead: %s, minStr: %s, secStr: %s, hunSecStr: %s\n", arr, minstr, secstr, hunSecStr);
	hex_timer_update();

  	// Start timer
	*(TIMER0_ptr + 1) = tempStatus;

}

static ssize_t stopwatch_write (struct file * filp, const char * buffer, size_t length, loff_t *offset) {
	char regRead[256];
	int command = 10;

	//Read buffer input
	if (length < 256) {
		for(ind_read = 0; ind_read < length-1; ind_read++) {
			regRead[ind_read] = buffer[ind_read];
		}
		regRead[ind_read++] = '\0';
	}

	//Get command
	command = get_command(regRead, strlen(regRead));
	switch (command) {

		case(0) : printk("stop\n");
				*(TIMER0_ptr + 1) = 0xB;
				break;
		case(1) : printk("run\n");
				*(TIMER0_ptr + 1) = 0x7;
				break;
		case(2) : printk("disp\n");
				disp = 1;
				hex_timer_update();
				break;
		case(3) : printk("nodisp\n");
				disp = 0;
				hex_timer_update();
				break;
		case(4) : printk("New Time\n");
				timer_update(regRead);
				break;
		default : printk("Not a valid command\n");
	}
	return ind_read;
}

module_init (init_stopwatch);
module_exit (stop_stopwatch);
