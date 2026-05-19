#ifndef USER_APP_H
#define USER_APP_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "timers.h"

// Status do buffer compartilhado
#define TEMP_STATUS_OK   0
#define TEMP_STATUS_ALTA 1

// Buffer compartilhado entre tarefa de controle (escritor) e tarefa UART (leitor).
// Acesso protegido por xBufferMutex.
typedef struct {
    uint16_t   temperatura;  // ultima leitura em graus Celsius
    TickType_t timestamp;    // tick em que a amostra foi escrita
    uint8_t    status;       // TEMP_STATUS_*
} TempBuffer_t;

void vTask_ControlLogic(void *pvParameters);
void vTask_ReportStatus(void *pvParameters);
void vTask_ReadLevel(void *pvParameters);
void vTask_EmergencyHandler(void *pvParameters);

void vWatchdogTimerCallback(TimerHandle_t xTimer);

#endif /* USER_APP_H */
