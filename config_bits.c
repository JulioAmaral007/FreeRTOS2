#include <xc.h>

// CONFIG2 — cadeia de clock: cristal HS -> PLL -> FOSC 32 MHz -> FCY 16 MHz
#pragma config POSCMOD   = HS      // oscilador primario em modo High-Speed (cristal > 4 MHz)
#pragma config FNOSC     = PRIPLL  // inicia pelo oscilador primario com PLL; sem isso, FRC (~8 MHz) corrompe UART e ADC
#pragma config OSCIOFNC  = OFF     // OFF desabilita saida de clock no OSC2, mantendo o pino dedicado ao cristal
#pragma config FCKSM     = CSDCMD  // desabilita clock switching e monitor; evita troca silenciosa que quebraria FCY do FreeRTOS
#pragma config IESO      = OFF     // desabilita two-speed startup; necessario no Proteus — UART perde sincronia se o chip iniciar no FRC
