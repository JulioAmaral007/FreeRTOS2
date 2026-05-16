# Trabalho 2 — Relatório de Modificações

## Arquivos Modificados

| Arquivo | Tipo de Mudança |
|---|---|
| `user_app.c` | Lógica de controle, alarme, strings UART, ISR removida, watchdog timer |
| `user_app.h` | Declaração do callback do timer |
| `main.c` | Remoção de `Init_EmergencyInterrupt`, criação do Software Timer, prioridades |

---

## Componentes Reutilizados (sem modificação)

| Componente | Papel no T2 |
|---|---|
| `xLevelQueue` (fila) | Comunicação ADC → Controle (requisito T2 direto) |
| `xUARTMutex` (mutex) | Exclusão mútua na UART (requisito T2 direto) |
| `xEmergencySemaphore` (semáforo binário) | Sincronização Controle → Alarme |
| `vTask_ReadLevel` | Tarefa ADC — leitura do sensor de temperatura |
| `vTask_ReportStatus` | Tarefa UART — escreve temperatura no monitor serial |
| `vTask_EmergencyHandler` | Tarefa de Alarme — mensagem de emergência na UART |
| `vTask_ControlLogic` | Tarefa de Controle |
| `user_drivers.c` / `user_drivers.h` | ADC, UART, PWM, LEDs — **sem qualquer modificação** |

---

## Modificações Realizadas

### 1. `vTask_ControlLogic` — trigger do alarme (`user_app.c`)

**Mudança:** adicionado `xSemaphoreGive(xEmergencySemaphore)` na condição `uiReceivedTemp > LEVEL_HIGH`.

```c
} else if (uiReceivedTemp > LEVEL_HIGH) {
    // ... (lógica existente) ...
    // T2: a tarefa de controle é a ÚNICA responsável por desbloquear a tarefa alarme
    xSemaphoreGive(xEmergencySemaphore);
}
```

**Antes (T1):** o semáforo era dado pela ISR `_INT0Interrupt` (botão de hardware).  
**Depois (T2):** o semáforo é dado pela própria tarefa de controle quando temperatura excede `LEVEL_HIGH`.

---

### 2. Remoção da ISR `_INT0Interrupt` (`user_app.c`)

A interrupção externa INT0 foi removida. A tarefa de alarme agora só pode ser desbloqueada pela tarefa de controle, conforme exigido pelo enunciado:

> *"A tarefa alarme é iniciada bloqueada em um semáforo binário e deve ser desbloqueada somente pela tarefa controle."*

---

### 3. Remoção de `Init_EmergencyInterrupt()` (`main.c`)

Consequência direta da remoção da ISR. O pino RF6 não é mais configurado como entrada de interrupção.

---

### 4. Atualização das strings UART

| Antes (T1) | Depois (T2) |
|---|---|
| `"Nivel Baixo! Bomba ligada."` | `"Temperatura Baixa!"` |
| `"Nivel Alto! Bomba desligada."` | `"Temperatura Alta!"` |
| `"NIVEL DO RESERVATORIO"` | `"LEITURA DE TEMPERATURA"` |
| `"[STATUS] Nivel Atual: %u / 1023"` | `"[STATUS] Temperatura: %u / 1023"` |
| `"EMERGENCIA: Parada forcada acionada!"` | `"ALARME: Temperatura critica! Sistema parado."` |

---

### 5. Threshold de alarme ajustado

`LEVEL_HIGH` alterado de `900` para `700` (68% da escala ADC) para facilitar o acionamento do alarme na simulação Proteus.

---

### 6. Prioridades das tarefas (`main.c`)

Configuradas conforme o enunciado ("faça configurações diferentes em relação a prioridade das tarefas"):

