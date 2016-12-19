.origin 0
.entrypoint START

#define PRU0_R31_VEC_VALID 	32	/* allows notification of program completion */
#define PRU_EVTOUT_0 		3	/* the event number that is sent back */
#define PRU_EVTOUT_1		4

/* Network related registers */ 
#define CPSW_STATERAM_BASE		0x4A100A00
#define RX0_CP_OFFSET			0x60

#define CPSW_CPDMA_BASE 		0x4A100800
#define RX_INTMASK_SET_OFFSET  	0xA8
#define RX0_PEND_M				1 << 0
#define CPDMA_EOI_VECTOR_OFFSET 0x94 

#define CPSW_WR_BASE 			0x4A101200
#define C0_RX_EN_OFFSET  		0x14
#define C0_R0_EN		  		1 << 0 
#define C0_RX_STAT_OFFSET		0x44


START:
		
		CLR r30.t5
		CLR r30.t3

		/* Habilita OCP port para comunicação com outros modulos do processador */
		LBCO 	r0, c4, 4, 4 	// load SYSCFG register to r0(use c4 const addr)
		CLR		r0, r0, 4		// clear bit 4 (STANDBY_INIT)	
		SBCO	r0, c4, 4, 4	// store the modified r0 back at the load addr

		/* Configura Ethernet module para gerar interrupcoes */
		
		MOV		r1, CPSW_CPDMA_BASE | RX_INTMASK_SET_OFFSET		// Move para r1 o endereco do registrador
																// RX_INTMASK_SET.
		MOV		r2, RX0_PEND_M
		SBBO	r2, r1, 0, 4
																
		MOV		r1, CPSW_WR_BASE | C0_RX_EN_OFFSET				// Move para r1 o endereco do registrador
																// C0_RX_EN.
		MOV		r2, C0_R0_EN
		SBBO	r2, r1, 0, 4
		
CHECK_INTERRUPT:

		MOV		r1, CPSW_WR_BASE | C0_RX_STAT_OFFSET
		LBBO	r2, r1, 0, 4
		
		QBBC	CHECK_INTERRUPT, r2.t0
		
		/* Interrupcao para host user space */		
		MOV 	r31.b0, PRU0_R31_VEC_VALID | PRU_EVTOUT_1
		
		/* Espera tratamento do pacote:
		   Faz a leitura da memoria e, caso esteja setada, continua   */
		
		WBS	r31.t30		
						
		CLR	r31.t30
		
		QBBC CLEAR_INTERRUPTION, r31.t31
		
		SET r30.t3
		CLR r30.t3
		
		QBBS TURN_OFF, r30.t5
		
TURN_ON:
		SET r30.t5
		JMP CLEAR_INTERRUPTION
		
TURN_OFF:
		CLR r30.t5
					
CLEAR_INTERRUPTION:			
		/* Atualizar RX0_CP com o proprio valor (teste) */
		
		MOV 	r1, CPSW_STATERAM_BASE | RX0_CP_OFFSET
		LBBO	r2, r1, 0, 4
		SBBO	r2, r1, 0, 4
		
		MOV		r1, CPSW_CPDMA_BASE | CPDMA_EOI_VECTOR_OFFSET
		MOV		r2, 0x1
		SBBO	r2, r1, 0, 4															

		JMP 	CHECK_INTERRUPT

END:
		MOV 	r31.b0, PRU0_R31_VEC_VALID | PRU_EVTOUT_0
		HALT