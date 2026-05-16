# Revisão Técnica — Trabalho 2 (DEC7562)
**Sistemas Operacionais Embarcados — PIC24FJ128GA010 + FreeRTOS**

---

## 1. Resumo Geral do Projeto

O projeto implementa uma planta industrial simulada no Proteus, composta por quatro tarefas FreeRTOS concorrentes que compartilham recursos e trocam informações via mecanismos de sincronização do kernel. O sensor de temperatura é simulado por um potenciômetro conectado ao canal ADC AN2 (RB2). A UART1 serve como interface de supervisão serial. Três LEDs (RB13/RB14/RB15) fornecem indicação visual de estado.

**Fluxo principal:**
```
ADC (500ms) → xLevelQueue → Control → g_uiLastTemperature → UART (3s)
                                  ↓ temp > 700
                          xEmergencySemaphore → Alarm → UART (emergência)
```

**Recursos FreeRTOS em uso:** fila, semáforo binário, mutex, `vTaskDelayUntil`, software timer.

---

## 2. Revisão dos Requisitos do `t2.png`

---

### REQ-1 — Tarefa para ler sensor de temperatura (ADC)
**Status: ✅ Implementado corretamente**

`vTask_ReadLevel` lê o ADC a cada 500 ms via `ADC1_ReadLevel()` e envia o resultado para `xLevelQueue`.

```c
uiTempReading = ADC1_ReadLevel();
xQueueSend(xLevelQueue, &uiTempReading, portMAX_DELAY);
vTaskDelay(xSampleDelay); // 500 ms
```

O driver `Init_ADC1()` configura corretamente: canal AN2, amostragem manual, clock derivado de FCY, tempo de amostragem de 16 TAD. A espera por `DONE` é feita em polling — aceitável em tarefa FreeRTOS (a tarefa não bloqueia o scheduler; outras tarefas executam normalmente).

---

### REQ-2 — Tarefa para escrever no UART
**Status: ✅ Implementado corretamente**

`vTask_ReportStatus` lê `g_uiLastTemperature` (buffer compartilhado) e envia ao monitor serial a cada 3 segundos, protegendo o acesso à UART com `xUARTMutex`.

```c
vTaskDelayUntil(&xLastWakeTime, xFrequency); // período exato de 3 s
if (xSemaphoreTake(xUARTMutex, xMutexWaitTicks) == pdTRUE) {
    sprintf(cReportBuffer, "[STATUS] Temperatura: %u / 1023\r\n", g_uiLastTemperature);
    UART_SendString(...);
    xSemaphoreGive(xUARTMutex);
}
```

O uso de `vTaskDelayUntil` é tecnicamente superior ao `vTaskDelay` para tarefas periódicas: compensa o tempo de execução, garantindo exatamente 3 s entre execuções mesmo que a tarefa demore para adquirir o mutex.

---

### REQ-3 — Tarefa de controle
**Status: ✅ Implementado corretamente**

`vTask_ControlLogic` recebe da fila, atualiza o buffer compartilhado, envia alertas UART e aciona o alarme:

```c
if (xQueueReceive(xLevelQueue, &uiReceivedTemp, portMAX_DELAY) == pdPASS) {
    g_uiLastTemperature = uiReceivedTemp;      // escreve buffer compartilhado
    // ... lógica de controle ...
    xSemaphoreGive(xEmergencySemaphore);       // dispara alarme
}
```

---

### REQ-4 — Tarefa de alarme
**Status: ✅ Implementado corretamente**

`vTask_EmergencyHandler` bloqueia no semáforo binário e, ao ser desbloqueada, pisca os LEDs 3 vezes e envia mensagem de emergência via UART:

```c
if (xSemaphoreTake(xEmergencySemaphore, portMAX_DELAY) == pdTRUE) {
    // pisca LEDs 3×
    xQueueReset(xLevelQueue);
    UART_SendString("\nALARME: Temperatura critica! Sistema parado.\r\n");
}
```

---

### REQ-5 — Tarefa ADC envia temperatura para tarefa de controle via fila de mensagens
**Status: ✅ Implementado corretamente**

`xLevelQueue = xQueueCreate(5, sizeof(uint16_t))` — fila de 5 elementos de 16 bits.

- Produtor: `vTask_ReadLevel` → `xQueueSend(..., portMAX_DELAY)`
- Consumidor: `vTask_ControlLogic` → `xQueueReceive(..., portMAX_DELAY)`

Ambos bloqueiam no acesso, sem polling ativo. Correto.

---

### REQ-6 — Tarefa de controle escreve temperatura em buffer compartilhado com a tarefa UART
**Status: ✅ Implementado — com ressalva técnica (ver §5)**

