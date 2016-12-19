#include <stdio.h>
#include <prussdrv.h>
#include <pruss_intc_mapping.h>
#include <strings.h>

/* Network headers */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PRU_NUM 0
#define MESSAGE_LENGTH 6
#define PORT_NUMBER 561

int main(){

	int sockfd, n_message_length, result, change_state;
	struct sockaddr_in server_addr;
	char message[MESSAGE_LENGTH];

	tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;

	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	bzero((char *) &server_addr, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUMBER);
	server_addr.sin_addr.s_addr = inet_addr("192.168.2.22"); /* Endereco do servidor (propria maquina) */

	if ((result = bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr))) < 0) {
		perror("ERROR on binding");
		return 1;
	}

	prussdrv_init ();

	prussdrv_open (PRU_EVTOUT_0);
	prussdrv_open (PRU_EVTOUT_1);

	prussdrv_pruintc_init(&pruss_intc_initdata);

	prussdrv_exec_program (PRU_NUM, "./pru_timer_server.bin");

	int count = 0;

	for (;;) {


		printf ("Esperando PRU_EVTOUT_1...\n");

		prussdrv_pru_wait_event(PRU_EVTOUT_1);

		printf ("Recebeu evento PRU_EVTOUT_1...\n");

		prussdrv_pru_clear_event (PRU_EVTOUT_1, PRU1_ARM_INTERRUPT);

		/* Process packet */

		bzero(message, MESSAGE_LENGTH);

		change_state = 0;
		n_message_length = read(sockfd, message, MESSAGE_LENGTH);

		if (n_message_length > 0) {

			printf("Message : %s...\n", message);
			change_state = message[4] - '0';

			if (change_state) {
				prussdrv_pru_send_event(ARM_PRU1_INTERRUPT);
			}
		}

		/* Fim do processo do pacote */

		prussdrv_pru_send_event(ARM_PRU0_INTERRUPT);

		printf ("Send event ARM_PRU0_INTERRUPT...\n");

		prussdrv_pru_clear_event(PRU0, ARM_PRU0_INTERRUPT);

		if (change_state) prussdrv_pru_clear_event(PRU1, ARM_PRU1_INTERRUPT);

		printf ("Clear event ARM_PRU0_INTERRUPT...\n");

		printf("Count #%d...\n", count++);

	}

	prussdrv_pru_wait_event(PRU_EVTOUT_0);

	prussdrv_pru_disable(PRU_NUM);

	prussdrv_exit ();

	close(sockfd);

	return 0;
}
