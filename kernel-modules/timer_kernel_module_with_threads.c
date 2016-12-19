#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/sched.h>

/* KThread Header */
#include <linux/kthread.h>
#include <linux/wait.h>

/* GPIO Header */
#include <linux/gpio.h>

/* DMTimer Header */
#include <plat/dmtimer.h>

/* Network headers */
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gustavo CIOTTO");
MODULE_DESCRIPTION("A module to control timer interruptions.");
MODULE_VERSION("0.1");

/* Real-time Priority */
static int set_priority(struct task_struct *task, int priority, int policy);
static struct task_struct *thread, *thread_int;
static void kthread_start(void);
static void kthread_int_start(void);
wait_queue_head_t my_queue;


#define MAX_ELEMENT 65536
struct buff_element {
	int count;
	int send_change_cmd_flag;
};
struct buff_element circular_buffer [MAX_ELEMENT];
static int start = 0, end = 0;


/* GPIO-related variables */
static unsigned int gpioButton = 48;
static unsigned int gpioState = 68;
static unsigned int irq_gpio_id;
static irqreturn_t gpio_irq_handler( int irq, void *dev_id);
static int init_GPIO_as_input(int gpio_id);
static int init_GPIO_as_output(int gpio_id);

/* DMtimer-related variables */
#define PTV_DM_TIMER 	0x01		/* Valor do prescaler - frequencia sera multiplicada por 2 ^ (1 + 1) */
//#define LDR_DM_TIMER	0xFFFFF9BF 	/* tick each 100ms */
#define LDR_DM_TIMER	0xFFFFFF5F 	/* Valor a ser carregado no registrador de LOAD -  interrupcoes a cada 10ms */
//#define LDR_DM_TIMER	0xFFFFFFEF
//#define PTV_DM_TIMER 	0x04
//#define LDR_DM_TIMER	0xFFFFFC17	/* Valor a ser carregado no registrador de LOAD -  interrupcoes a cada 1s */
#define MAX_TICKS 1					/* Constante que define o numero de interrupcoes que o
									   o sinal de saida do GPIO67 permanecera alto apos um evento
									   de trigger (apertar um botao ou sinal de PWM, por exemplo */

static int send_cmd_count = 0;
static int counter, tick_counter, tick_pressed, state; /* Variaveis que controlarao o estado do GPIO67  */
static int ready = 0;		/* Indica que o DMTimer foi totalmente configurado */
static int timer_id = 5;	/* Identificador do DMTimer. Por padrao, sera 5. */
static int irq_id;			/* Identificador da interrupcao IRQ associado ao DMTimer */
module_param(timer_id, int, S_IRUGO);
static struct omap_dm_timer *timer = NULL;

/* DMTimer function prototypes*/
static irqreturn_t dmtimer_irq_handler( int irq, void *dev_id);
static int init_DMTimer(void);

/* Network-related variables */
#define DEFAULT_PORT 2325
#define CONNECT_PORT 561
#define INADDR_SEND ((unsigned long int)0xc0a80216) 			/* 192.168.2.22 */
#define INADDR_BROADCAST_NET ((unsigned long int)0xc0a802ff)	/* 192.168.2.255 */
//#define INADDR_SEND ((unsigned long int)0xc0a80201) 			/* 192.168.2.1 */

static int send_change_cmd = 0, isReady_to_End, canEnd;
static struct socket *sock_send;
static struct sockaddr_in addr_send;
static int ksocket_send(unsigned char *buf, int len);
static int init_network(void);
static void release_socket(void);

/* Thread which initializes resources */
static void kthread_int_start(void) {

	int result_timer, result_network, result_gpio;

	printk(KERN_INFO "KThread Interruption started.\n");

	/* Init socket*/
	result_network = init_network();

	if (result_network) {
		printk(KERN_INFO "Network initialization failed.\n");
		return;
	}

	result_gpio = init_GPIO_as_output(gpioState);

	if (result_gpio) {
		printk(KERN_INFO "GPIO 67 initialization failed.\n");
		return;
	}

	result_gpio = init_GPIO_as_input(gpioButton);

	if (result_gpio) {
		printk(KERN_INFO "GPIO 48 initialization failed.\n");
		return;
	}

	printk(KERN_INFO "GPIO initialization succeeded.\n");

	/* Init timer to pulse with 1ms period */
	result_timer = init_DMTimer();

	if (result_timer) {
		printk(KERN_INFO "DMTimer initialization failed.\n");
		return;
	}

	printk(KERN_INFO "Initialization KThread exit.\n");

}

/* Starts thread that sends packets to the server  */
static void kthread_start(void) {

	int result;
	char buffer[6];

	printk(KERN_INFO "KThread client started.\n");

	while(!isReady_to_End) {

		if (signal_pending(current))
			break;

		/* Verifies that circular buffer is not empty */
		if (start != end) {

			struct buff_element b_elem = circular_buffer[start];
			start = (start + 1) % MAX_ELEMENT;

			/* Constroi buffer para envio.  */
			sprintf(buffer, "%04d%d", b_elem.count, b_elem.send_change_cmd_flag);

			if ((result = ksocket_send(buffer, strlen(buffer))) < 0)
				printk(KERN_INFO "Sending UDP packet failed .\n");

		}
		/* Circular buffer is empty. Waits for a event to wake up this thread */
		else wait_event(my_queue, (start != end) );

	}

	canEnd = 1;
	printk(KERN_INFO "KThread client exit.\n");

}

/* Changes a thread policy and priority */
static int set_priority(struct task_struct *task, int priority, int policy){

	struct sched_param sched_parameter;
	int result;

	sched_parameter.sched_priority = priority;
	printk(KERN_INFO "Old policy %d\n", task->policy);

	if ( (result = sched_setscheduler(task, policy, &sched_parameter)) ) {
		printk(KERN_INFO "Set scheduler failed with error #%d\n.",result);
		return -result;
	}

	printk(KERN_INFO "Current PID #%d\n",task->pid);
	printk(KERN_INFO "Current (SCHED_FIFO = %d) policy %d\n",SCHED_RR, task->policy);

	return 0;

}


/* init_GPIO48 initializes and prepares pin P9_15 as input.
   Such pin is the trigger source for state changes. */
static int init_GPIO_as_input(int gpio_id) {

	int result;

	tick_pressed = 0;

	printk(KERN_INFO "Initializing the GPIO #%d...\n", gpio_id);

	if (!gpio_is_valid(gpio_id)){
		printk(KERN_INFO "GPIO_TEST: invalid LED GPIO\n");
		return -ENODEV;
	}

	/* Reserves pin */
	gpio_request(gpio_id, "sysfs");
	gpio_direction_input(gpio_id);
	gpio_set_debounce(gpio_id, 200);
	gpio_export(gpio_id, true);

	irq_gpio_id = gpio_to_irq(gpio_id);

	printk(KERN_INFO "The button is mapped to IRQ: %d\n", irq_gpio_id);

	/* Sets IRQ handler for interruptions */
	result = request_irq(irq_gpio_id,   // The interrupt number requested
			gpio_irq_handler, 			// The pointer to the handler function below
			IRQF_TRIGGER_RISING,   		// Interrupt on rising edge (button press, not release)
			"gpio48_handler",
			NULL);

	printk(KERN_INFO "GPIO_TEST: The interrupt request result is: %d\n", result);
	return result;

}

/* init_GPIO67 configures pin P8_10 as output. This pin will be used to
 * show a state change, that is, a rising edge was detected. */
static int init_GPIO_as_output(int gpio_id) {

	int result;

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

/* Handler function generated when a rising edge is detected on pin P9_15. */
static irqreturn_t gpio_irq_handler( int irq, void *dev_id) {

	//printk(KERN_INFO "GPIO_TEST: Interrupt! (button state is %d)\n", gpio_get_value(gpioButton));

	/* Refreshes variables which indicate that event has happened */
	send_change_cmd = 1;
	tick_counter = MAX_TICKS;
	tick_pressed = 1;

	return IRQ_HANDLED;

}

/* Initialiazes resources used by this module */
static int __init dmtimer_module_init(void){

	int  result_kthread;

	isReady_to_End = 0;
	canEnd = 0;

	thread_int = kthread_create((void *)kthread_int_start, NULL, "KThread_interruption_client");
	result_kthread = set_priority(thread_int, MAX_RT_PRIO - 2, SCHED_FIFO);

	init_waitqueue_head(&my_queue);

	if (IS_ERR(thread_int)) {

		printk(KERN_INFO "KThread initialization failed.\n");
		return -IS_ERR(thread);
	}

	wake_up_process(thread_int);

	thread = kthread_create((void *)kthread_start, NULL, "KThread_client");
	result_kthread = set_priority(thread, MAX_RT_PRIO - 3, SCHED_FIFO);

	if (IS_ERR(thread)) {

		printk(KERN_INFO "KThread initialization failed.\n");
		return -IS_ERR(thread);
	}

	wake_up_process(thread);

	return 0;
}

/* Free all resources used by the module. */
static void __exit dmtimer_module_exit(void){

	int result;

	send_sig(SIGKILL, thread_int, 1);
	send_sig(SIGKILL, thread, 1);

	printk (KERN_INFO "Freeing DMTimer #%d...", timer_id);

	result = omap_dm_timer_free(timer);
	free_irq(irq_id, NULL);

	if (result)
		printk (KERN_INFO "Freeing DMTimer #%d failed with code %d.\n", timer_id, result);
	else printk (KERN_INFO "DMTimer #%d has been successfully freed.\n", timer_id);

	release_socket();

	printk (KERN_INFO "Unexporting GPIO #%d...\n", gpioButton);
	free_irq(irq_gpio_id, NULL);
	gpio_unexport(gpioButton);
	gpio_free(gpioButton);
	printk (KERN_INFO "GPIO #%d freed successfully.\n", gpioButton);

	printk (KERN_INFO "Unexporting GPIO #%d...\n", gpioState);
	gpio_unexport(gpioState);
	gpio_free(gpioState);
	printk (KERN_INFO "GPIO #%d freed successfully.\n", gpioState);


	printk (KERN_INFO "Trying to kill thread.\n");
	isReady_to_End = 1;
	start = end + 1;
	wake_up(&my_queue);

	printk (KERN_INFO "Thread killed.\n");



}

/* Releases socket used in packets transmission. */
static void release_socket() {

	printk (KERN_INFO "Releasing network socket...\n");
	sock_release(sock_send);
	sock_send = NULL;
	printk (KERN_INFO "Network socket released...\n");

}

/* Function which handles DMTimer interruptions. Sends a packet to server
 * each timer it is called. */
static irqreturn_t dmtimer_irq_handler( int irq, void *dev_id) {

	int status = 0;

	//printk(KERN_INFO "IRQ interruption captured.\n");

	/* Read the current Status */
	status = omap_dm_timer_read_status(timer);

	/* Clear the timer interrupt */
	if (status == OMAP_TIMER_INT_OVERFLOW)	{

		//printk(KERN_INFO "Sending UDP packet... \n");
		omap_dm_timer_write_status(timer,  OMAP_TIMER_INT_OVERFLOW);

		if (ready) {

			struct buff_element b_elem;

			b_elem.count = counter;
			b_elem.send_change_cmd_flag = send_change_cmd;

			circular_buffer[end] = b_elem;
			end = (end + 1) % MAX_ELEMENT;

			wake_up(&my_queue);

			/* Se um evento de borda de subida foi detectada, muda o estado do pino
			 * P8_10. */
			if (send_change_cmd) {
				send_cmd_count++;
				send_change_cmd = 0;
				gpio_set_value(gpioState, 1);
			}

			counter++;
			if (counter > 1000)
				counter = 0;

			if (tick_pressed) {

				if (tick_counter) tick_counter--;

				/* Verifica a quantidade de interrupcoes que passaram a fim de reverter
				 * novamente o estado do pino P8_10. */
				if (tick_counter < 0) {

					gpio_set_value(gpioState, 0);

					//printk(KERN_INFO "state = %d.\n", state);
					//printk(KERN_INFO "state changed at tick = %d.\n", counter);
					tick_pressed = 0;
				}
			}
		}

	}

	return IRQ_HANDLED;
}

/* Allocates resources to send UDP packets to the network. */
static int init_network() {

	int err;

	printk(KERN_INFO "Initializing Network...\n");

	/* create a socket */
	if ( (err = sock_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock_send)) < 0 ){

		printk(KERN_INFO "Could not create a datagram socket, error = %d\n", -ENXIO);
		return -ENXIO;
	}

	/* Enables broadcasting
	 * if ((err = sock_send->ops->setsockopt(sock_send, SOL_SOCKET, SO_BROADCAST, (char *) &broadcast, sizeof(int))) < 0) {
		printk("Error, sock_setsockopt(SO_BROADCAST) failed!\n");
		return err;
	}*/

	memset(&addr_send, 0, sizeof(struct sockaddr));
	addr_send.sin_family = AF_INET;
	addr_send.sin_addr.s_addr = htonl(INADDR_SEND);
	addr_send.sin_port = htons(CONNECT_PORT);

	printk(KERN_INFO "Starting connection to the socket...\n");

	if ((err = sock_send->ops->connect(sock_send, (struct sockaddr *) &addr_send,
			sizeof(struct sockaddr), 0) ) < 0 ){

		printk(KERN_INFO "Could not bind or connect to socket, error = %d\n", err);
		return err;

	}

	printk(KERN_INFO "Connection to the socket set.\n");

	printk(KERN_INFO "Socket created successfully.\n");

	return 0;
}

/* Function that builds and sends UDP datagrams. */
static int ksocket_send(unsigned char *buf, int len)
{
	struct msghdr msg;
	struct iovec iov;
	int size = 0;

	if (sock_send->sk==NULL)
		return 0;

	iov.iov_base = buf;
	iov.iov_len = len;

	msg.msg_flags = 0;
	msg.msg_name = &addr_send;
	msg.msg_namelen  = sizeof(struct sockaddr_in);
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;

	size = sock_sendmsg(sock_send,&msg,len);

	return size;
}

/* Initialization of this module. */
static int init_DMTimer() {

	int result_source, result_PRESCALER, result_TLDR, result_ITR,
	result_RQS_ITR, result_START, result;

	counter = 0;
	tick_counter = MAX_TICKS;
	ready = 0;

	printk (KERN_INFO "Initializing DMTimer...\n");

	/* Allocate DMtimer */

	if ((timer = omap_dm_timer_request()) == NULL) {

		printk(KERN_INFO "LKM_TIMER_MODULE: impossible to request dmtimer #%d\n", timer_id);
		return -ENODEV;
	}

	omap_dm_timer_disable(timer);

	timer_id =  timer->id;

	printk(KERN_INFO "DMTimer chosen %d", timer_id);

	//omap_dm_timer_stop(timer);

	/* Set timer clock source */

	if ((result_source = omap_dm_timer_set_source(timer, OMAP_TIMER_SRC_32_KHZ ))) {

		omap_dm_timer_free(timer);
		printk(KERN_INFO "LKM_TIMER_MODULE: impossible to set clock source (%d).\n", result_source);
		return -result_source;
	}

	printk(KERN_INFO "DMTimer clock source set.\n");

	/* Set PTV prescaler value. */

	result_PRESCALER = omap_dm_timer_set_prescaler(timer, PTV_DM_TIMER);
	printk(KERN_INFO "DMTimer prescaler (enabled %d) set to 0x%02x.\n",
			(timer->context.tclr & (1 << 5)) >> 5, (timer->context.tclr & 0x1C) >> 2);

	/* Define TLDR register value according to the interruption period desired and specified
	 * by the equation (FFFF FFFFh - TLDR + 1)* timer period * 2^(PTV + 1)
	 * PTV is the prescaler factor */

	result_TLDR = omap_dm_timer_set_load_start(timer, 1, LDR_DM_TIMER);
	printk(KERN_INFO "DMTimer load value set to 0x%02x.\n",	timer->context.tldr);
	printk(KERN_INFO "DMTimer autoreload flag (TCLR AR bit) set to %d.\n",	(timer->context.tclr & 0x2) >> 1);

	if ((result_PRESCALER) || (result_TLDR) ) {

		omap_dm_timer_free(timer);
		printk(KERN_INFO "LKM_TIMER_MODULE: impossible to configure prescaler (%d) or load (%d).\n",
				result_PRESCALER, result_TLDR);
		return -result_PRESCALER - result_TLDR ;
	}


	/* Determine which IRQ the timer triggers */
	irq_id = omap_dm_timer_get_irq(timer);


	printk(KERN_INFO "DMTimer IRQ id 0%d.\n", irq_id);

	/* This next call requests an interrupt line */
	result_RQS_ITR = request_irq(irq_id,            // The interrupt number requested
			dmtimer_irq_handler, 					// The pointer to the handler function below
			IRQF_TIMER,
			"dmtimer_irq_handler",
			NULL);

	omap_dm_timer_enable(timer);

	printk (KERN_INFO "DMTimer #%d almost there... (TCLR ST flag = %d).\n",
			timer_id, timer->context.tclr & 0x1);

	result_ITR = omap_dm_timer_set_int_enable(timer, OMAP_TIMER_INT_OVERFLOW );

	printk(KERN_INFO "DMTimer IRQ Enable flag (IRQENABLE_SET OVF_IT_FLAG) set id %d.\n",
			(timer->context.tier & 0x2) >> 1);

	if (result_ITR || result_RQS_ITR) {

		omap_dm_timer_free(timer);
		printk(KERN_INFO "LKM_TIMER_MODULE: impossible to configure interruption handler (%d or %d) .\n",
				result_ITR, result_RQS_ITR);
		return -result_ITR - result_RQS_ITR ;

	}

	result_START =  omap_dm_timer_start(timer);
	if (result_START) {

		omap_dm_timer_free(timer);
		printk(KERN_INFO "LKM_TIMER_MODULE: DMTimer could not be started (%d).\n",
				result_START);
		return -result_START ;

	}

	printk (KERN_INFO "DMTimer #%d initialized (TCLR ST flag = %d).\n",
			timer_id, timer->context.tclr & 0x1);

	result = omap_dm_timer_read_counter(timer);

	printk (KERN_INFO "DMTimer counter = 0x%02x.\n", result);

	ready = 1;

	return 0;
}

module_init(dmtimer_module_init);
module_exit(dmtimer_module_exit);