`g_uiLastTemperature` é a variável compartilhada declarada como `static volatile uint16_t`.

- Escrita (Control): `g_uiLastTemperature = uiReceivedTemp;`
- Leitura (UART): `sprintf(..., g_uiLastTemperature)` dentro do mutex

O `volatile` impede que o compilador faça cache do valor em registrador. No PIC24 (arquitetura de 16 bits), uma leitura/escrita de `uint16_t` é uma instrução única, portanto atomicamente segura nessa plataforma. Porém, formalmente falta proteção explícita na escrita (ver §5.1).

---

### REQ-7 — Tarefa UART escreve temperatura no monitor serial
**Status: ✅ Implementado corretamente**

A tarefa UART lê o buffer e formata a saída:
```
--- LEITURA DE TEMPERATURA ---
[STATUS] Temperatura: 512 / 1023
------------------------------
```

---

### REQ-8 — Tarefa de controle aciona alarme quando temperatura atinge determinado valor
**Status: ✅ Implementado corretamente**

Threshold definido como `LEVEL_HIGH = 700` (≈68% da escala ADC):

```c
} else if (uiReceivedTemp > LEVEL_HIGH) {
    xSemaphoreGive(xEmergencySemaphore); // único ponto de disparo
}
```

---

### REQ-9 — Tarefa de alarme iniciada bloqueada em semáforo binário, desbloqueada SOMENTE pela tarefa de controle
**Status: ✅ Implementado corretamente**

`xSemaphoreCreateBinary()` cria o semáforo com contagem inicial **zero**. Portanto, quando `vTask_EmergencyHandler` executa `xSemaphoreTake(..., portMAX_DELAY)` pela primeira vez, bloqueia imediatamente — sem ISR de hardware, sem outro código que possa dar o semáforo.

Busca no código-fonte: há exatamente **um** `xSemaphoreGive(xEmergencySemaphore)` em todo o projeto, localizado em `vTask_ControlLogic`. Requisito satisfeito na íntegra.

---

### REQ-10 — Tarefa de alarme escreve mensagem de emergência no monitor serial
**Status: ✅ Implementado corretamente**

```c
UART_SendString("\nALARME: Temperatura critica! Sistema parado.\r\n");
```

Protegido pelo `xUARTMutex` com timeout de 200 ms. Correto.

---

### REQ-11 — Controle da UART com variáveis mutex
**Status: ✅ Implementado corretamente**

`xUARTMutex = xSemaphoreCreateMutex()` — criado como **mutex** (não semáforo binário). Essa distinção é importante: mutexes FreeRTOS suportam **herança de prioridade** (`configUSE_MUTEXES = 1`), prevenindo inversão de prioridade quando uma tarefa de baixa prioridade segura o mutex enquanto uma de alta espera.

Todas as escritas à UART seguem o padrão correto sem exceção:

| Tarefa | Timeout de espera |
|---|---|
| `vTask_ControlLogic` | 100 ms |
| `vTask_ReportStatus` | 1000 ms |
| `vTask_EmergencyHandler` | 200 ms |
| `vWatchdogTimerCallback` | 100 ms |

Nenhuma escritura à UART ocorre fora desse padrão. Não há risco de deadlock (sem aquisições aninhadas de mutex).

---

### REQ-12 — Diferentes configurações de prioridade e análise do sistema
**Status: ⚠️ Parcialmente implementado**

O código define uma configuração com prioridades distintas:

| Tarefa | Prioridade |
|---|---|
| `vTask_EmergencyHandler` | 4 (máxima) |
| `vTask_ControlLogic` | 3 |
| `vTask_ReadLevel` | 2 |
| `vTask_ReportStatus` | 1 (mínima) |

O requisito, porém, exige **duas ou mais configurações diferentes** sendo comparadas e **analisadas quanto ao comportamento do escalonador e das exclusões mútuas**. O código apresenta apenas uma configuração. A análise comparativa (e.g., "o que acontece se alarme tiver prioridade menor que controle?") não está documentada no projeto.

**Ação recomendada:** documentar no relatório ao menos dois cenários de prioridade e a diferença observada no Proteus (latência do alarme, ordem de impressão no terminal serial, etc.).

---

### REQ-13 — Funcionalidade do FreeRTOS não vista em aula
**Status: ✅ Implementado — ver §3**

---

## 3. Funcionalidade Não Vista em Aula — Software Timer

### O que foi implementado

Um **Software Timer** auto-reload de 5 segundos que envia um heartbeat `[WATCHDOG] Sistema ativo.` ao monitor serial:

