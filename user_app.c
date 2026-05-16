#include <p24FJ128GA010.h>

#include "FreeRTOS.h"
#include "user_app.h"
#include "user_drivers.h"
#include "task.h"
#include <xc.h>
#include "semphr.h"
#include "queue.h"
#include <stdio.h>
#include "timers.h"

// Limites de temperatura em graus Celsius (LM35)
#define LEVEL_LOW  20   // abaixo de 20 °C → alerta frio
#define LEVEL_HIGH 50   // acima de 50 °C → alarme

#define REPORT_TASK_DELAY_MS 3000

extern QueueHandle_t xLevelQueue;
extern SemaphoreHandle_t xUARTMutex;
extern SemaphoreHandle_t xEmergencySemaphore;

// Buffer compartilhado entre tarefa de controle e tarefa UART
static volatile uint16_t g_uiLastTemperature = 0;

// Software Timer callback — recurso FreeRTOS não visto em aula (T2)
void vWatchdogTimerCallback(TimerHandle_t xTimer) {
    (void)xTimer;
    if (xSemaphoreTake(xUARTMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        UART_SendString("[WATCHDOG] Sistema ativo.\r\n");
        xSemaphoreGive(xUARTMutex);
    }
}

// Tarefa de controle: recebe temperatura da fila, atualiza buffer compartilhado,
// aciona tarefa de alarme via semáforo quando temperatura > LEVEL_HIGH.
void vTask_ControlLogic(void *pvParameters) {
    (void)pvParameters;
    uint16_t uiReceivedTemp;
    const TickType_t xMutexWaitTicks = pdMS_TO_TICKS(100);

    for (;;) {
        if (xQueueReceive(xLevelQueue, &uiReceivedTemp, portMAX_DELAY) == pdPASS) {
            g_uiLastTemperature = uiReceivedTemp;

            if (uiReceivedTemp < LEVEL_LOW) {
                LED_Status_Baixo(ON);
                LED_Status_OK(OFF);
                LED_Status_Alto(OFF);
                if (xSemaphoreTake(xUARTMutex, xMutexWaitTicks) == pdTRUE) {
                    UART_SendString("\nALERTA: Temperatura Baixa!\r\n");
                    xSemaphoreGive(xUARTMutex);
                }
            } else if (uiReceivedTemp > LEVEL_HIGH) {
                LED_Status_Baixo(OFF);
                LED_Status_OK(OFF);
                LED_Status_Alto(ON);
                if (xSemaphoreTake(xUARTMutex, xMutexWaitTicks) == pdTRUE) {
                    UART_SendString("\nALERTA: Temperatura Alta!\r\n");
                    xSemaphoreGive(xUARTMutex);
                }
                xSemaphoreGive(xEmergencySemaphore);
            } else {
                LED_Status_Baixo(OFF);
                LED_Status_OK(ON);
                LED_Status_Alto(OFF);
            }
        }
    }
}

// Tarefa UART: lê buffer compartilhado e envia temperatura ao monitor serial.
void vTask_ReportStatus(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(REPORT_TASK_DELAY_MS);
    const TickType_t xMutexWaitTicks = pdMS_TO_TICKS(1000);
    char cReportBuffer[80];

    for (;;) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        if (xSemaphoreTake(xUARTMutex, xMutexWaitTicks) == pdTRUE) {
            sprintf(cReportBuffer, "[STATUS] Temperatura: %u C\r\n", g_uiLastTemperature);
            UART_SendString("\n--- LEITURA DE TEMPERATURA ---\r\n");
            UART_SendString(cReportBuffer);
            UART_SendString("------------------------------\r\n");
            xSemaphoreGive(xUARTMutex);
        }
    }
}

// Tarefa ADC: lê sensor e envia à tarefa de controle via fila de mensagens.
void vTask_ReadLevel(void *pvParameters) {
    const TickType_t xSampleDelay = pdMS_TO_TICKS(500);
    uint16_t uiTempReading = 0;

    (void)pvParameters;

    for (;;) {
        uiTempReading = ADC1_ReadTemperature();
        xQueueSend(xLevelQueue, &uiTempReading, portMAX_DELAY);
        vTaskDelay(xSampleDelay);
    }
}

// Tarefa de alarme: inicia bloqueada no semáforo; desbloqueada somente pela tarefa de controle.
void vTask_EmergencyHandler(void *pvParameters) {
    const TickType_t xMutexWaitTicks = pdMS_TO_TICKS(200);

    (void)pvParameters;

    for (;;) {
        if (xSemaphoreTake(xEmergencySemaphore, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < 3; i++) {
                LED_Status_Baixo(ON);
                LED_Status_OK(ON);
                LED_Status_Alto(ON);
                vTaskDelay(pdMS_TO_TICKS(100));
                LED_Status_Baixo(OFF);
                LED_Status_OK(OFF);
                LED_Status_Alto(OFF);
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            xQueueReset(xLevelQueue);

            if (xSemaphoreTake(xUARTMutex, xMutexWaitTicks) == pdTRUE) {
                UART_SendString("\nALARME: Temperatura critica! Sistema parado.\r\n");
                xSemaphoreGive(xUARTMutex);
            }
        }
    }
}
