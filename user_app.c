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

extern QueueHandle_t     xLevelQueue;
extern SemaphoreHandle_t xUARTMutex;
extern SemaphoreHandle_t xBufferMutex;
extern SemaphoreHandle_t xEmergencySemaphore;

static TempBuffer_t g_xBuffer = { 0, 0, TEMP_STATUS_OK };

/* Executa a cada 500 ms. Chama ADC1_ReadTemperature para converter a tensao do
   LM35 em graus Celsius e envia o valor a xLevelQueue via xQueueSend. Se a
   fila estiver cheia, bloqueia ate haver espaco. Apos o envio, suspende a
   tarefa por 500 ms via vTaskDelay antes de iniciar o proximo ciclo. */
void vTask_ReadLevel(void *pvParameters) {
    uint16_t uiTempReading = 0;
    (void)pvParameters;

    for (;;) {
        uiTempReading = ADC1_ReadTemperature();

        xQueueSend(xLevelQueue, &uiTempReading, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* Bloqueia em xLevelQueue ate receber uma leitura de temperatura. Classifica o
   valor contra LEVEL_HIGH e atualiza o LED imediatamente. Em seguida adquire
   xBufferMutex, grava temperatura, timestamp e status em g_xBuffer e libera o
   mutex. Se o status for TEMP_STATUS_ALTA, envia alerta pela UART sob
   xUARTMutex e da xEmergencySemaphore para despertar vTask_EmergencyHandler. */
void vTask_ControlLogic(void *pvParameters) {
    (void)pvParameters;
    uint16_t uiReceivedTemp;

    for (;;) {
        xQueueReceive(xLevelQueue, &uiReceivedTemp, portMAX_DELAY);

        uint8_t uiStatus;

        if (uiReceivedTemp > 50) {
            uiStatus = TEMP_STATUS_ALTA;
            LED_Status_Alto(ON);
        } else {
            uiStatus = TEMP_STATUS_OK;
            LED_Status_Alto(OFF);
        }

        if (xSemaphoreTake(xBufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_xBuffer.temperatura = uiReceivedTemp;
            g_xBuffer.timestamp   = xTaskGetTickCount();
            g_xBuffer.status      = uiStatus;
            xSemaphoreGive(xBufferMutex);
        }

        if (uiStatus == TEMP_STATUS_ALTA) {
            if (xSemaphoreTake(xUARTMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                UART_SendString("\nALERTA: Temperatura Alta!\r\n");
                xSemaphoreGive(xUARTMutex);
            }
            xSemaphoreGive(xEmergencySemaphore);
        }
    }
}

/* Executa a cada 3 s via vTaskDelayUntil para manter periodo fixo independente
   do tempo de execucao. Adquire xBufferMutex, copia g_xBuffer para um snapshot
   local e libera o mutex imediatamente. Converte o campo status em string,
   formata a linha com temperatura e timestamp via sprintf e envia o relatorio
   completo pela UART sob xUARTMutex. Pula o ciclo se nao conseguir adquirir
   xBufferMutex. */
void vTask_ReportStatus(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    char cReportBuffer[80];
    TempBuffer_t xSnapshot;
    const char *pcStatusStr;

    (void)pvParameters;

    for (;;) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(3000));

        if (xSemaphoreTake(xBufferMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            xSnapshot = g_xBuffer;
            xSemaphoreGive(xBufferMutex);
        } else {
            continue;
        }

        switch (xSnapshot.status) {
            case TEMP_STATUS_ALTA: 
                pcStatusStr = "ALTA"; 
                break;
            default:               
                pcStatusStr = "OK";   
                break;
        }

        if (xSemaphoreTake(xUARTMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            sprintf(cReportBuffer,
                    "[STATUS] T=%u C | tick=%lu | %s\r\n",
                    xSnapshot.temperatura,
                    (unsigned long)xSnapshot.timestamp,
                    pcStatusStr);
            UART_SendString("\n--- LEITURA DE TEMPERATURA ---\r\n");
            UART_SendString(cReportBuffer);
            UART_SendString("------------------------------\r\n");
            xSemaphoreGive(xUARTMutex);
        }
    }
}

/* Bloqueia indefinidamente em xEmergencySemaphore. Ao ser desbloqueada por
   vTask_ControlLogic, executa a sequencia de alarme: pisca o LED RB15 tres
   vezes com intervalo de 100 ms entre cada estado. Em seguida chama
   xQueueReset para descartar amostras acumuladas em xLevelQueue durante o
   alarme. Por fim, adquire xUARTMutex e envia mensagem de alarme critico. */
void vTask_EmergencyHandler(void *pvParameters) {
    (void)pvParameters;

    for (;;) {
        if (xSemaphoreTake(xEmergencySemaphore, portMAX_DELAY) == pdTRUE) {

            for (int i = 0; i < 3; i++) {
                LED_Status_Alto(ON);
                vTaskDelay(pdMS_TO_TICKS(100));
                LED_Status_Alto(OFF);
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            xQueueReset(xLevelQueue);

            if (xSemaphoreTake(xUARTMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                UART_SendString("\nALARME: Temperatura critica! Sistema parado.\r\n");
                xSemaphoreGive(xUARTMutex);
            }
        }
    }
}

/* Executado pelo Timer Service Task a cada 5 s. Tenta adquirir xUARTMutex com
   timeout de 100 ms; se obtido, envia mensagem de heartbeat e libera o mutex.
   Se o mutex nao estiver disponivel, a mensagem e descartada para nao bloquear
   o agendador de timers. */
void vWatchdogTimerCallback(TimerHandle_t xTimer) {
    (void)xTimer;

    if (xSemaphoreTake(xUARTMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        UART_SendString("[WATCHDOG] Sistema ativo.\r\n");
        xSemaphoreGive(xUARTMutex);
    }
}
