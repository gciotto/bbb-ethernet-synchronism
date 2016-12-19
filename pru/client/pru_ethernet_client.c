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

	int sockfd, n, result;
	char buff[MESSAGE_LENGTH];
	struct sockaddr_in server_addr;

	tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;

	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	bzero((char *) &server_addr, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUMBER);
	server_addr.sin_addr.s_addr = inet_addr("192.168.2.22"); /* Endereco do servidor (propria maquina) */

	if ((result = connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr))) < 0) {
		perror("ERROR on connecting...");
		return 1;
	}

	prussdrv_init ();

	prussdrv_open (PRU_EVTOUT_0);
	prussdrv_open (PRU_EVTOUT_1);

	prussdrv_pruintc_init(&pruss_intc_initdata);

	prussdrv_exec_program (PRU_NUM, "./pru_client.bin");

	printf("Executando programa....\n");

	for (;;) {

		printf ("Esperando PRU_EVTOUT_1...\n");

		/* Waits for gpio events from PRU */
		prussdrv_pru_wait_event(PRU_EVTOUT_1);

		printf ("Recebeu evento PRU_EVTOUT_1...\n");

		prussdrv_pru_clear_event (PRU_EVTOUT_1, PRU1_ARM_INTERRUPT);

		sprintf(buff, "00001");

		printf("Enviando comando...\n");

		/* Sends a UDP datagram to server every time an event is detected */
		n = sendto(sockfd, buff, MESSAGE_LENGTH, 0,(struct sockaddr *) &server_addr, sizeof(server_addr));

		printf("Enviou comando %d...\n", n);
	}

	prussdrv_pru_wait_event(PRU_EVTOUT_0);

	prussdrv_pru_disable(PRU_NUM);

	prussdrv_exit ();

	return 0;
}
