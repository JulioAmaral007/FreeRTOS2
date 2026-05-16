#include <xc.h>

// CONFIG2
#pragma config POSCMOD = HS             // Usar cristal de alta velocidade (High-Speed)
#pragma config OSCIOFNC = OFF           // Pino OSC2 � sa�da de clock
#pragma config FCKSM = CSDCMD           // Desabilitar monitoramento e troca de clock
#pragma config FNOSC = PRIPLL           // Usar Oscilador Prim�rio com PLL (para atingir alta frequ�ncia)
#pragma config IESO = OFF               // Desabilitar troca de clock interno/externo
