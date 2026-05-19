#ifndef USER_DRIVERS_H
#define USER_DRIVERS_H

#include <stdint.h>

#define FCY 16000000UL

#define ON  1
#define OFF 0

// LEDs
void LED_Status_Alto(int state);

// UART
void UART_SendString(const char* str);
void UART_SendChar(char c);

// Inicialização
void Init_DigitalOutputs(void);
void Init_UART1(long baud_rate);
void Init_ADC1(void);

// ADC — LM35: 10 mV/°C, referência = AVdd
// Ajuste VREF_MV conforme a tensão de alimentação do circuito (3300 ou 5000)
#define VREF_MV        5000
#define LM35_MV_PER_C  10

uint16_t ADC1_ReadTemperature(void);

#endif // USER_DRIVERS_H