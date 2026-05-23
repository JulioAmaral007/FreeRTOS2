#include "user_drivers.h"
#include <xc.h>
#include <stdio.h>
#include <p24FJ128GA010.h>

void LED_Status_Alto(int state)  { LATBbits.LATB15 = (state == ON) ? 1 : 0; }

void Init_DigitalOutputs(void) {
    AD1PCFGbits.PCFG9 = 1;  
    TRISBbits.TRISB15 = 0;  
    LED_Status_Alto(OFF);  
}

// UART
void UART_SendChar(char c) {
    while (U1STAbits.UTXBF);
    U1TXREG = c;
}

void UART_SendString(const char* str) {
    while (*str != '\0')
        UART_SendChar(*str++);
}

// U1TX = RF3 (saida) — somente transmissao
void Init_UART1(long baud_rate) {
    TRISFbits.TRISF3 = 0;  // RF3 (U1TX) como SAIDA

    U1BRG = (FCY / (16UL * baud_rate)) - 1;  // divisor de baud rate; formula para BRGH=0: BRG = FCY/(16*baud) - 1

    U1MODEbits.UARTEN = 0;
    U1MODEbits.BRGH   = 0;   // 16x oversampling (maior imunidade a ruido; para BRGH=1 seria 4x)
    U1MODEbits.PDSEL  = 0;   // 8 bits de dados, sem paridade (8N)
    U1MODEbits.STSEL  = 0;   // 1 stop bit — quadro completo: 8N1

    U1MODEbits.UARTEN = 1;   // habilita o modulo UART
    U1STAbits.UTXEN   = 1;   // habilita o transmissor (deve ser setado APOS UARTEN=1)
}

// Sensor de temperatura no canal AN2 (pino RB2)
void Init_ADC1(void) {
    AD1CON1 = 0;
    AD1CON2 = 0;
    AD1CON3 = 0;
    AD1CHS  = 0;

    AD1PCFGbits.PCFG2 = 0;     // AN2 como analogico
    TRISBbits.TRISB2  = 1;     // RB2 como entrada

    AD1CON1bits.SSRC = 0b111;  // auto-convert apos SAMC ciclos
    AD1CON3bits.SAMC = 16;     // tempo de amostragem: 16 TAD
    AD1CON3bits.ADCS = 2;      // TAD = (ADCS+1)*TCY = 3 * 62,5 ns = 187,5 ns
    AD1CHSbits.CH0SA = 2;      // canal positivo = AN2

    AD1CON1bits.ADON = 1;
}

// Le o LM35 e retorna temperatura em graus Celsius (inteiro).
// Conversao: temp_C = (raw * VREF_MV) / (1023 * LM35_MV_PER_C)
// Usa uint32_t no intermediario para evitar overflow (raw * VREF_MV pode ultrapassar 65535).
uint16_t ADC1_ReadTemperature(void) {
    AD1CON1bits.SAMP = 1;

    while (AD1CON1bits.DONE == 0);

    uint16_t raw = ADC1BUF0;
    AD1CON1bits.DONE = 0;

    return (uint16_t)((uint32_t)raw * VREF_MV / (1023UL * LM35_MV_PER_C));
}
