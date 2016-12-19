#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>

/* KThread Header */
#include <linux/kthread.h>

/* Scheduler Header */
#include <linux/sched.h>

/* GPIO Header */
#include <linux/gpio.h>

/* Network headers */
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gustavo CIOTTO");
MODULE_DESCRIPTION("A module to receive synchronization packets.");
MODULE_VERSION("0.1");

static int set_priority(struct task_struct *task);

static unsigned int gpioLEDGreen = 60;
static unsigned int gpioLEDRed = 49;
static unsigned int gpioOscilloscope = 67;
static int init_GPIO(int gpio_id);
static void free_GPIO(int gpio_id);

#define PORT_NUMBER 561
static struct socket *sock_receive;
static struct sockaddr_in addr_receive;
static int init_network_server(void);
static void release_socket(void);
static int ksocket_receive(struct socket* sock, struct sockaddr_in* addr, unsigned char* buf, int len);

static struct task_struct *thread;
static void kthread_server_start(void);

#define MAX_TICKS 1

/* Thread that waits for UDP datagrams to send a pulse to
 * to the output pin */
static void kthread_server_start(void){

	char buffer[5];
	int size, change_state, counter = 0, onLEDRed = 0, tick_pressed = 0, tick_counter = 0;
	long int counter_received;

	printk(KERN_INFO "KThread Server started.\n");

	current->flags |= PF_NOFREEZE;

	allow_signal(SIGKILL);

	for (;;) {

		memset(&buffer, 0, 5);

		/* Receives a new UDP datagram */
		size = ksocket_receive(sock_receive, &addr_receive, buffer, 5);

		if (signal_pending(current))
			break;

		if (size < 0)
			printk(KERN_INFO ": error getting datagram, sock_recvmsg error = %d\n", size);
		else {

			/* State byte is the last one in the received string */
			change_state = buffer[4] - '0';

			buffer[4] = '\0';

			kstrtol(buffer, 10, &counter_received);

			if (tick_pressed)
				tick_counter++;

			if (tick_counter >= MAX_TICKS && tick_pressed) {

				gpio_set_value(gpioOscilloscope, 0);

				tick_pressed = 0;
				tick_counter = 0;
			}

			/* Generates pulse */
			if (change_state) {

				gpio_set_value(gpioLEDRed, onLEDRed);
				onLEDRed = !onLEDRed;

				gpio_set_value(gpioOscilloscope, 1);
				gpio_set_value(gpioOscilloscope, 0);
				tick_pressed = 0;
				tick_counter = 0;

				change_state = 0;

			}

			/* Refreshes other pins' states */
			if (counter_received % 2) gpio_set_value(gpioLEDGreen, 0);
			else gpio_set_value(gpioLEDGreen, 1);

			counter++;
			if (counter > 1000)
				counter = 0;
		}


	}

}

/* Sends a UDP datagram to the given address and returns its size */
static int ksocket_receive(struct socket* sock, struct sockaddr_in* addr, unsigned char* buf, int len) {

	struct msghdr msg;
	struct iovec iov;
	int size = 0;

	if (sock->sk==NULL) return 0;

	iov.iov_base = buf;
	iov.iov_len = len;

	msg.msg_flags = 0;
	msg.msg_name = addr;
	msg.msg_namelen  = sizeof(struct sockaddr_in);
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;

	size = sock_recvmsg(sock,&msg,len,msg.msg_flags);

	return size;
}

/* Releases socket */
static void release_socket() {

	printk (KERN_INFO "Releasing network socket...\n");
	sock_release(sock_receive);
	sock_receive = NULL;
	printk (KERN_INFO "Network socket released...\n");

}

/* Free GPIO pin */
static void free_GPIO(int gpio_id) {

	printk (KERN_INFO "Unexporting GPIO #%d...\n", gpio_id);
	gpio_unexport(gpio_id);
	gpio_free(gpio_id);
	printk (KERN_INFO "GPIO #%d freed successfully.\n", gpio_id);

}

