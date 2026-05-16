#ifndef USER_APP_H
#define USER_APP_H

#include "timers.h"

void vTask_ControlLogic(void *pvParameters);
void vTask_ReportStatus(void *pvParameters);
void vTask_ReadLevel(void *pvParameters);
void vTask_EmergencyHandler(void *pvParameters);

void vWatchdogTimerCallback(TimerHandle_t xTimer);

#endif /* USER_APP_H */
