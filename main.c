#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "user_app.h"
#include "user_drivers.h"

QueueHandle_t xLevelQueue;
SemaphoreHandle_t xUARTMutex;
SemaphoreHandle_t xBufferMutex;
SemaphoreHandle_t xEmergencySemaphore;

int main() {
    // Hardware
    Init_DigitalOutputs();
    Init_UART1(9600);
    Init_ADC1();

    // Objetos FreeRTOS
    xLevelQueue         = xQueueCreate(5, sizeof(uint16_t));
    xUARTMutex          = xSemaphoreCreateMutex();
    xBufferMutex        = xSemaphoreCreateMutex();
    xEmergencySemaphore = xSemaphoreCreateBinary();

    if (xLevelQueue == NULL || xUARTMutex == NULL ||
        xBufferMutex == NULL || xEmergencySemaphore == NULL) {
        while(1);
    }

    // Tarefas — prioridades distintas para análise de escalonamento (T2)
    xTaskCreate(vTask_EmergencyHandler, "Alarm",   configMINIMAL_STACK_SIZE + 128, NULL, 4, NULL);
    xTaskCreate(vTask_ControlLogic,     "Control", configMINIMAL_STACK_SIZE + 256, NULL, 3, NULL);
    xTaskCreate(vTask_ReadLevel,        "ADC",     configMINIMAL_STACK_SIZE + 128, NULL, 2, NULL);
    xTaskCreate(vTask_ReportStatus,     "UART",    configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);

    // Software Timer — recurso FreeRTOS não visto em aula (T2)
    TimerHandle_t xWatchdogTimer = xTimerCreate(
        "Watchdog", pdMS_TO_TICKS(5000), pdTRUE, NULL, vWatchdogTimerCallback
    );
    if (xWatchdogTimer != NULL) {
        xTimerStart(xWatchdogTimer, 0);
    }

    vTaskStartScheduler();

    while (1);
    return 0;
}