```c
// main.c
TimerHandle_t xWatchdogTimer = xTimerCreate(
    "Watchdog", pdMS_TO_TICKS(5000), pdTRUE, NULL, vWatchdogTimerCallback
);
xTimerStart(xWatchdogTimer, 0);

// user_app.c
void vWatchdogTimerCallback(TimerHandle_t xTimer) {
    (void)xTimer;
    if (xSemaphoreTake(xUARTMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        UART_SendString("[WATCHDOG] Sistema ativo.\r\n");
        xSemaphoreGive(xUARTMutex);
    }
}
```

### Por que não foi visto em aula

O conteúdo da disciplina cobre tarefas, semáforos binários, contadores, mutexes e filas. Software Timers são um mecanismo separado do kernel, gerenciado por uma **Timer Service Task** (tarefa daemon interna do FreeRTOS). Eles não criam uma tarefa de aplicação — o callback é executado no contexto da tarefa daemon, compartilhada por todos os timers do sistema.

### Diferencial técnico

| Aspecto | Tarefa periódica com `vTaskDelayUntil` | Software Timer |
|---|---|---|
| Ocupa entrada na lista de tarefas | Sim | Não (usa Timer Task) |
| Consome heap para TCB e stack | Sim (TCB + stack dedicada) | Apenas o objeto `TimerHandle_t` |
| Overhead de troca de contexto | Uma troca por execução | Zero — roda no daemon |
| Adequado para ações longas | Sim | **Não** — callbacks devem ser curtos |
| API | `xTaskCreate`, `vTaskDelayUntil` | `xTimerCreate`, `xTimerStart` |

Para uma mensagem de heartbeat simples (curta, rara), o timer é mais eficiente do que uma tarefa dedicada.

### Ressalva técnica

O callback chama `xSemaphoreTake` com timeout de 100 ms. Se o mutex estiver ocupado, a Timer Task bloqueia por até 100 ms, **atrasando potencialmente outros timers ativos no sistema**. A prática recomendada seria enviar uma notificação para uma tarefa auxiliar fazer o envio, mas para o escopo acadêmico o timeout curto mitiga o problema adequadamente.

---

## 4. Pontos Fortes do Trabalho

### 4.1 Mutex vs semáforo binário para UART
A escolha de `xSemaphoreCreateMutex()` (e não `xSemaphoreCreateBinary()`) para a UART é tecnicamente correta. Somente mutexes FreeRTOS implementam herança de prioridade. Se `vTask_ReportStatus` (prioridade 1) segurar o mutex e `vTask_EmergencyHandler` (prioridade 4) precisar dele, o scheduler eleva temporariamente a prioridade da tarefa de baixa para evitar inversão de prioridade. Com semáforo binário isso não ocorreria.

### 4.2 `vTaskDelayUntil` na tarefa UART
Uso correto para tarefas estritamente periódicas. Diferentemente de `vTaskDelay(3000)`, `vTaskDelayUntil` compensa o tempo de execução da tarefa, mantendo o período exato independente do tempo gasto aguardando o mutex.

### 4.3 Semáforo binário com contagem inicial zero
`xSemaphoreCreateBinary()` começa em 0. A tarefa de alarme bloqueia imediatamente ao chamar `xSemaphoreTake`, satisfazendo o requisito "iniciada bloqueada" sem nenhuma lógica extra.

### 4.4 Checagem de criação dos objetos FreeRTOS
```c
if (xLevelQueue == NULL || xUARTMutex == NULL || xEmergencySemaphore == NULL) {
    while(1);
}
```
Trava o sistema em caso de falha de alocação em vez de prosseguir com ponteiros nulos — comportamento correto para firmware embarcado.

### 4.5 Arquitetura limpa
Código resultante é direto, sem blocos `#ifdef` residuais, sem funções mortas. Separação clara entre drivers (`user_drivers.c`), lógica de aplicação (`user_app.c`) e inicialização (`main.c`).

---

## 5. Problemas Encontrados

---

### 5.1 ⚠️ Race condition formal no buffer compartilhado

**Arquivo:** `user_app.c`

O buffer compartilhado `g_uiLastTemperature` é escrito pela tarefa de controle **fora de qualquer proteção**:

```c
// vTask_ControlLogic — sem mutex
g_uiLastTemperature = uiReceivedTemp;
```

A tarefa UART lê dentro do mutex, mas o mutex protege o acesso à UART, não a leitura da variável. Formalmente, há uma race condition.