/* Inits GPIO pin as output */
static int init_GPIO(int gpio_id) {

	int result, state;

	state = 0;

	printk(KERN_INFO "Initializing the GPIO #%d...\n", gpio_id);

	if (!gpio_is_valid(gpio_id)){
		printk(KERN_INFO "GPIO_TEST: invalid GPIO\n");
		return -ENODEV;
	}

	result = gpio_request(gpio_id, "sysfs");
	gpio_direction_output(gpio_id, state);
	result = gpio_export(gpio_id, true);

	printk(KERN_INFO "Initializing the GPIO #%d succeed.\n", gpio_id);

	return result;

}

/* Sets the priority of the given task to SCHED_FIFO with the highest priority  */
static int set_priority(struct task_struct *task) {

	struct sched_param sched_parameter;
	int result;

	sched_parameter.sched_priority = MAX_RT_PRIO - 1;
	printk(KERN_INFO "Old policy %d\n", task->policy);

	/* Changes task policy to SCHED_FIFO */
	if ( (result = sched_setscheduler(task, SCHED_FIFO, &sched_parameter)) ) {
		printk(KERN_INFO "Set scheduler failed with error #%d\n.",result);
		return -result;
	}

	printk(KERN_INFO "Current PID #%d\n",task->pid);
	printk(KERN_INFO "Current (SCHED_FIFO = %d) policy %d\n",SCHED_FIFO, task->policy);

	return 0;

}

/* Inits network socket and sets ip address, port and mask */
static int init_network_server(void) {

	int err;

	printk(KERN_INFO "Initializing Network...\n");

	/* Creates a socket */
	if ( (err = sock_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock_receive)) < 0 ){

		printk(KERN_INFO "Could not create a datagram socket, error = %d\n", -ENXIO);
		return -ENXIO;
	}

	/* Sets ip address, port and socket type */
	memset(&addr_receive, 0, sizeof(struct sockaddr));
	addr_receive.sin_family = AF_INET;
	addr_receive.sin_addr.s_addr = htonl(INADDR_ANY);
	addr_receive.sin_port = htons(PORT_NUMBER);

	printk(KERN_INFO "Starting connection to the socket...\n");

	/* Binds the address and port */
	if ((err = sock_receive->ops->bind(sock_receive, (struct sockaddr *) &addr_receive,
			sizeof(struct sockaddr)) ) < 0 ){

		printk(KERN_INFO "Could not bind or connect to socket, error = %d\n", err);
		return err;

	}

	printk(KERN_INFO "Socket created successfully.\n");

	return 0;
}


/* Inits all pins, timer and threads used by this module. */
static int __init server_module_init(void){

	int result_kthread, result_network, result_gpio;

	result_network = init_network_server();

	if (result_network) {
		printk(KERN_INFO "Network initialization failed.\n");
		return result_network;
	}

	result_gpio = init_GPIO(gpioLEDGreen);

	if (result_gpio) {
		printk(KERN_INFO "GPIO %d initialization failed.\n", gpioLEDGreen);
		return result_gpio;
	}

	result_gpio = init_GPIO(gpioLEDRed);

	if (result_gpio) {
		printk(KERN_INFO "GPIO %d initialization failed.\n", gpioLEDRed);
		return result_gpio;
	}

	result_gpio = init_GPIO(gpioOscilloscope);

	if (result_gpio) {
		printk(KERN_INFO "GPIO %d initialization failed.\n", gpioOscilloscope);
		return result_gpio;
	}


	/* Creates new thread */
	thread = kthread_create((void *)kthread_server_start, NULL, "KThread_server");
	result_kthread = set_priority(thread);

	if (result_kthread || IS_ERR(thread)) {

		printk(KERN_INFO "KThread initialization failed.\n");
		return -(result_kthread | IS_ERR(thread));
	}

	wake_up_process(thread);

	return 0;

}

/* Free all ressources allocated by the module. */
static void __exit server_module_exit(void){

	release_socket();

	free_GPIO(gpioLEDGreen);
	free_GPIO(gpioLEDRed);
	free_GPIO(gpioOscilloscope);

}

module_init(server_module_init);
module_exit(server_module_exit);

