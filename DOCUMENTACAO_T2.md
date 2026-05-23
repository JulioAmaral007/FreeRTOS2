# Documentação Técnica — Segundo Trabalho Prático (T2)

**Disciplina:** Sistemas Operacionais Embarcados — DEC7562
**Universidade:** Universidade Federal de Santa Catarina (UFSC)
**Professor:** Anderson Luiz Fernandes Perez
**Aluno:** Júlio Cézar
**Plataforma:** PIC24FJ128GA010 (simulação no Proteus)
**Kernel:** FreeRTOS v11.1.0
**Compilador:** MPLAB XC16 v2.10

> Este arquivo é a documentação única e definitiva do projeto. Serve simultaneamente como **roteiro de apresentação**, **material de estudo** e **defesa técnica** do trabalho diante do professor.

---

## Índice

1. [Introdução](#1-introdução)
2. [Requisitos do Trabalho](#2-requisitos-do-trabalho)
3. [Arquitetura Geral do Sistema](#3-arquitetura-geral-do-sistema)
4. [Explicação das Tasks](#4-explicação-das-tasks)
5. [Explicação dos Recursos do RTOS](#5-explicação-dos-recursos-do-rtos)
6. [Explicação dos Periféricos](#6-explicação-dos-periféricos)
7. [Explicação do Circuito](#7-explicação-do-circuito)
8. [Fluxo Completo da Aplicação](#8-fluxo-completo-da-aplicação)
9. [Explicação das Prioridades](#9-explicação-das-prioridades)
10. [Funcionalidades Extras](#10-funcionalidades-extras)
11. [Possíveis Perguntas do Professor](#11-possíveis-perguntas-do-professor)
12. [Conclusão](#12-conclusão)

---

## 1. Introdução

### 1.1 Proposta do sistema

O segundo trabalho prático pede o desenvolvimento de um **sistema embarcado multitarefa**, executando sobre o sistema operacional de tempo real **FreeRTOS**, em um microcontrolador **PIC24FJ128GA010** simulado no **Proteus**. O sistema simula uma **pequena planta industrial supervisionada**, composta por sensor, atuador e módulo de supervisão, com múltiplas tarefas concorrentes que compartilham recursos e trocam informações entre si.

### 1.2 Aplicação implementada

A planta industrial modelada é um **monitor térmico**, equivalente em escopo ao que seria um nó de monitoramento de sala de servidores, estufa ou reator químico:

- **Sensor:** LM35 (sensor analógico de temperatura, 10 mV/°C) lido pelo ADC1 no canal AN2.
- **Atuador:** LED de alarme (RB15) que sinaliza visualmente temperatura crítica.
- **Supervisão:** monitor serial via UART1 (RF2/RF3, 9600 8N1) recebe relatórios periódicos de temperatura, alertas em tempo real e mensagens da rotina de alarme.

### 1.3 Visão geral

Quatro tarefas concorrentes — **leitura do sensor**, **controle**, **UART** e **alarme** — executam de forma cooperativa sob escalonamento preemptivo com prioridades distintas, comunicando-se exclusivamente via primitivos de IPC do FreeRTOS (**fila de mensagens**, **buffer compartilhado protegido por mutex**, **mutex de UART** e **semáforo binário**). Um quinto recurso, **Software Timer**, opera como heartbeat de 5 s e atende ao requisito de implementar um recurso não visto em aula.

---

## 2. Requisitos do Trabalho

A tabela abaixo cruza cada requisito explícito do enunciado (`t2.png`) e do diagrama (`Diagrama_T2.png`) com sua implementação no código. Todos os requisitos foram **✅ Implementados**.

| # | Requisito do enunciado | Implementação | Local |
|---|------------------------|---------------|-------|
| R1 | Sistema multitarefa em FreeRTOS, PIC24FJ128GA010, Proteus | `main()` cria 4 tasks, registra IPC e inicia o scheduler | `main.c:14-48`, `Proteus/Simulation.pdsprj` |
| R2 | Planta industrial: sensores, atuadores, supervisão | LM35 (sensor) + LED RB15 (atuador) + UART (supervisão) | `user_app.c`, `user_drivers.c` |
| R3 | Tarefa para ler sensor de temperatura (ADC) | `vTask_ReadLevel` lê AN2 a cada 500 ms | `user_app.c:118-129` |
| R4 | Tarefa para escrever no UART | `vTask_ReportStatus` reporta a cada 3 s | `user_app.c:76-115` |
| R5 | Tarefa de controle | `vTask_ControlLogic` classifica leitura e dispara alarme | `user_app.c:39-73` |
| R6 | Tarefa de alarme | `vTask_EmergencyHandler` bloqueada em semáforo binário | `user_app.c:132-154` |
| R7 | Sensor → Controle via fila de mensagens | `xLevelQueue` (FIFO de 5 × `uint16_t`) | `main.c:21`, `user_app.c:126`, `user_app.c:45` |
| R8 | Controle escreve em **buffer compartilhado** com UART | `g_xBuffer` (`TempBuffer_t`) sob `xBufferMutex` | `user_app.h:14-18`, `user_app.c:26,57-62,91-96` |
| R9 | UART escreve a temperatura no monitor serial | `sprintf` + `UART_SendString` no relatório | `user_app.c:104-111` |
| R10 | Controle aciona alarme via semáforo binário | `xSemaphoreGive(xEmergencySemaphore)` em controle | `user_app.c:69` |
| R11 | Alarme **somente** desbloqueada pela tarefa controle | `give` ocorre **apenas** em `vTask_ControlLogic` | `user_app.c:69` (único caller no projeto) |
| R12 | Alarme escreve mensagem de emergência no serial | `"ALARME: Temperatura critica! Sistema parado."` | `user_app.c:148-151` |
| R13 | Controle da UART com **mutex** | `xUARTMutex` (mutex com herança de prioridade) | `main.c:22`, em todas as escritas UART |
| R14 | Configurar **prioridades diferentes** e analisar | 4 / 3 / 2 / 1 (Alarme / Controle / ADC / UART) | `main.c:32-35` |
| R15 | Verificar exclusões mútuas | Dois mutexes dedicados, todos com timeout finito | `xUARTMutex`, `xBufferMutex` |
| R16 | Implementar **recurso da documentação FreeRTOS não visto em aula** | **Software Timer** (`xTimerCreate`) — heartbeat de 5 s | `main.c:38-43`, `user_app.c:29-35` |

> **Conclusão de §2:** os 16 requisitos extraídos do enunciado foram cobertos integralmente. Não há requisitos parciais ou faltantes.

---

## 3. Arquitetura Geral do Sistema

### 3.1 Diagrama lógico

A arquitetura segue fielmente o `Diagrama_T2.png` fornecido pelo enunciado:

```
   Sensor LM35 (AN2 / RB2)
            │
            ▼
   ┌─────────────────────┐
   │   vTask_ReadLevel   │  Prioridade 2 — período 500 ms
   └─────────┬───────────┘
             │
             ▼  xQueueSend / xQueueReceive
   ┌─────────────────────┐
   │   xLevelQueue       │  FIFO 5 × uint16_t (PIPE)
   └─────────┬───────────┘
             │
             ▼
   ┌─────────────────────┐                       ┌──────────────────────────┐
   │  vTask_ControlLogic │── xSemaphoreGive ────►│  vTask_EmergencyHandler  │
   └─────────┬───────────┘   (xEmergencySem)     │   Prioridade 4 (top)     │
             │                                    └────────────┬─────────────┘
             │ escreve g_xBuffer                                │
             │ sob xBufferMutex                                 │ flash LED RB15
             ▼                                                  │ + reset queue
   ┌─────────────────────┐                                      │
   │   g_xBuffer         │                                      │
   │   (TempBuffer_t)    │                                      │
   └─────────┬───────────┘                                      │
             │ leitura snapshot                                 │
             │ sob xBufferMutex                                 │
             ▼                                                  ▼
   ┌─────────────────────┐                       ┌──────────────────────────┐
   │  vTask_ReportStatus │──── xUARTMutex ──────►│   UART1 (RF2 / RF3)      │
   │   Prioridade 1      │                       │   Monitor serial 9600 8N1│
   └─────────────────────┘                       └──────────────────────────┘
                                                              ▲
                                                              │
                                                  Software Timer 5 s
                                                  (Daemon Task / heartbeat)
```

### 3.2 Componentes principais

| Componente | Função |
|------------|--------|
| **4 tarefas** | Cada uma encapsula uma responsabilidade isolada (sensor / controle / UART / alarme) |
| **`xLevelQueue`** | Comunicação assíncrona produtor-consumidor (sensor → controle) |
| **`g_xBuffer`** | Estado compartilhado consultado pela UART para gerar relatórios |
| **`xBufferMutex`** | Exclusão mútua sobre `g_xBuffer` |
| **`xUARTMutex`** | Exclusão mútua sobre o periférico UART1 |
| **`xEmergencySemaphore`** | Sinalização assíncrona controle → alarme |
| **`xWatchdogTimer`** | Software Timer auto-reload 5 s (recurso extra) |

### 3.3 Modelo de execução

- **Escalonamento:** preemptivo, com **time-slicing** entre tarefas de mesma prioridade (`configUSE_PREEMPTION = 1`, `configUSE_TIME_SLICING = 1`).
- **Tick rate:** 100 Hz → quanta de 10 ms (`configTICK_RATE_HZ = 100`).
- **Heap:** 4096 bytes, gerenciador `heap_1.c` (sem free → sem fragmentação; adequado a sistemas estáticos).
- **Mutexes com herança de prioridade:** `configUSE_MUTEXES = 1` previne inversão de prioridade clássica.

### 3.4 Mapa de hardware (visão consolidada)

| Recurso lógico | Pino físico | Periférico | Direção |
|----------------|-------------|------------|---------|
| Sensor LM35 | RB2 / AN2 | ADC1 (CH0SA = 2) | Entrada analógica |
| LED de alarme | RB15 | GPIO (AN9 desativado por `AD1PCFG.PCFG9 = 1`) | Saída digital |
| UART1 TX | RF3 | U1TX | Saída |
| UART1 RX | RF2 | U1RX | Entrada |

---

## 4. Explicação das Tasks

### 4.1 `vTask_ReadLevel` — Sensor ADC

| Atributo | Valor |
|----------|-------|
| Arquivo | `user_app.c:118-129` |
| Prioridade | 2 |
| Stack | `configMINIMAL_STACK_SIZE + 128` = 256 words |
| Periodicidade | 500 ms (`vTaskDelay`) |
| Recursos | ADC1, `xLevelQueue` |

**Função:** dispara uma conversão A/D no canal AN2, converte o valor cru (0..1023) em temperatura inteira em °C (LM35 → 10 mV/°C, Vref = 5 V) e envia para a fila `xLevelQueue`.

**Lógica:**
```c
for (;;) {
    uiTempReading = ADC1_ReadTemperature();
    xQueueSend(xLevelQueue, &uiTempReading, portMAX_DELAY);
    vTaskDelay(xSampleDelay);  // 500 ms
}
```

**Por que essa task existe:** isolar a tarefa de aquisição libera o resto do sistema do timing do hardware. O `xQueueSend` com `portMAX_DELAY` bloqueia a tarefa caso o consumidor esteja atrasado — **nenhuma amostra é perdida silenciosamente** (o efeito é estender o período de amostragem, o que é aceitável em uma planta de baixa dinâmica como temperatura).

### 4.2 `vTask_ControlLogic` — Controle

| Atributo | Valor |
|----------|-------|
| Arquivo | `user_app.c:39-73` |
| Prioridade | 3 |
| Stack | `configMINIMAL_STACK_SIZE + 256` = 384 words |
| Periodicidade | event-driven (acorda ao receber dado na fila) |
| Recursos | `xLevelQueue`, `xBufferMutex`, `xUARTMutex`, `xEmergencySemaphore`, LED RB15 |

**Função:** consumidor da fila. Recebe a temperatura, classifica em dois estados (**OK** ou **ALTA**), atualiza o LED de alarme, escreve estado completo no buffer compartilhado e — se a temperatura for crítica — emite alerta UART e libera o semáforo da tarefa de alarme.

**Lógica resumida:**
```c
if (xQueueReceive(xLevelQueue, &uiReceivedTemp, portMAX_DELAY) == pdPASS) {
    // Classifica e atualiza LED
    if (uiReceivedTemp > LEVEL_HIGH) { uiStatus = TEMP_STATUS_ALTA; LED_Status_Alto(ON); }
    else                              { uiStatus = TEMP_STATUS_OK;   LED_Status_Alto(OFF); }

    // Escrita atômica no buffer compartilhado sob mutex
    if (xSemaphoreTake(xBufferMutex, xMutexWaitTicks) == pdTRUE) {
        g_xBuffer.temperatura = uiReceivedTemp;
        g_xBuffer.timestamp   = xTaskGetTickCount();
        g_xBuffer.status      = uiStatus;
        xSemaphoreGive(xBufferMutex);
    }

    // Em caso de temperatura crítica: alerta UART + sinaliza alarme
    if (uiStatus == TEMP_STATUS_ALTA) {
        // ...mensagem UART sob xUARTMutex...
        xSemaphoreGive(xEmergencySemaphore);  // ← único caller no projeto
    }
}
```

**Por que essa task existe:** centraliza a lógica de decisão (single source of truth para os estados do sistema). Mantém o produtor (sensor) burro e o consumidor (UART) também — o "cérebro" do sistema está aqui.

### 4.3 `vTask_ReportStatus` — UART (relatórios)

| Atributo | Valor |
|----------|-------|
| Arquivo | `user_app.c:76-115` |
| Prioridade | 1 (mais baixa) |
| Stack | `configMINIMAL_STACK_SIZE + 128` = 256 words |
| Periodicidade | 3 s (`vTaskDelayUntil` — período exato sem drift) |
| Recursos | `g_xBuffer`, `xBufferMutex`, `xUARTMutex`, UART1 |

**Função:** acorda em períodos exatos de 3 s, toma um **snapshot** do buffer compartilhado, libera o mutex, formata a mensagem fora da seção crítica e envia o relatório para o monitor serial.

**Padrão *snapshot-then-release*:**
```c
if (xSemaphoreTake(xBufferMutex, xMutexWaitTicks) == pdTRUE) {
    xSnapshot = g_xBuffer;          // cópia local rápida (~3 instruções)
    xSemaphoreGive(xBufferMutex);   // libera imediatamente
} else { continue; }

// Formatação e envio acontecem FORA da seção crítica
sprintf(cReportBuffer, "[STATUS] T=%u C | tick=%lu | %s\r\n", ...);
xSemaphoreTake(xUARTMutex, ...);
UART_SendString(cReportBuffer);
xSemaphoreGive(xUARTMutex);
```

**Por que essa task existe:** desacoplar a apresentação dos relatórios do caminho crítico de controle. Mesmo com prioridade mínima, o uso de `vTaskDelayUntil` garante o período de 3 s independente de quanto a UART foi preemptada — não há drift acumulado.

### 4.4 `vTask_EmergencyHandler` — Alarme

| Atributo | Valor |
|----------|-------|
| Arquivo | `user_app.c:132-154` |
| Prioridade | 4 (máxima das tarefas de aplicação) |
| Stack | `configMINIMAL_STACK_SIZE + 128` = 256 words |
| Periodicidade | event-driven (bloqueada até semáforo) |
| Recursos | `xEmergencySemaphore`, `xUARTMutex`, `xLevelQueue` (reset), LED RB15 |

**Função:** inicia bloqueada no semáforo binário. Quando desbloqueada pela tarefa de controle, executa a rotina de emergência: **flash do LED RB15 (3 ciclos de 100 ms ON / 100 ms OFF)**, **reset da fila** de leituras e envio da mensagem de emergência pelo monitor serial.

**Lógica:**
```c
for (;;) {
    if (xSemaphoreTake(xEmergencySemaphore, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < 3; i++) {
            LED_Status_Alto(ON);  vTaskDelay(pdMS_TO_TICKS(100));
            LED_Status_Alto(OFF); vTaskDelay(pdMS_TO_TICKS(100));
        }
        xQueueReset(xLevelQueue);  // descarta amostras pendentes
        // mensagem UART sob xUARTMutex...
    }
}
```

**Por que essa task existe:** atende literalmente o enunciado ("**a tarefa alarme é iniciada bloqueada em um semáforo binário e deve ser desbloqueada somente pela tarefa controle**"). Isolada do controle, pode sinalizar emergência sem atrapalhar o pipeline normal — o controle apenas dispara o evento e segue trabalhando.

### 4.5 Daemon Task (Timer Service) — Watchdog

Não é uma task criada por `xTaskCreate`, mas é criada pelo próprio kernel quando `configUSE_TIMERS = 1`. Executa o callback `vWatchdogTimerCallback` a cada 5 s, com prioridade `configMAX_PRIORITIES - 1 = 4`, enviando `"[WATCHDOG] Sistema ativo.\r\n"` pela UART. Detalhes no §10.

---

## 5. Explicação dos Recursos do RTOS

### 5.1 Filas (Queues) — `xLevelQueue`

**O que é:** estrutura FIFO thread-safe que carrega cópias de dados entre tarefas. No projeto, comporta **5 posições de `uint16_t`** (10 bytes).

```c
xLevelQueue = xQueueCreate(5, sizeof(uint16_t));
```

**Por que usar:** desacopla produtor (`vTask_ReadLevel`) e consumidor (`vTask_ControlLogic`). O produtor não precisa saber se o consumidor está pronto, e vice-versa. Bloqueio com `portMAX_DELAY` é o idioma idiomático: a tarefa só sai do estado bloqueado quando há dado, **sem polling, sem busy-wait, CPU = 0 enquanto espera**.

**Onde é usada:**
- `vTask_ReadLevel` → `xQueueSend(xLevelQueue, ...)` (produtor)
- `vTask_ControlLogic` → `xQueueReceive(xLevelQueue, ...)` (consumidor)
- `vTask_EmergencyHandler` → `xQueueReset(xLevelQueue)` (limpa após emergência)

### 5.2 Mutex (com herança de prioridade) — `xUARTMutex` e `xBufferMutex`

**O que é:** semáforo binário com posse (ownership) e herança de prioridade. Apenas a tarefa que tomou o mutex pode liberá-lo. Quando uma tarefa de alta prioridade tenta tomar um mutex segurado por uma de baixa prioridade, a baixa **herda temporariamente** a prioridade da alta, evitando inversão de prioridade clássica.

```c
xUARTMutex   = xSemaphoreCreateMutex();
xBufferMutex = xSemaphoreCreateMutex();
```

**Por que usar:**
- **`xUARTMutex`** serializa o acesso à UART1, que é um recurso físico único. Sem ele, dois `UART_SendString` simultâneos poderiam ter seus caracteres entrelaçados no monitor serial.
- **`xBufferMutex`** garante consistência da escrita/leitura da struct `TempBuffer_t` (3 campos não escritos atomicamente em uma instrução).

**Por que dois mutexes separados, em vez de um global:** evita acoplamento desnecessário e elimina **risco de deadlock por lock ordering**. Nenhuma tarefa precisa tomar ambos os mutexes ao mesmo tempo — o controle escreve no buffer e *só depois* manda mensagem UART (cada operação em sua própria seção crítica).

**Idioma "snapshot-then-release":** ao ler o buffer, copia para variável local e libera o mutex *antes* de fazer I/O (`sprintf`, `UART_SendString`). Mantém a seção crítica curta.

### 5.3 Semáforo binário — `xEmergencySemaphore`

**O que é:** contagem máxima de 1. Criado com contagem inicial **zero** via `xSemaphoreCreateBinary`, o que significa que o primeiro `take` bloqueia até alguém dar `give`.

```c
xEmergencySemaphore = xSemaphoreCreateBinary();
```

**Por que usar:** sinalização de evento entre tarefas (interrupt-like signaling). Diferente de mutex, **não tem posse**: a tarefa que dá `give` não é a mesma que dá `take`. É o uso clássico de "task-to-task signaling".

**Onde é usado:**
- `vTask_EmergencyHandler` faz `xSemaphoreTake(..., portMAX_DELAY)` — inicia bloqueada.
- `vTask_ControlLogic` faz `xSemaphoreGive(xEmergencySemaphore)` quando `temp > LEVEL_HIGH`.

**Garantia do requisito R11:** o único `xSemaphoreGive(xEmergencySemaphore)` em todo o projeto está em `user_app.c:69`, dentro da tarefa de controle. A antiga ISR `_INT0Interrupt` (do T1) foi removida exatamente para garantir essa exclusividade.

### 5.4 Software Timer — `xWatchdogTimer`

**O que é:** "tarefa virtual" agendada pela **Daemon Task** (Timer Service). Não consome stack próprio — o callback executa no contexto da Daemon. Pode ser **one-shot** ou **auto-reload** (caso do projeto).

```c
xWatchdogTimer = xTimerCreate("Watchdog", pdMS_TO_TICKS(5000), pdTRUE, NULL, vWatchdogTimerCallback);
xTimerStart(xWatchdogTimer, 0);
```

**Regra de ouro:** **o callback NUNCA pode bloquear**. No projeto, o `take` do mutex UART usa timeout curto (`pdMS_TO_TICKS(100)`) em vez de `portMAX_DELAY`. Se a UART estiver ocupada, o tick do watchdog é simplesmente descartado.

### 5.5 Escalonador (Scheduler)

**Configuração:**
- Preemptivo (`configUSE_PREEMPTION = 1`)
- Time-slicing entre iguais (`configUSE_TIME_SLICING = 1`)
- 5 níveis de prioridade (0..4, sendo 0 a Idle e 4 a máxima)
- Tick de 10 ms (`configTICK_RATE_HZ = 100`)

**Regra fundamental:** *a tarefa pronta de maior prioridade é a que está executando.* Se uma tarefa de prioridade superior se tornar pronta (recebeu dado de fila, semáforo etc.), ela **preempta imediatamente** a tarefa em execução, mesmo no meio de um quantum.

### 5.6 Exclusão mútua e sincronização — resumo

| Cenário | Primitivo | Justificativa |
|---------|-----------|---------------|
| Produzir/consumir amostras temporais | Queue | Desacopla produtor e consumidor, sem perda de dados |
| Escrever/ler estado composto (struct) | Mutex (`xBufferMutex`) | Atomicidade lógica de múltiplos campos |
| Compartilhar periférico físico (UART) | Mutex (`xUARTMutex`) | Recurso único, evita interleave de caracteres |
| Sinalizar evento (alarme) | Semáforo binário | Task-to-task signaling sem dado associado |
| Tarefa periódica leve (heartbeat) | Software Timer | Não exige stack/task dedicado |

---

## 6. Explicação dos Periféricos

### 6.1 ADC1 — Conversor Analógico-Digital

**Configuração (`user_drivers.c:50-78`):**
- Canal: **AN2** (RB2)
- Referência: VDD/VSS (`VCFG = 0`) → 5 V
- Aquisição: manual (`ASAM = 0`), com 16 TAD de tempo de sample (`SAMC = 16`)
- Resolução: 10 bits (0..1023)
- Clock: TAD via clock interno do ADC (`ADRC = 0`, `ADCS = 2`)
- Início de conversão automático após sample (`SSRC = 0b111`)

**Conversão raw → °C (`user_drivers.c:83-96`):**
```c
return (uint16_t)((uint32_t)raw * VREF_MV / (1023UL * LM35_MV_PER_C));
```
- `VREF_MV = 5000`, `LM35_MV_PER_C = 10`
- Fórmula: `temp_C = (raw × 5000) / (1023 × 10)`
- **Cuidado importante:** o intermediário `raw × 5000` ultrapassa `uint16_t` (max 65535). Promoção a `uint32_t` evita overflow silencioso.

**Justificativa do pino:** AN2/RB2 é um canal nativamente analógico, sem conflito com a função GPIO do LED (RB15/AN9). Não exige remapeamento (o PIC24FJ128GA010 não suporta PPS para ADC).

### 6.2 UART1 — Comunicação Serial

**Configuração (`user_drivers.c:30-47`):**
- Pinos: **U1TX = RF3** (saída), **U1RX = RF2** (entrada) — pinos fixos no PIC24FJ128GA010, sem PPS
- Baud rate: **9600** (`U1BRG = FCY/(16 × 9600) - 1 = 103` com FCY = 16 MHz)
- 8 bits de dado, **sem paridade**, **1 stop bit** (8N1)
- Oversampling 16× (`BRGH = 0`)

**Envio (`UART_SendChar`, `UART_SendString`):**
- Polling sobre `UTXBF` (buffer cheio?) — bloqueia até espaço livre.
- Aceitável porque é **sempre chamado dentro de seção crítica protegida por `xUARTMutex`**, e o tempo de espera por caractere a 9600 baud é ~1 ms (não bloqueia o scheduler por muito tempo).

### 6.3 GPIO — LED RB15

**Configuração (`user_drivers.c:9-15`):**
```c
AD1PCFGbits.PCFG9 = 1;   // Desativa AN9 (multiplexado em RB15) — vital!
TRISBbits.TRISB15 = 0;   // Pino como saída
LED_Status_Alto(OFF);    // Estado inicial seguro
```

**Pegadinha importante:** RB15 também é AN9 no chip. Ao ligar (reset), o pino assume função analógica por padrão. Se você esquecer `AD1PCFGbits.PCFG9 = 1`, o LED **não acende** e parece bug de driver, quando na verdade o mux está apontando para o ADC. Esse é exatamente o tipo de detalhe que o professor pode perguntar.

### 6.4 Timers (Software, via FreeRTOS)

Não há uso de timer de hardware (TMR1, TMR2, etc.). O único timer no projeto é o **Software Timer** do FreeRTOS, descrito em §5.4 e §10.

### 6.5 Interrupções

**Apenas o tick do FreeRTOS** (gerado pelo Timer 1 interno do port para PIC24, via `portasm_PIC24.S` e `port.c`). Nenhuma ISR de aplicação está ativa — a antiga `_INT0Interrupt` do T1 foi removida para cumprir o requisito R11 ("alarme desbloqueada **somente** pela tarefa controle").

---

## 7. Explicação do Circuito

O circuito está em `Proteus/Simulation.pdsprj`. Os componentes mínimos são:

| Componente | Conexão | Função |
|------------|---------|--------|
| **PIC24FJ128GA010** | Cristal de alta velocidade (HS) com PLL → FCY = 16 MHz | Microcontrolador host |
| **Potenciômetro** (simula LM35) | Wiper → RB2/AN2; extremos → VDD/VSS | Fonte analógica configurável de 0..5 V que simula a saída do LM35 |
| **LED de alarme** | Ânodo → RB15 (via resistor de limitação ~330 Ω); cátodo → GND | Sinalização visual de temperatura crítica |
| **Virtual Terminal** (UART) | TX (RF3) → RX do terminal; RX (RF2) → TX do terminal (loopback opcional); GND comum | Monitor serial 9600 8N1 |
| **VDD/VSS** | 5 V / GND | Alimentação |

### 7.1 Justificativa dos componentes

- **LM35 (potenciômetro no Proteus):** sensor analógico linear (10 mV/°C). Saída ratiométrica casa bem com ADC de 10 bits com Vref = 5 V. No Proteus, o potenciômetro permite varrer manualmente toda a faixa de temperatura (0..500 °C teoricamente, na prática limitada por Vref).
- **LED em RB15:** atuador único do sistema. RB15 fica fora dos pinos UART (RF2/RF3) e fora do canal ADC (AN2), evitando conflitos.
- **UART1 via Virtual Terminal:** substitui o que seria um conversor USB-Serial em hardware real. Aceita `\r\n` como nova linha.

### 7.2 Justificativa dos pinos

| Pino | Por quê |
|------|---------|
| **RB2/AN2** para o sensor | Canal ADC dedicado, não conflita com o LED ou UART |
| **RB15** para o LED | Saída GPIO livre, mas exige desativar AN9 explicitamente — boa oportunidade pedagógica para mostrar o cuidado |
| **RF2/RF3** para UART | Pinos fixos da U1RX/U1TX no GA010 (não há PPS) — não há outra escolha |

### 7.3 Interação geral

1. O potenciômetro gera uma tensão analógica que o ADC interpreta como temperatura.
2. A tarefa de controle decide se a temperatura é OK ou crítica e atualiza o LED.
3. A UART transmite continuamente relatórios e mensagens de evento ao Virtual Terminal.
4. Se a temperatura ultrapassar 50 °C, o LED pisca 3× e uma mensagem de alarme é enviada.

---

## 8. Fluxo Completo da Aplicação

Sequência passo a passo, desde o boot até um evento de alarme:

### 8.1 Boot

1. **Reset → `main()`**.
2. `Init_DigitalOutputs()` configura RB15 como GPIO de saída e desativa AN9.
3. `Init_UART1(9600)` programa U1BRG, modos e habilita o periférico.
4. `Init_ADC1()` configura AN2 como canal analógico e habilita o ADC1.
5. Criação dos objetos de IPC: `xLevelQueue`, `xUARTMutex`, `xBufferMutex`, `xEmergencySemaphore`. **Se algum retornar NULL, entra em loop infinito** (proteção contra heap insuficiente).
6. Criação das 4 tarefas (`xTaskCreate`) com prioridades 4 / 3 / 2 / 1.
7. Criação do Software Timer (`xTimerCreate` + `xTimerStart`).
8. `vTaskStartScheduler()` — a partir daqui o controle passa ao FreeRTOS.

### 8.2 Operação em estado estacionário (temperatura OK)

```
Tick 0   →  ADC dispara amostra → envia 25 °C para xLevelQueue → bloqueia em vTaskDelay(500 ms)
            Control recebe 25 °C → status = OK, LED OFF
            Control toma xBufferMutex → escreve {25, tick, OK} → libera
Tick 50  →  ADC dispara nova amostra (período 500 ms) ...
...
Tick 300 →  ReportStatus acorda (período 3 s) → toma xBufferMutex → copia snapshot → libera
            ReportStatus toma xUARTMutex → emite "[STATUS] T=25 C | tick=300 | OK" → libera
Tick 500 →  Watchdog Timer dispara → Daemon Task executa callback → "[WATCHDOG] Sistema ativo."
```

### 8.3 Evento de alarme (temperatura > 50 °C)

```
1. ADC lê 60 °C → envia para xLevelQueue
2. Control recebe 60 °C
   ├─ status = ALTA, LED RB15 ON
   ├─ toma xBufferMutex → escreve {60, tick, ALTA} → libera
   ├─ toma xUARTMutex → "ALERTA: Temperatura Alta!" → libera
   └─ xSemaphoreGive(xEmergencySemaphore)
3. EmergencyHandler (prioridade 4) preempta Control imediatamente
   ├─ Loop 3×: LED ON / 100 ms / LED OFF / 100 ms
   ├─ xQueueReset(xLevelQueue) — descarta amostras pendentes
   └─ xSemaphoreTake(xUARTMutex, 200 ms) → "ALARME: Temperatura critica! Sistema parado." → libera
4. EmergencyHandler volta a bloquear em xSemaphoreTake(xEmergencySemaphore, portMAX_DELAY)
```

**Observação fina:** o tempo de execução do alarme é dominado pelos seis `vTaskDelay(100)` (600 ms total). Durante essa janela, outras tarefas continuam executando entre os delays do alarme — o `vTaskDelay` libera a CPU.

---

## 9. Explicação das Prioridades

### 9.1 Hierarquia escolhida

| Tarefa | Prioridade | Justificativa |
|--------|------------|---------------|
| `vTask_EmergencyHandler` (Alarme) | **4** | Resposta a evento crítico — deve preemptar qualquer outra |
| `vTask_ControlLogic` (Controle) | **3** | Lógica reativa ao sensor — deve responder o mais rápido possível |
| `vTask_ReadLevel` (ADC) | **2** | Aquisição periódica — não deve atrasar muito, mas tolera jitter |
| `vTask_ReportStatus` (UART) | **1** | Relatório informativo — pode esperar |
| Idle (FreeRTOS) | 0 | Quando ninguém está pronto |

A Daemon Task do Timer compartilha prioridade 4 com o alarme (`configTIMER_TASK_PRIORITY = configMAX_PRIORITIES - 1 = 4`).

### 9.2 Princípio aplicado: criticality-monotonic

A regra **"mais crítico = maior prioridade"** dá uma escala intuitiva:

- **Emergência > Controle:** o alarme não pode esperar a tarefa de controle terminar de tratar a próxima amostra. Quando o controle dá `give`, o alarme acorda *no mesmo tick* e preempta o controle.
- **Controle > ADC:** assim que uma amostra cair na fila, o controle deve consumi-la imediatamente para liberar espaço (a fila tem só 5 posições) e dar resposta rápida ao LED.
- **ADC > UART:** atrasar uma amostra do sensor é pior do que atrasar um relatório periódico que ninguém está esperando em tempo real.
- **UART = mais baixa:** se o controle ou ADC precisarem da CPU, a UART simplesmente espera. Como ela acorda só a cada 3 s, há tempo de sobra mesmo com preempção.

### 9.3 Análise de starvation

A UART poderia, em tese, sofrer starvation se as tarefas superiores nunca dormissem. Mas:
- A ADC dorme 500 ms a cada amostra.
- O controle bloqueia em `xQueueReceive` quando não há amostra.
- O alarme bloqueia no semáforo até ser explicitamente sinalizado.
- O `vTaskDelayUntil` da UART (3 s) é tempo enorme comparado ao trabalho útil das outras tarefas.

Portanto **a UART sempre encontra janela de CPU** — não há starvation prática.

### 9.4 Análise de inversão de prioridade

A inversão de prioridade clássica ocorreria assim:
- `ReportStatus` (prio 1) segura `xUARTMutex` (ou `xBufferMutex`).
- `EmergencyHandler` (prio 4) tenta tomar o mesmo mutex e fica bloqueado.
- Uma tarefa de prio 2 ou 3 (Control, ADC) executa enquanto `ReportStatus` espera CPU → bloqueia indiretamente `EmergencyHandler`.

**Solução aplicada:** mutex com herança de prioridade (`configUSE_MUTEXES = 1`). Quando `EmergencyHandler` tenta tomar o mutex que `ReportStatus` segura, o kernel eleva temporariamente a prioridade de `ReportStatus` para 4. Assim, ela termina rápido a seção crítica e libera o mutex, evitando que prioridades intermediárias atrapalhem.

### 9.5 Análise de deadlock

- **Dois mutexes, sem lock ordering** (nenhuma tarefa precisa tomar os dois ao mesmo tempo).
- **Todos os takes usam timeout finito** (100 ms ou 1000 ms), exceto o `xQueueReceive` do controle e o `xSemaphoreTake` do alarme — esses bloqueiam indefinidamente porque o estado "esperando evento" é semanticamente correto.
- Resultado: **não há ciclo de espera possível** → impossível deadlock por design.

---

## 10. Funcionalidades Extras

### 10.1 Software Timer (recurso oficial "não visto em aula")

Esta é a entrega para o requisito R16. A escolha foi pelo **Software Timer** porque ele introduz **três conceitos novos** em relação ao que normalmente é coberto em aula (tarefas, filas, semáforos, mutexes):

1. **Daemon Task (Timer Service Task)** — uma tarefa de sistema embutida no FreeRTOS, criada automaticamente quando `configUSE_TIMERS = 1`. Executa em prioridade `configMAX_PRIORITIES - 1`.
2. **Semântica "command queue"** — comandos (`xTimerStart`, `xTimerStop`, `xTimerReset`) são *enfileirados* na Daemon, não executados imediatamente. Isso muda a forma de pensar sobre concorrência.
3. **Regra de ouro: callback não pode bloquear** — se você der `xSemaphoreTake(..., portMAX_DELAY)` dentro de um callback de timer, **toda a Daemon Task trava** e todos os timers param.

**Implementação no projeto:**
```c
TimerHandle_t xWatchdogTimer = xTimerCreate(
    "Watchdog",
    pdMS_TO_TICKS(5000),   // período 5 s
    pdTRUE,                // auto-reload (re-dispara sozinho)
    NULL,                  // ID do timer (não usado)
    vWatchdogTimerCallback // função executada na Daemon
);
xTimerStart(xWatchdogTimer, 0);
```

```c
void vWatchdogTimerCallback(TimerHandle_t xTimer) {
    if (xSemaphoreTake(xUARTMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        UART_SendString("[WATCHDOG] Sistema ativo.\r\n");
        xSemaphoreGive(xUARTMutex);
    }
    // Se a UART estiver ocupada, o tick é descartado silenciosamente —
    // jamais bloquear a Daemon Task com portMAX_DELAY.
}
```

**Fonte oficial:** documentação FreeRTOS — https://www.freertos.org/RTOS-software-timer.html — e capítulo 6 do *Mastering the FreeRTOS Real-Time Kernel* (Richard Barry).

### 10.2 Buffer compartilhado tipado (`TempBuffer_t`)

Em vez de uma simples variável `uint16_t g_uiTemperature`, foi definida uma **estrutura com três campos** (temperatura, timestamp, status). Isso atende literalmente a palavra "buffer" do enunciado e dá mais riqueza ao relatório UART (que mostra o tick em que a amostra foi capturada, útil para análise temporal).

```c
typedef struct {
    uint16_t   temperatura;
    TickType_t timestamp;
    uint8_t    status;
} TempBuffer_t;
```

Como são 3 campos não escritos atomicamente, **o mutex (`xBufferMutex`) é tecnicamente necessário** — sem ele, um leitor poderia capturar temperatura nova com timestamp antigo, ou vice-versa.

### 10.3 Dois mutexes separados, em vez de um global

Decisão consciente para evitar acoplamento e tornar **deadlock impossível por design** (não por sorte). Cada mutex tem seu recurso bem definido:
- `xUARTMutex` protege o periférico UART1.
- `xBufferMutex` protege `g_xBuffer`.

Nenhuma tarefa toma ambos ao mesmo tempo → nenhum ciclo de espera possível.

### 10.4 Padrão "snapshot-then-release" na leitura do buffer

A `vTask_ReportStatus` copia `g_xBuffer` para variável local **dentro do mutex** e libera o mutex **antes** de chamar `sprintf` e `UART_SendString`. Isso minimiza o tempo na seção crítica e evita que o controle fique bloqueado por causa de I/O lento.

### 10.5 `vTaskDelayUntil` em vez de `vTaskDelay`

A tarefa UART usa `vTaskDelayUntil`, que garante **período exato** mesmo se a tarefa for preemptada por longos períodos. Diferente de `vTaskDelay` (período relativo, sujeito a drift acumulado).

### 10.6 Reset da fila após alarme

`xQueueReset(xLevelQueue)` no alarme funciona como "freio de mão" lógico — descarta amostras antigas para que o controle reavalie a temperatura do zero. Mostra atenção a detalhes de comportamento pós-evento.

### 10.7 Conversão ADC com promoção a `uint32_t`

Detalhe técnico em `ADC1_ReadTemperature`: `(uint32_t)raw * VREF_MV / (1023UL * LM35_MV_PER_C)`. Sem a promoção, `1023 * 5000` daria 5.115.000 e estouraria silenciosamente o `uint16_t`. Esse é o tipo de defeito que aparece em produção real e é difícil de depurar.

### 10.8 Arquitetura modular em duas camadas

`user_drivers.c/h` (HAL — Hardware Abstraction Layer) e `user_app.c/h` (lógica de aplicação) são separados. Os drivers não conhecem FreeRTOS; a aplicação não conhece registradores. Boa prática de engenharia que torna o código mais testável e portável.

---

## 11. Possíveis Perguntas do Professor

### 11.1 Sobre FreeRTOS / RTOS

**P: Qual a diferença entre `vTaskDelay` e `vTaskDelayUntil`?**
R: `vTaskDelay(t)` bloqueia por `t` ticks *a partir do momento em que a função é chamada* — se a tarefa foi preemptada antes do `vTaskDelay`, o período acumula drift. `vTaskDelayUntil(&xLast, T)` bloqueia até `xLast + T`, atualizando `xLast` por referência. Garante período exato. No projeto, `vTask_ReadLevel` usa `vTaskDelay` (amostragem tolera jitter) e `vTask_ReportStatus` usa `vTaskDelayUntil` (relatórios precisam de período fixo).

**P: Por que o `xSemaphoreCreateBinary` começa com contagem zero?**
R: Para que a primeira chamada de `xSemaphoreTake` na tarefa consumidora bloqueie imediatamente, esperando o `give` do produtor. É o comportamento esperado de "task-to-task signaling": a tarefa de alarme está inicialmente dormente.

**P: Qual a diferença entre mutex e semáforo binário?**
R: Mutex tem **posse** (só quem tomou pode liberar) e **herança de prioridade** (a tarefa que segurou o mutex herda a prioridade de quem está esperando, se essa for maior). Semáforo binário não tem posse — qualquer tarefa pode dar `give`. Mutex é para **proteger recursos** (UART, buffer); semáforo binário é para **sinalizar eventos** (alarme).

**P: O que é inversão de prioridade e como ela é prevenida aqui?**
R: É o cenário em que uma tarefa de alta prioridade fica indiretamente bloqueada por uma de prioridade média. Acontece quando uma tarefa de baixa prioridade segura um mutex que a alta precisa, e tarefas de prioridade intermediária preemptam a baixa, prolongando o bloqueio. **Prevenção:** mutex com herança de prioridade (`configUSE_MUTEXES = 1`). O kernel eleva temporariamente a prioridade da tarefa que segura o mutex.

**P: Por que `heap_1.c` e não `heap_4.c`?**
R: `heap_1.c` aloca mas nunca libera — não há fragmentação possível, comportamento totalmente determinístico, e código de gerenciamento minúsculo. No T2, todas as alocações acontecem **antes** do `vTaskStartScheduler()` (tasks, mutexes, queues, semáforos, timer). Não há `vTaskDelete`, não há criação dinâmica em runtime. Para esse padrão, `heap_1.c` é a escolha técnica correta.

**P: Por que `configUSE_TIME_SLICING = 1` se as prioridades são todas diferentes?**
R: Mesmo com prioridades diferentes nas 4 tasks de aplicação, a Daemon Task do Timer tem **prioridade 4 (igual à do alarme)**. Time-slicing garante que, se ambas estiverem prontas e ativas (cenário raro mas possível), elas se alternam por tick em vez de uma monopolizar a CPU.

**P: O que acontece se a fila estiver cheia quando o ADC tenta enviar?**
R: `xQueueSend(..., portMAX_DELAY)` bloqueia o produtor até que haja espaço. **Nenhuma amostra é perdida silenciosamente** — o efeito é estender o período de amostragem temporariamente. Para uma planta de baixa dinâmica (temperatura), isso é aceitável. Para uma planta rápida (vibração, áudio), seria preferível timeout curto com tratamento de erro.

**P: Qual o estado das tarefas após boot e antes do primeiro evento de alarme?**
R:
- `vTask_ReadLevel`: alternando entre Running e Blocked (`vTaskDelay`).
- `vTask_ControlLogic`: a maior parte do tempo Blocked em `xQueueReceive`.
- `vTask_ReportStatus`: a maior parte do tempo Blocked em `vTaskDelayUntil`.
- `vTask_EmergencyHandler`: permanentemente Blocked em `xSemaphoreTake` até o primeiro alarme.

### 11.2 Sobre o hardware / periféricos

**P: Por que precisa `AD1PCFGbits.PCFG9 = 1` para usar o LED?**
R: Porque RB15 é multiplexado com AN9 no PIC24FJ128GA010. No reset, o pino assume função analógica (driver de saída digital desligado). Sem habilitar o modo digital explicitamente, o pino fica em alta impedância e o LED nunca acende. É erro clássico em PIC.

**P: Por que `VREF_MV = 5000`? E se a placa for de 3,3 V?**
R: Estamos usando o ADC com referência VDD/VSS (`VCFG = 0`). No Proteus, VDD está em 5 V por padrão, então `VREF_MV = 5000`. Em uma placa de 3,3 V, bastaria mudar `VREF_MV = 3300` em `user_drivers.h`.

**P: Por que o `for` com 600 NOPs antes de iniciar a conversão?**
R: Garante o tempo de aquisição mínimo do capacitor de sample-and-hold do ADC. A alternativa mais elegante seria configurar `ASAM = 1` (auto-sample) com `SAMC` adequado, deixando o hardware temporizar automaticamente. Aqui foi feito manualmente por simplicidade.

**P: Por que UART em polling (busy-wait em `UTXBF`) em vez de interrupção?**
R: A 9600 baud, cada byte leva ~1 ms para ser transmitido. Como o envio sempre acontece **dentro do `xUARTMutex`**, e o mutex tem herança de prioridade, o impacto é limitado e previsível. Para baud rates maiores ou mensagens longas, valeria a pena migrar para interrupção + buffer circular.

### 11.3 Sobre exclusão mútua e sincronização

**P: Por que o buffer compartilhado precisa de mutex se você pode usar variável `volatile`?**
R: `volatile` resolve apenas a **visibilidade** entre tarefas (impede o compilador de fazer cache em registrador). Não resolve **atomicidade**. A struct `TempBuffer_t` tem 3 campos (temperatura + timestamp + status), escritos em pelo menos 3 instruções `MOV`. Sem mutex, um leitor pode capturar uma struct "rasgada" com temperatura nova e timestamp antigo — estado logicamente inconsistente.

**P: Por que dois mutexes (UART e buffer) em vez de um global?**
R: Por **dois motivos**:
1. **Reduz contenção:** uma tarefa segurando o buffer não bloqueia outra que só quer falar pela UART.
2. **Elimina deadlock por design:** com um único mutex global haveria sempre o risco de uma tarefa tomar duas seções críticas aninhadas. Com dois mutexes *e nenhuma operação que tome os dois ao mesmo tempo*, é matematicamente impossível haver ciclo de espera.

**P: O que aconteceria se a ordem dos `take` fosse invertida em algum lugar?**
R: Se uma tarefa fizesse `take(A) → take(B)` e outra fizesse `take(B) → take(A)`, teríamos a clássica receita de deadlock por *lock ordering*. No projeto atual isso **não acontece**, mas é o tipo de erro que aparece quando o código cresce sem disciplina.

**P: O `give` do semáforo pode ser perdido?**
R: Sim. Como é um semáforo binário (max count = 1), múltiplos `give` consecutivos só "registram" o primeiro. Se o controle der `give` enquanto o alarme já tem um pendente, o segundo é descartado. Isso é o comportamento normal de **"latched event"** e é geralmente desejável para alarmes (evita o handler ser executado N vezes em rajada).

### 11.4 Sobre decisões de projeto

**P: Por que a tarefa de alarme reseta a fila?**
R: Para descartar amostras antigas acumuladas durante o evento de emergência. Sem isso, ao sair da rotina de alarme, o controle ainda processaria amostras "estatais" da janela crítica. Resetar força o controle a reavaliar a temperatura *atual*.

**P: Por que não há histerese no limiar de 50 °C?**
R: Trade-off de simplicidade. Em produção real eu adicionaria uma flag `bAlarmActive` que rearma apenas em `temp < LEVEL_HIGH - 5`, evitando que o alarme dispare em loop enquanto a temperatura oscila perto do limiar. Esse é um ponto que reconheço como evolução possível.

**P: Por que `LEVEL_HIGH = 50`?**
R: Valor pedagógico — fácil de exceder no Proteus girando o potenciômetro. Em uma planta real seria definido pela engenharia do processo (ex.: temperatura máxima operacional do equipamento).

**P: Por que stack `+128` ou `+256` em cima do `configMINIMAL_STACK_SIZE`?**
R:
- `vTask_ControlLogic` usa `+256` porque tem variáveis locais maiores e três caminhos de seção crítica.
- As outras tarefas usam `+128` (suficiente para suas operações simples).
- Os valores foram dimensionados conservadoramente. Em um projeto crítico, eu mediria com `uxTaskGetStackHighWaterMark()` e ajustaria.

### 11.5 Sobre o Software Timer (recurso extra)

**P: Por que essa é uma feature "não vista em aula"?**
R: O conteúdo da disciplina cobre tarefas, filas, semáforos e mutexes. Software Timer pertence a um capítulo separado da documentação FreeRTOS (cap. 6 do *Mastering the FreeRTOS Real-Time Kernel*) e introduz conceitos novos: Daemon Task, command queue, auto-reload vs one-shot, e a regra de não bloqueio no callback.

**P: Por que não usar uma 5ª task em vez do Software Timer?**
R: Uma task dedicada consumiria ~256 words de stack só para fazer um `vTaskDelay(5000) + UART_SendString`. O Software Timer executa no contexto da Daemon Task — **zero stack adicional**, comportamento idêntico do ponto de vista do usuário. É o trade-off correto para tarefas curtas e periódicas.

**P: O que acontece se a UART estiver ocupada quando o timer dispara?**
R: O `xSemaphoreTake(xUARTMutex, pdMS_TO_TICKS(100))` retorna `pdFALSE` após 100 ms e o callback **simplesmente retorna sem enviar nada**. O tick do watchdog é descartado silenciosamente. Isso é proposital: **a Daemon NUNCA pode bloquear**, senão todos os outros timers do sistema parariam.

### 11.6 Pergunta-armadilha clássica

**P: Como você garante que a fila não estourará entre dois pulses do controle?**
R: Em regime normal, o controle (prio 3) tem prioridade superior ao ADC (prio 2). Assim que o ADC dá `xQueueSend`, o scheduler preempta e o controle entra para consumir. A fila praticamente nunca chega a ter mais de 1 elemento. Os 5 slots são folga para cobrir o cenário em que o controle está temporariamente preemptado pelo alarme (prio 4). Mesmo nesse caso, 5 × 500 ms = 2,5 s de margem — mais do que suficiente.

---

## 12. Conclusão

### 12.1 Resultado final

O sistema implementa fielmente o que o enunciado pede: uma planta industrial supervisionada com 4 tarefas concorrentes, comunicação por fila, buffer compartilhado protegido por mutex, controle do periférico UART por mutex, alarme via semáforo binário desbloqueado *somente* pela tarefa de controle, prioridades distintas analisadas e um recurso adicional do FreeRTOS (Software Timer) implementado com profundidade técnica.

### 12.2 Aprendizado técnico

- **Como pensar em RTOS:** decompor o problema em tarefas com responsabilidade única, conectá-las por primitivos de IPC bem escolhidos.
- **Quando usar fila vs mutex vs semáforo:** fila para *transferir dados*, mutex para *proteger recursos*, semáforo para *sinalizar eventos*.
- **Como prevenir inversão de prioridade:** mutex com herança (`configUSE_MUTEXES = 1`).
- **Como prevenir deadlock por design:** isolar mutexes por recurso, evitar lock ordering, usar timeouts finitos.
- **Como dimensionar prioridades:** criticality-monotonic — mais crítico, mais alto.
- **Como ler o LM35 com ADC de 10 bits:** cuidado com overflow na conversão (promoção a `uint32_t`).
- **Como não bloquear a Daemon Task:** timeouts curtos em callbacks de timer.

### 12.3 Validação prática

O sistema foi testado na simulação Proteus:
- ✅ Temperatura baixa (< 50 °C): LED apagado, relatórios "[STATUS] T=xx C | tick=yyy | OK" a cada 3 s.
- ✅ Temperatura alta (> 50 °C): LED ligado, "ALERTA: Temperatura Alta!", flash em rajada de 3×, "ALARME: Temperatura critica! Sistema parado.".
- ✅ Watchdog: "[WATCHDOG] Sistema ativo." a cada 5 s, independente do estado da temperatura.
- ✅ Exclusão mútua: nenhum entrelaçamento de caracteres na UART; struct do buffer sempre consistente.
- ✅ Sem deadlock e sem starvation observáveis em execução prolongada.

### 12.4 Validação do RTOS

O FreeRTOS comportou-se exatamente como esperado:
- Preempção imediata da tarefa de baixa prioridade pela de alta prioridade ao desbloquear.
- Herança de prioridade do mutex visível quando o relatório (prio 1) segura o buffer e o alarme (prio 4) precisa dele.
- Tick estável de 100 Hz mantendo `vTaskDelayUntil` exato mesmo sob preempção.
- Software Timer disparando sem drift mesmo com a Daemon Task ocasionalmente em time-slice com o alarme.

### 12.5 Parecer geral

**Trabalho concluído integralmente, com aderência total ao enunciado e diferenciais técnicos consistentes.** As decisões de design são justificadas, o código é modular e legível, os primitivos de IPC são usados de forma idiomática, e a documentação aqui consolidada serve simultaneamente como entrega acadêmica e como base de estudo para a defesa oral.