**Na prática no PIC24:** a instrução `MOV W0, _g_uiLastTemperature` é atômica (16 bits em um ciclo de escrita). Combinada com `volatile`, funciona corretamente nesta plataforma. Porém, isso é dependente de arquitetura — tecnicamente incorreto segundo o modelo de memória do C11 e do FreeRTOS.

**Solução formal:**
```c
// Opção A: ler com taskENTER_CRITICAL / taskEXIT_CRITICAL
// Opção B: usar xQueuePeek ou xQueueOverwrite para o buffer (mais idiomático em FreeRTOS)
// Opção C: ler dentro do mutex que já envolve o sprintf
```

O professor pode questionar este ponto diretamente.

---

### 5.2 ⚠️ Alarme dispara continuamente enquanto temperatura permanecer alta

**Arquivo:** `user_app.c`

Quando `uiReceivedTemp > LEVEL_HIGH`, a cada leitura ADC (500 ms) o alarme é disparado novamente. A sequência se repete indefinidamente:

```
[500ms] ADC → Control → Give(semaphore) → Alarm acorda → pisca LEDs (600ms) → UART
[500ms] ADC → Control → Give(semaphore) → Alarm acorda → ...
```

O sistema nunca "normaliza" enquanto a temperatura estiver alta — o terminal será inundado de mensagens de alarme. Em sistemas reais isso é tratado com **histerese** (só reaciona quando temperatura cai abaixo de um limiar menor) ou com um **flag de alarme ativo**.

Para o escopo do trabalho é funcionalmente demonstrativo, mas o professor pode questionar a falta de mecanismo de reset de alarme.

---

### 5.3 ⚠️ Timer Service Task tem prioridade igual à tarefa de alarme

**Arquivo:** `FreeRTOSConfig.h`

```c
#define configTIMER_TASK_PRIORITY   ( configMAX_PRIORITIES - 1 )  // = 4
```

`vTask_EmergencyHandler` também tem prioridade 4. Quando o timer de 5 s disparar simultaneamente com um alarme, ambas as tarefas estarão prontas com a mesma prioridade — o escalonador fará time-slice entre elas (round-robin). O alarme pode ser atrasado em até um tick (10 ms com `configTICK_RATE_HZ = 100`) pelo callback do watchdog.

**Solução simples:** reduzir a prioridade do timer ou a do alarme para evitar contenção.

---

### 5.4 ⚠️ `xTaskCreate` sem verificação de retorno

**Arquivo:** `main.c`

```c
xTaskCreate(vTask_EmergencyHandler, "Alarm", ...);
xTaskCreate(vTask_ControlLogic,     "Control", ...);
// ...
```

Nenhuma chamada verifica se `xTaskCreate` retornou `pdPASS`. Com `configTOTAL_HEAP_SIZE = 4096` bytes e o heap estimado em ~3600 bytes, a margem é estreita (~400 bytes). Uma falha de alocação de tarefa passaria silenciosamente.

**Solução:**
```c
BaseType_t ret = xTaskCreate(vTask_EmergencyHandler, "Alarm", ...);
configASSERT(ret == pdPASS);
```

---

### 5.5 ⚠️ Heap potencialmente apertado

**Arquivo:** `FreeRTOSConfig.h`

```c
#define configTOTAL_HEAP_SIZE   4096
```

Estimativa de uso (PIC24, palavras de 16 bits, heap_1.c):

| Objeto | Tamanho aprox. |
|---|---|
| TCB × 6 tarefas (incl. Idle + Timer) | ~540 bytes |
| Stack Alarm (256 palavras = 512 bytes) | 512 bytes |
| Stack Control (384 palavras = 768 bytes) | 768 bytes |
| Stack ADC (256 palavras = 512 bytes) | 512 bytes |
| Stack UART (256 palavras = 512 bytes) | 512 bytes |
| Stack Timer Task (128 palavras) | 256 bytes |
| Stack Idle Task (128 palavras) | 256 bytes |
| Queue (5×2 bytes + struct) | ~60 bytes |
| Mutex + Semáforo + Timer object | ~220 bytes |
| **Total estimado** | **~3636 bytes** |

Margem: ~460 bytes. Ajustado, mas viável com `heap_1.c` (sem fragmentação). A desativação de `configCHECK_FOR_STACK_OVERFLOW` (valor 0) impede detecção de overflow — **risco latente**.

---

### 5.6 ✅ ~~`(void)pvParameters` ausente em `vTask_ControlLogic`~~ — **CORRIGIDO**

`(void)pvParameters;` adicionado em `vTask_ControlLogic`, alinhando com o padrão das demais tarefas. Warning de compilação eliminado.

---

### 5.7 ✅ Pinos RF6 e RD0 confirmados sem uso — **REMOVIDOS**

