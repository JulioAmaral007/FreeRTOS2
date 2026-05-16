#include "user_drivers.h"
#include <xc.h>
#include <stdio.h>
#include <p24FJ128GA010.h>

// LEDs: RB13 = Baixo, RB14 = OK, RB15 = Alto
void LED_Status_Baixo(int state) { LATBbits.LATB13 = (state == ON) ? 1 : 0; }
void LED_Status_OK(int state)    { LATBbits.LATB14 = (state == ON) ? 1 : 0; }
void LED_Status_Alto(int state)  { LATBbits.LATB15 = (state == ON) ? 1 : 0; }

void Init_DigitalOutputs(void) {
    AD1PCFGbits.PCFG9  = 1;  // RB15/AN9  como digital
    AD1PCFGbits.PCFG10 = 1;  // RB14/AN10 como digital
    AD1PCFGbits.PCFG11 = 1;  // RB13/AN11 como digital

    TRISBbits.TRISB13 = 0;
    TRISBbits.TRISB14 = 0;
    TRISBbits.TRISB15 = 0;

    LED_Status_Baixo(OFF);
    LED_Status_OK(OFF);
    LED_Status_Alto(OFF);
}

// UART
void UART_SendChar(char c) {
    while (U1STAbits.UTXBF);
    U1TXREG = c;
}

void UART_SendString(const char* str) {
    while (*str != '\0') {
        UART_SendChar(*str++);
    }
}

// U1TX = RF3 (saída), U1RX = RF2 (entrada) — pinos fixos no PIC24FJ128GA010
void Init_UART1(long baud_rate) {
    TRISFbits.TRISF3 = 0;
    TRISFbits.TRISF2 = 1;

    U1BRG = (FCY / (16UL * baud_rate)) - 1;

    U1MODEbits.UARTEN = 0;
    U1MODEbits.BRGH   = 0;  // 16x oversampling
    U1MODEbits.PDSEL  = 0;  // 8-bit, no parity
    U1MODEbits.STSEL  = 0;  // 1 stop bit

    U1STAbits.UTXISEL0 = 0;
    U1STAbits.UTXISEL1 = 0;
    U1STAbits.URXISEL  = 0;

    U1MODEbits.UARTEN = 1;
    U1STAbits.UTXEN   = 1;
}

// Sensor de temperatura no canal AN2 (pino RB2)
void Init_ADC1(void) {
    AD1CON1bits.ADON = 0;

    AD1PCFGbits.PCFG2 = 0;
    TRISBbits.TRISB2  = 1;

    AD1CON1 = 0;
    AD1CON2 = 0;
    AD1CON3 = 0;
    AD1CHS  = 0;

    AD1CON1bits.FORM = 0;
    AD1CON1bits.SSRC = 0b111;
    AD1CON1bits.ASAM = 0;

    AD1CON2bits.VCFG  = 0;
    AD1CON2bits.CSCNA = 0;
    AD1CON2bits.SMPI  = 0;
    AD1CON2bits.BUFM  = 0;

    AD1CON3bits.ADRC = 0;
    AD1CON3bits.SAMC = 16;
    AD1CON3bits.ADCS = 2;

    AD1CHSbits.CH0SA = 2;
    AD1CHSbits.CH0NA = 0;

    AD1CON1bits.ADON = 1;
}

// Lê o LM35 e retorna temperatura em graus Celsius (inteiro).
// Conversão: temp_C = (raw * VREF_MV) / (1023 * LM35_MV_PER_C)
// Usa uint32_t no intermediário para evitar overflow (raw * VREF_MV pode ultrapassar 65535).
uint16_t ADC1_ReadTemperature(void) {
    AD1CON1bits.SAMP = 1;
    for (volatile uint16_t i = 0; i < 600; i++) {
        __builtin_nop();
    }
    AD1CON1bits.SAMP = 0;

    while (AD1CON1bits.DONE == 0);

    uint16_t raw = ADC1BUF0;
    AD1CON1bits.DONE = 0;

    return (uint16_t)((uint32_t)raw * VREF_MV / (1023UL * LM35_MV_PER_C));
}