| Tarefa | Prioridade | Justificativa |
|---|---|---|
| `vTask_EmergencyHandler` (Alarme) | 4 (máxima) | Alarme deve preemptar tudo ao ser desbloqueado |
| `vTask_ControlLogic` (Controle) | 3 | Processa dados e toma decisões em tempo real |
| `vTask_ReadLevel` (ADC) | 2 | Coleta periódica; pode aguardar um ciclo |
| `vTask_ReportStatus` (UART) | 1 (mínima) | Relatório periódico não é tempo-crítico |

**Análise:** Com essas prioridades, quando a temperatura excede o limiar, `vTask_ControlLogic` (prio 3) dá o semáforo e imediatamente `vTask_EmergencyHandler` (prio 4) preempta e executa a mensagem de alarme. A tarefa UART (prio 1) só executa quando as demais estiverem bloqueadas, garantindo que relatórios periódicos não atrasem o controle.

---

## Novo Recurso FreeRTOS (não visto em aula)

### Software Timer (`timers.h`)

Documentação: https://www.freertos.org/RTOS-software-timer.html

**Implementação:** `vWatchdogTimerCallback` — timer auto-reload de 5 segundos que envia `[WATCHDOG] Sistema ativo.` no monitor serial.

```c
// main.c
TimerHandle_t xWatchdogTimer = xTimerCreate(
    "Watchdog",
    pdMS_TO_TICKS(5000),
    pdTRUE,          // auto-reload
    NULL,
    vWatchdogTimerCallback
);
xTimerStart(xWatchdogTimer, 0);
```

**Por que é relevante:** Software Timers executam no contexto da Timer Task (tarefa daemon do FreeRTOS), sem criar uma tarefa dedicada. Isso demonstra uma alternativa leve à `vTaskDelayUntil` para ações periódicas simples. `configUSE_TIMERS = 1` já estava habilitado no `FreeRTOSConfig.h`.

---

## Mapeamento Requisito → Implementação

| Requisito T2 | Implementação |
|---|---|
| Tarefa para ler sensor de temperatura (ADC) | `vTask_ReadLevel` — lê `ADC1_ReadLevel()` a cada 500 ms |
| Tarefa para escrever no UART | `vTask_ReportStatus` — lê buffer compartilhado, escreve com mutex |
| Tarefa de controle | `vTask_ControlLogic` — recebe da fila, atualiza buffer, aciona alarme |
| Tarefa de alarme | `vTask_EmergencyHandler` — bloqueada em semáforo binário |
| Sensor → Controle via fila de mensagens | `xLevelQueue` (queue de `uint16_t`) |
| Controle → buffer compartilhado com UART | `g_uiLastTemperature` (variável global `volatile`) |
| UART task escreve temperatura no serial | `sprintf` + `UART_SendString` dentro de `vTask_ReportStatus` |
| Controle aciona alarme via semáforo binário | `xSemaphoreGive(xEmergencySemaphore)` em `vTask_ControlLogic` |
| Alarme desbloqueado somente pela tarefa controle | ISR removida; única fonte do `Give` é `vTask_ControlLogic` |
| Alarme escreve mensagem de emergência | `UART_SendString("\nALARME: ...")` em `vTask_EmergencyHandler` |
| Controle da UART com mutex | `xSemaphoreTake/Give(xUARTMutex)` em todas as tarefas que usam UART |
| Prioridades distintas | 4 / 3 / 2 / 1 (Alarme / Controle / ADC / UART) |
| Recurso FreeRTOS não visto em aula | Software Timer (`xTimerCreate`, `xTimerStart`) |

---

## Verificação de Exclusão Mútua

Todas as operações na UART seguem o padrão:

```c
if (xSemaphoreTake(xUARTMutex, xMutexWaitTicks) == pdTRUE) {
    UART_SendString("...");
    xSemaphoreGive(xUARTMutex);
}
```

Tarefas que usam o mutex: `vTask_ControlLogic`, `vTask_ReportStatus`, `vTask_EmergencyHandler` e `vWatchdogTimerCallback`. Nenhuma escreve na UART fora desse padrão.