RF6 era entrada do `Init_EmergencyInterrupt()` (INT0). RD0 era saída do PWM (OC1). Ambas as funções foram removidas dos drivers e não são chamadas em nenhum ponto do projeto. Os pinos podem ser desconectados no circuito Proteus sem qualquer impacto.

---

### 5.8 ℹ️ `xQueueReset` sem verificação de NULL

**Arquivo:** `user_app.c`, `vTask_EmergencyHandler`

```c
xQueueReset(xLevelQueue); // sem checagem de NULL
```

O código anterior verificava `if (xLevelQueue != NULL)`. A fila nunca é destruída no projeto, então o ponteiro é sempre válido, mas é uma regressão estilística menor.

---

### 5.9 ℹ️ Análise de diferentes prioridades ausente no relatório

O enunciado exige: *"Faça configurações diferentes em relação a prioridade das tarefas e analise o resultado do sistema."*

O código define apenas uma configuração. Não há segunda configuração comentada, nem documentação de análise comparativa. O professor pode deduzir pontos por isso — é um requisito explícito de **experimentação e análise**, não apenas de implementação.

---

## 6. Avaliação Final

### Aderência aos requisitos

| Requisito | Situação |
|---|---|
| Tarefa ADC | ✅ |
| Tarefa UART | ✅ |
| Tarefa Controle | ✅ |
| Tarefa Alarme | ✅ |
| Fila ADC → Controle | ✅ |
| Buffer compartilhado Controle → UART | ✅ (com ressalva técnica formal §5.1) |
| Alarme acionado somente pela tarefa de controle | ✅ |
| Alarme bloqueado em semáforo binário | ✅ |
| Mensagem de emergência no serial | ✅ |
| UART com mutex | ✅ (uso correto de mutex com herança de prioridade) |
| Diferentes prioridades implementadas | ✅ |
| Análise de prioridades documentada | ⚠️ ausente |
| Verificação das exclusões mútuas | ⚠️ verificável na simulação mas não documentado |
| Recurso não visto em aula | ✅ (Software Timer) |

---

### Qualidade técnica

**Pontos positivos:**
- Sincronização correta e completa entre as quatro tarefas
- Uso apropriado de mutex (com herança de prioridade) vs semáforo binário
- `vTaskDelayUntil` para periodicidade precisa
- Código limpo, sem funções mortas, sem blocos de preprocessador residuais
- Software Timer bem escolhido e justificado

**Pontos negativos:**
- Race condition formal em `g_uiLastTemperature` (§5.1)
- Alarme contínuo sem histerese (§5.2)
- `xTaskCreate` sem verificação de retorno (§5.4)
- Análise comparativa de prioridades ausente (§5.9)

**Corrigidos desde a revisão inicial:**
- ~~`(void)pvParameters` faltando em `vTask_ControlLogic`~~ (§5.6) ✅
- ~~Pinos RF6 e RD0 referenciados sem uso~~ (§5.7) ✅

---

### Perguntas que o professor provavelmente fará

1. **"Por que você usou mutex e não semáforo binário para a UART?"**
   Resposta esperada: mutex suporta herança de prioridade, prevenindo inversão de prioridade.

2. **"O que acontece se duas tarefas tentarem escrever na UART ao mesmo tempo sem o mutex?"**
   Resposta: os bytes de ambas serão intercalados no buffer de transmissão, corrompendo as mensagens.

3. **"O buffer compartilhado `g_uiLastTemperature` está protegido contra acesso concorrente?"**
   Ponto vulnerável — ver §5.1.

4. **"Por que o semáforo binário começa em zero? O que aconteceria se começasse em 1?"**
   Se começasse em 1, a tarefa de alarme executaria imediatamente ao ser criada, antes de qualquer leitura de temperatura — comportamento incorreto.

5. **"Explique o que é o Software Timer e por que você o escolheu."**
   Oportunidade de demonstrar conhecimento de §3.

6. **"Quais configurações de prioridade você testou? O que mudou no comportamento?"**
   Ponto fraco — apenas uma configuração foi implementada.

---

### Nota estimada

O trabalho atende **13 dos 14 requisitos verificáveis**, com o único requisito incompleto sendo a **análise documental de diferentes configurações de prioridade** (que é comportamental, não de código). Os problemas técnicos encontrados (§5) são em sua maioria ressalvas ou melhorias, não falhas de funcionamento.

Para maximizar a nota, recomenda-se:
1. Adicionar no relatório dois cenários de prioridade com screenshots do terminal Proteus
2. Mencionar a race condition formal em `g_uiLastTemperature` e justificar por que é segura no PIC24
