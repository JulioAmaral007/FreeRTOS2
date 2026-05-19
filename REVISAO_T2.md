# Revisão Acadêmica do Segundo Trabalho Prático — FreeRTOS / PIC24FJ128GA010

**Disciplina:** Sistemas Operacionais Embarcados — DEC7562 (UFSC)
**Aluno:** Júlio Cézar
**Data da revisão:** 2026-05-19
**Documento de referência:** `t2.png`, `Diagrama_T2.png`
**Implementação:** `main.c`, `user_app.c/h`, `user_drivers.c/h`, `FreeRTOSConfig.h`

---

## 1. Resumo Geral do Projeto

### 1.1 Objetivo

Sistema embarcado multitarefa que simula uma **pequena planta industrial supervisionada**, executando sobre o kernel **FreeRTOS v11.1.0** em um microcontrolador **PIC24FJ128GA010** (16 bits, Harvard, FCY = 16 MHz) simulado no ambiente **Proteus**. O sistema lê continuamente um sensor de temperatura **LM35** via ADC, classifica a leitura em dois estados (OK / ALTA), aciona um LED de alarme quando a temperatura ultrapassa o limiar crítico, transmite relatórios periódicos pela **UART1** e dispara uma rotina de alarme via semáforo binário.

### 1.2 Arquitetura

A arquitetura segue o diagrama `Diagrama_T2.png`:

```
   Sensor LM35 (AN2/RB2)
           │
           ▼
   vTask_ReadLevel ───── xLevelQueue (5 × uint16_t) ────► vTask_ControlLogic
                                                                  │
                                              ┌─────────────────────┼─────────────────────┐
                                              ▼                     ▼                     ▼
                                  g_xBuffer (TempBuffer_t)  xEmergencySemaphore       LED RB15
                                  protegido por xBufferMutex         │
                                              │                     ▼
                                              ▼          vTask_EmergencyHandler
                                  vTask_ReportStatus                 │
                                              │                     │
                                              └──────► xUARTMutex ◄─┘
                                                           │
                                                           ▼
                                                    UART1 (RF2/RF3)
```

Há também um **Software Timer** (`xWatchdogTimer`) executando heartbeat a cada 5 s a partir da Daemon Task, configurado como recurso adicional não visto em aula.

### 1.3 Mapeamento de hardware

| Recurso | Pino | Periférico |
|---------|------|------------|
| LM35 (sensor)         | RB2 / AN2          | ADC1, canal CH0SA = 2 |
| LED de alarme (alto)  | RB15               | GPIO (AN9 desativado via `AD1PCFG.PCFG9`) |
| UART1 TX (monitor)    | RF3                | U1TX |
| UART1 RX              | RF2                | U1RX |

---

## 2. Revisão Completa dos Requisitos do `t2.png`

### Requisito R1 — Sistema multitarefa em FreeRTOS sobre PIC24FJ128GA010, simulado no Proteus
**Status:** ✅ Implementado corretamente
**Local:** `main.c:14-49`, `Proteus/Simulation.pdsprj`, `FreeRTOSConfig.h`
**Análise técnica:** O `main()` inicializa o hardware, cria os objetos de sincronização, registra 4 tarefas (`xTaskCreate`) e inicia o escalonador preemptivo (`vTaskStartScheduler`). O `FreeRTOSConfig.h` confirma `configUSE_PREEMPTION = 1` e `configUSE_TIME_SLICING = 1`. O tick de 100 Hz (`configTICK_RATE_HZ`) gera quanta de 10 ms, suficiente para a granularidade temporal do problema.

### Requisito R2 — Planta industrial supervisionada (sensores, atuadores e supervisão)
**Status:** ✅ Implementado corretamente
**Local:** todo o `user_app.c` + `user_drivers.c`
**Análise técnica:** O sistema modela uma planta com:
- **Sensor:** LM35 lido por ADC1 (`ADC1_ReadTemperature`, `user_drivers.c:83-96`)
- **Atuador:** LED de alarme em RB15 (sinalização visual de temperatura crítica)
- **Supervisão:** monitor serial via UART1 recebe relatórios periódicos e mensagens de evento.

### Requisito R3 — Quatro tarefas obrigatórias
**Status:** ✅ Todas implementadas

| Tarefa pedida | Função | Arquivo:linha |
|---------------|--------|---------------|
| Ler sensor ADC | `vTask_ReadLevel` | `user_app.c:118` |
| Escrever no UART | `vTask_ReportStatus` | `user_app.c:76` |
| Controle | `vTask_ControlLogic` | `user_app.c:39` |
| Alarme | `vTask_EmergencyHandler` | `user_app.c:132` |

### Requisito R4 — Sensor → Controle via fila de mensagens
**Status:** ✅ Implementado corretamente
**Local:** `main.c:21` (criação), `user_app.c:126` (envio), `user_app.c:45` (recepção)
**Análise técnica:** `xLevelQueue` é uma fila FIFO com **5 posições de `uint16_t`** (10 bytes). A tarefa de leitura envia com `xQueueSend(..., portMAX_DELAY)` — bloqueia se a fila encher, garantindo que nenhuma amostra seja perdida silenciosamente. A tarefa de controle recebe com `xQueueReceive(..., portMAX_DELAY)`, que é o padrão idiomático para uma tarefa de evento: ela só sai do estado bloqueado quando há dado disponível, **sem polling** (zero CPU enquanto espera).

### Requisito R5 — Tarefa de controle escreve a temperatura em buffer compartilhado com a tarefa UART
**Status:** ✅ Implementado corretamente
**Local:** `user_app.h:14-18` (typedef), `user_app.c:26` (definição), `user_app.c:57-62` (escrita), `user_app.c:91-96` (leitura snapshot), `main.c:11,23` (mutex)
**Análise técnica:** O buffer compartilhado é a estrutura `TempBuffer_t`, com três campos:
```c
typedef struct {
    uint16_t   temperatura;  // ultima leitura em °C
    TickType_t timestamp;    // tick em que a amostra foi escrita
    uint8_t    status;       // TEMP_STATUS_OK / TEMP_STATUS_ALTA
} TempBuffer_t;
```
A instância global `g_xBuffer` é acessada exclusivamente sob `xBufferMutex` (mutex dedicado, separado do `xUARTMutex`). Padrão de uso:

- **Escrita (tarefa de controle):** após classificar o status e atualizar o LED, toma o mutex, escreve os três campos atomicamente em relação a outras tarefas e libera. As mensagens de alerta UART e o `give` do semáforo de emergência ficam **fora** da seção crítica para não prolongar a posse do mutex.
- **Leitura (tarefa UART):** toma o mutex, copia a struct para uma variável local (`xSnapshot = g_xBuffer`), libera. A formatação com `sprintf` e o envio pela UART acontecem **fora** da seção crítica — boa prática que evita segurar o mutex durante I/O lento.

**Por que o mutex é necessário aqui:** diferente de um simples `uint16_t`, a struct tem 3 campos cuja escrita não é atômica (são 3+ instruções `MOV`). Sem mutex, o leitor poderia capturar uma struct "rasgada" (temperatura nova com timestamp antigo, ou vice-versa). O mutex com herança de prioridade (`configUSE_MUTEXES = 1`) garante consistência e evita inversão de prioridade.

**Por que separar do `xUARTMutex`:** dois mutexes separados, cada um com seu recurso bem definido, evitam acoplamento. Uma tarefa que segura o buffer não bloqueia outra que só quer falar na UART, e vice-versa. Também elimina qualquer risco de deadlock por *ordering* — nenhuma tarefa precisa tomar ambos os mutexes ao mesmo tempo.

### Requisito R6 — Tarefa UART escreve a temperatura no monitor serial
**Status:** ✅ Implementado corretamente
**Local:** `user_app.c:76-115`
**Análise técnica:** `vTask_ReportStatus` acorda a cada 3 s via `vTaskDelayUntil` (período exato, sem drift), formata a string com `sprintf` em buffer local de 80 bytes e envia três strings delimitadoras pela UART. Boa prática: cabeçalho/rodapé visíveis no monitor serial, facilitando inspeção humana durante a apresentação.

### Requisito R7 — Tarefa de controle aciona alarme se a temperatura atingir o valor crítico
**Status:** ✅ Implementado corretamente
**Local:** `user_app.c:48-54` (condição) e `user_app.c:69` (`xSemaphoreGive(xEmergencySemaphore)`)
**Análise técnica:** quando `uiReceivedTemp > LEVEL_HIGH` (50 °C, definido em `user_app.c:14`), a tarefa de controle liga o LED RB15, escreve a entrada no buffer, emite um alerta UART e libera o semáforo de emergência. Boa ordem: o estado do display (LED) e do buffer é atualizado **antes** de notificar o alarme, evitando que o handler veja estado inconsistente.

### Requisito R8 — Tarefa de alarme inicia bloqueada em semáforo binário
**Status:** ✅ Implementado corretamente
**Local:** `main.c:24` (criação), `user_app.c:138` (bloqueio)
**Análise técnica:** `xSemaphoreCreateBinary` cria o semáforo com contagem inicial **zero**, então a primeira chamada `xSemaphoreTake(..., portMAX_DELAY)` em `vTask_EmergencyHandler` bloqueia indefinidamente — comportamento correto e exatamente o que o enunciado pede.

### Requisito R9 — Alarme desbloqueada SOMENTE pela tarefa de controle
**Status:** ✅ Implementado corretamente
**Análise técnica:** o `xSemaphoreGive(xEmergencySemaphore)` aparece **uma única vez** no projeto inteiro, em `user_app.c:69`, dentro de `vTask_ControlLogic`. Não há outro caller. Isto satisfaz o requisito de exclusividade da fonte do evento de alarme.

### Requisito R10 — Tarefa de alarme escreve mensagem de emergência no monitor serial
**Status:** ✅ Implementado corretamente
**Local:** `user_app.c:148-151`
**Análise técnica:** após o flash do LED (3 ciclos de 100 ms ON/OFF em RB15), o alarme reseta a fila e adquire o mutex da UART com timeout de 200 ms para emitir a string `"ALARME: Temperatura critica! Sistema parado.\r\n"`. O timeout finito (e não `portMAX_DELAY`) evita que o alarme fique preso caso outra tarefa segure a UART por muito tempo.

### Requisito R11 — Controle da UART com variáveis mutex
**Status:** ✅ Implementado corretamente
**Local:** `main.c:22` (criação), todas as tarefas que escrevem na UART acessam via mutex
**Análise técnica:** `xSemaphoreCreateMutex` cria um **mutex com herança de prioridade** (priority inheritance, habilitado por `configUSE_MUTEXES = 1` em `FreeRTOSConfig.h`). Os quatro produtores de UART (controle, status, alarme, watchdog) fazem `xSemaphoreTake` antes de qualquer `UART_SendString` e `xSemaphoreGive` imediatamente depois. A serialização das mensagens é garantida e não há risco de *interleaving* de caracteres.

Locais onde o `xUARTMutex` é tomado:
- `user_app.c:31` — `vWatchdogTimerCallback` (Daemon Task) — timeout 100 ms
- `user_app.c:65` — alerta "Temperatura Alta" em `vTask_ControlLogic` — timeout 100 ms
- `user_app.c:103` — `vTask_ReportStatus` — timeout 1000 ms
- `user_app.c:148` — `vTask_EmergencyHandler` — timeout 200 ms

Locais onde o `xBufferMutex` é tomado:
- `user_app.c:57` — escrita do buffer em `vTask_ControlLogic` — timeout 100 ms
- `user_app.c:91` — leitura snapshot em `vTask_ReportStatus` — timeout 1000 ms

### Requisito R12 — Configurações diferentes de prioridade e análise de exclusão mútua
**Status:** ✅ Implementado corretamente
**Local:** `main.c:32-35`
**Análise técnica:** as quatro tarefas têm **prioridades distintas**, em ordem decrescente de criticidade:

| Tarefa | Prioridade | Justificativa |
|--------|------------|---------------|
| `vTask_EmergencyHandler` | 4 (alta) | Evento crítico — deve preemptar todas |
| `vTask_ControlLogic`     | 3        | Lógica reativa ao sensor — deve responder rápido |
| `vTask_ReadLevel`        | 2        | Aquisição periódica — não pode atrasar muito |
| `vTask_ReportStatus`     | 1 (baixa)| Relatório informativo — pode ser preemptado |

Esta hierarquia obedece à regra **criticality-monotonic**: tarefas mais críticas e/ou de menor período recebem maior prioridade. O escalonamento se comporta como esperado:
- Quando o ADC produz um valor crítico, `vTask_ReadLevel` (P=2) desbloqueia `vTask_ControlLogic` (P=3), que preempta o produtor e processa imediatamente.
- O `give` no semáforo de emergência libera `vTask_EmergencyHandler` (P=4), que preempta o controle e flasha o LED de alarme.
- `vTask_ReportStatus` (P=1) só executa quando nenhuma outra está pronta, garantindo CPU para o caminho crítico.

Cuidados com **starvation**: a tarefa UART tem prioridade mais baixa, mas roda em intervalos fixos de 3 s (não é uma tarefa puramente reativa), o que evita inanição prolongada. O mutex com herança de prioridade impede inversão de prioridade clássica entre `ReportStatus` e `EmergencyHandler`.

### Requisito R13 — Implementar recurso da documentação FreeRTOS não visto em aula
**Status:** ✅ Implementado: **Software Timer** (`xTimerCreate`)
**Local:** `main.c:38-43` (criação), `user_app.c:29-35` (callback), `FreeRTOSConfig.h` (`configUSE_TIMERS = 1`)
**Análise técnica:** o projeto usa um **timer de software** auto-recarregável de 5 s que serve como heartbeat / watchdog visual, enviando `"[WATCHDOG] Sistema ativo.\r\n"` periodicamente. Pontos relevantes:
- `configUSE_TIMERS = 1` habilita a Daemon Task
- A Daemon Task roda em prioridade `configMAX_PRIORITIES - 1` = 4 (a mesma do alarme)
- O callback respeita a regra de ouro dos timers: **nunca bloquear**. Em vez de `portMAX_DELAY`, usa `pdMS_TO_TICKS(100)` — se a UART estiver ocupada, a amostra é descartada silenciosamente
- O timer é `pdTRUE` (auto-reload) — não há necessidade de `xTimerStart` em cada disparo

**Por que este recurso é "não visto em aula":** o conteúdo padrão da disciplina costuma cobrir tarefas, filas, semáforos e mutexes. Software timers vêm de um capítulo separado da documentação (capítulo 6 do *Mastering the FreeRTOS Real-Time Kernel*) e introduzem conceitos novos: a Daemon Task, semântica "command queue", auto-reload vs. one-shot, e a regra de não bloqueio dentro do callback.

---

## 3. Explicação das Tasks

### 3.1 `vTask_ReadLevel` (Sensor ADC) — Prioridade 2
- **Periodicidade:** 500 ms (`vTaskDelay`)
- **Função:** aciona conversão A/D no canal AN2, converte raw ADC em °C inteiro e injeta na fila
- **Bloqueio:** pode bloquear em `xQueueSend(..., portMAX_DELAY)` se a fila estiver cheia — proteção contra perda de amostras se a tarefa de controle estiver atrasada
- **Comunicação:** produtor de `xLevelQueue`
- **Crítica acadêmica:** o `vTaskDelay` é relativo (não absoluto). Para amostragem com período rigoroso seria preferível `vTaskDelayUntil`, mas 500 ms para um LM35 não exige precisão estrita.

### 3.2 `vTask_ControlLogic` (Controle) — Prioridade 3
- **Periodicidade:** event-driven (bloqueio na fila)
- **Função:** recebe temperatura, classifica em dois estados (OK / ALTA), liga ou desliga o LED RB15, escreve no buffer compartilhado sob mutex (com temperatura + timestamp + status) e, se a leitura for crítica, emite alerta UART e libera o semáforo de emergência
- **Bloqueio:** `xQueueReceive(..., portMAX_DELAY)`, `xSemaphoreTake(xBufferMutex, 100 ms)`, `xSemaphoreTake(xUARTMutex, 100 ms)`
- **Comunicação:** consumidor de `xLevelQueue`, produtor de `g_xBuffer` (via `xBufferMutex`) e `xEmergencySemaphore`, cliente do `xUARTMutex`
- **Análise:** a lógica de duas faixas (`> LEVEL_HIGH` vs. caso normal) é determinística e simples. A escrita no buffer fica em uma seção crítica curta (3 atribuições), seguida pelo envio de mensagens UART **fora** da seção crítica do buffer.

### 3.3 `vTask_ReportStatus` (UART) — Prioridade 1
- **Periodicidade:** 3 s (`vTaskDelayUntil` — exato, sem drift)
- **Função:** toma snapshot do buffer compartilhado e emite relatório formatado (temperatura + tick + status textual) pela UART
- **Bloqueio:** `vTaskDelayUntil`, `xSemaphoreTake(xBufferMutex, 1000 ms)`, `xSemaphoreTake(xUARTMutex, 1000 ms)`
- **Comunicação:** consumidor de `g_xBuffer` (via `xBufferMutex`), cliente do `xUARTMutex`
- **Análise:** o uso de `vTaskDelayUntil` garante período exato mesmo se a tarefa for preemptada por uma de prioridade superior. **Padrão snapshot-then-release** aplicado corretamente: o mutex do buffer é segurado apenas pelo tempo de copiar a struct (~3 instruções), e a formatação + I/O lenta da UART acontecem fora da seção crítica.

### 3.4 `vTask_EmergencyHandler` (Alarme) — Prioridade 4
- **Periodicidade:** event-driven, totalmente bloqueada até semáforo
- **Função:** flasha o LED RB15 3× (200 ms cada ciclo), reseta a fila de leituras (limpa histórico para evitar reprocessamento) e emite mensagem de emergência pela UART
- **Bloqueio:** `xSemaphoreTake(xEmergencySemaphore, portMAX_DELAY)`
- **Comunicação:** consumidor de `xEmergencySemaphore`, cliente do `xUARTMutex`
- **Análise:** o `xQueueReset` (`user_app.c:146`) é uma decisão interessante — depois do alarme, descarta amostras antigas da fila para que o controle reavalie a temperatura "do zero". Funciona como um "freio de mão" lógico.

### 3.5 Daemon Task (Timer Service) — Prioridade 4
- Executa o callback `vWatchdogTimerCallback` a cada 5 s
- Compartilha prioridade com o alarme, mas não disputa pelos mesmos recursos comuns com frequência
- Boa prática: timeout curto no take do mutex (100 ms)

---

## 4. Explicação dos Periféricos

### 4.1 ADC1 (AN2 / RB2)
- **Configuração:** auto-convert em `SAMP=0` (`SSRC = 0b111`), 16 ciclos TAD de amostragem, TAD via clock interno
- **Referência:** VDD/VSS (`VCFG = 0`) — assumindo AVdd = 5 V no Proteus
- **Conversão:** `temp_C = (raw × 5000) / (1023 × 10)`, usando promoção a `uint32_t` para evitar overflow no produto intermediário
- **Justificativa do pino:** AN2/RB2 está fora do bloco AN9 usado pelo LED, e é um canal nativamente analógico — não precisa de remapeamento
- **Cuidado:** o atraso de 600 NOPs antes de desligar SAMP serve para garantir o tempo mínimo de aquisição do capacitor de sample-and-hold do ADC. Funciona, mas seria mais elegante usar `AD1CON1bits.ASAM = 1` com `SAMC` dimensionado.

### 4.2 UART1 (RF2 / RF3, 9600 8N1)
- **Configuração:** 16× oversampling (`BRGH = 0`), 8N1 (`PDSEL=0`, `STSEL=0`)
- **Baud:** `U1BRG = FCY / (16 × 9600) - 1 = 103` (com FCY=16 MHz)
- **Justificativa do pino:** RF2/RF3 são os pinos físicos da U1RX/U1TX no PIC24FJ128GA010 (não há remapeamento PPS nesta variante GA — pinos fixos)
- **Envio:** bloqueante por polling (`while UTXBF`) — aceitável porque sempre é chamado **dentro do mutex**, e o tempo de espera no buffer de TX é curto a 9600 baud (~1 ms por caractere)

### 4.3 GPIO (RB15 — LED de alarme)
- **Configuração:** o pino RB15 corresponde a AN9 e precisa ser **explicitamente desligado** do mux analógico via `AD1PCFGbits.PCFG9 = 1` antes de usar como digital — feito corretamente em `Init_DigitalOutputs` (`user_drivers.c:10-15`)
- **TRIS = 0** configura como saída
- **Estado inicial:** OFF, como esperado

---

## 5. Explicação da Aplicação Embarcada

A aplicação simula uma planta industrial de monitoramento térmico (sala de servidores, reator, estufa, sistema HVAC). O fluxo lógico é:

1. **Aquisição (500 ms):** o sensor LM35 é amostrado periodicamente e o valor convertido em °C inteiro
2. **Comunicação:** a leitura é enviada à tarefa de controle por uma fila, que desacopla produtor e consumidor — o controle pode atrasar momentaneamente sem perder dados
3. **Decisão:** a tarefa de controle classifica em 2 estados (OK / ALTA), atualiza o LED de alarme e o buffer compartilhado
4. **Notificação rotineira (3 s):** a tarefa UART publica relatórios de temperatura
5. **Notificação assíncrona:** a tarefa de controle, em caso de temperatura crítica, sinaliza a tarefa de alarme via semáforo binário (padrão *interrupt-like signaling*)
6. **Resposta de emergência:** o alarme flasha o LED, reseta a fila e emite mensagem de emergência
7. **Watchdog (5 s):** mostra que o sistema continua vivo mesmo em períodos sem evento

**Por que esta aplicação é uma boa escolha pedagógica:** ela usa **todos** os primitivos de IPC do FreeRTOS pedidos no enunciado de forma natural e justificada — fila (produção-consumo desacoplada), buffer compartilhado (estado de display), mutex (recurso serial), semáforo binário (notificação de evento crítico), software timer (heartbeat periódico em background). Não há uso "forçado" de primitivo apenas para cumprir requisito.

---

## 6. Problemas Encontrados e Riscos

### 6.1 Falta de histerese no alarme crítico — risco médio (qualidade de produto)
**Local:** `user_app.c:48-70`
**Descrição:** enquanto a temperatura permanecer > 50 °C, a cada 500 ms a tarefa de controle dá `xSemaphoreGive(xEmergencySemaphore)`. Como o semáforo é binário, múltiplos gives consecutivos não acumulam — o alarme processa uma vez, volta a tentar take, recebe outro give, processa de novo, e assim por diante. Resultado prático: o alarme fica em loop contínuo (flash + reset queue + mensagem) enquanto a temperatura permanecer crítica.
**Impacto:** consumo de CPU, *flooding* da UART, e a fila sendo resetada perpetuamente significa que o sistema pode ficar incapaz de "ver" a temperatura voltar ao normal.
**Sugestão:** introduzir uma flag de estado (`bAlarmActive`) na tarefa de controle, dando o semáforo apenas na transição low→high; rearmar apenas após `temp < LEVEL_HIGH - HISTERESE`. Ou usar `vTaskDelay` longo dentro do alarme antes de re-armar.

### 6.2 `xQueueReset` chamado no alarme pode racear com a leitura — risco baixo
**Local:** `user_app.c:146`
**Descrição:** o alarme reseta a fila enquanto a tarefa do ADC pode estar tentando enviar nela. Como `xQueueReset` é thread-safe, não há crash; mas a tarefa do ADC pode ter sua amostra descartada sem aviso, ou pode passar a enviar para uma fila recém-resetada, criando uma janela de ambiguidade.
**Impacto:** baixo (perda eventual de uma amostra a cada alarme).
**Sugestão:** documentar a intenção explicitamente; ou suspender a tarefa do ADC com `vTaskSuspend` durante o tratamento de alarme.

### 6.3 `xTaskCreate` sem checagem de retorno — risco operacional baixo, didático médio
**Local:** `main.c:32-35`
**Descrição:** se o heap (`configTOTAL_HEAP_SIZE = 4096`) acabar durante a criação de uma tarefa, `xTaskCreate` retorna `pdFAIL` e o sistema entra no scheduler com tarefas faltando.
**Impacto:** em apresentação acadêmica, o professor pode pedir para mostrar checagem.
**Sugestão:** envolver cada `xTaskCreate` em `if (... != pdPASS) while(1);` análogo ao que já é feito para os objetos de sincronização (linhas 26-29).

### 6.4 Prioridade do Timer Service igual à do alarme — risco baixo
**Local:** `FreeRTOSConfig.h` define `configTIMER_TASK_PRIORITY = configMAX_PRIORITIES - 1 = 4`, igual à `vTask_EmergencyHandler`.
**Descrição:** quando uma estiver pronta e a outra rodando, há time-slicing entre elas (porque `configUSE_TIME_SLICING = 1`).
**Impacto:** mínimo, mas pode atrasar o início do tratamento de alarme em até 1 tick (10 ms) se a Daemon Task estiver no meio de uma execução de callback. Para um sistema com requisito de tempo real estrito isto deveria ser corrigido — para o trabalho está aceitável.
**Sugestão:** se quiser priorizar o alarme, mover o alarme para prioridade 5 (e aumentar `configMAX_PRIORITIES`) ou abaixar o `configTIMER_TASK_PRIORITY` para 3.

### 6.5 `g_xBuffer` inicializa zerado — relatório nos primeiros 3 s pode mostrar "T=0 C | tick=0 | OK"
**Local:** `user_app.c:26`
**Descrição:** antes da primeira amostra do ADC, a UART task poderia emitir um relatório com a temperatura zerada. Como o primeiro `vTaskDelayUntil` da UART acorda em 3 s (6 amostras do ADC já terão chegado), na prática isso não acontece — mas é frágil.
**Sugestão:** inicializar com um valor sentinel (ex.: `temperatura = 0xFFFF`) e suprimir o relatório enquanto este valor não mudar; ou usar `timestamp == 0` como flag de "ainda não populado".

### 6.6 Heap próximo do limite — risco baixo, mas monitorar
**Local:** `FreeRTOSConfig.h` (`configTOTAL_HEAP_SIZE = 4096`)
**Descrição:** com a adição do `xBufferMutex`, o uso estimado do heap subiu de ~3636 para ~3716 bytes (de 4096). Sobram ~380 bytes — suficiente para a operação atual, mas qualquer nova fila/mutex/tarefa pode estourar.
**Impacto:** baixo enquanto o escopo do T2 for mantido.
**Sugestão:** se for adicionar mais primitivos no T3, aumentar `configTOTAL_HEAP_SIZE` para 5120 ou 6144 (verificar se cabe nos 8 KB de SRAM total do PIC24FJ128GA010).

---

## 7. Funcionalidades Extras

| Recurso | Onde | Status |
|---------|------|--------|
| Software Timer com callback | `main.c:38-43` + `user_app.c:29-35` | **Recurso extra oficial** (não visto em aula) |
| Estrutura modular com camadas (drivers + app) | `user_drivers.*` + `user_app.*` | Boa engenharia |
| Buffer compartilhado tipado (struct) com mutex dedicado | `TempBuffer_t` + `xBufferMutex` | Atende literalmente o enunciado |
| Padrão *snapshot-then-release* na leitura do buffer | `user_app.c:91-96` | Minimiza tempo na seção crítica |
| Dois mutexes separados (UART + buffer) sem ordering | `xUARTMutex` + `xBufferMutex` | Evita deadlock por design |
| Mutex com herança de prioridade | `configUSE_MUTEXES = 1` | Padrão FreeRTOS |
| `vTaskDelayUntil` (período exato) | `user_app.c:87` | Boa prática para relatórios |
| Conversão ADC com promoção a `uint32_t` | `user_drivers.c:95` | Previne overflow |
| Reset de fila pós-alarme | `user_app.c:146` | Decisão de design defensável |
| Idioma diferenciado de bloqueio (timeout curto na Daemon, longo nas tasks) | `user_app.c:31` vs `user_app.c:103` | Mostra entendimento da regra "não bloquear na Daemon" |

---

## 8. Avaliação Final

### 8.1 Aderência ao enunciado: **Excelente**
Todos os requisitos do `t2.png` foram cobertos integralmente. O buffer compartilhado é uma struct tipada (`TempBuffer_t`) protegida por um mutex dedicado (`xBufferMutex`), satisfazendo literalmente tanto a palavra "buffer" quanto a exigência de "exclusões mútuas adequadas".

### 8.2 Robustez técnica: **Muito boa**
O sistema é determinístico, sem riscos reais de deadlock (dois mutexes separados sem ordering, todos com timeout finito) ou starvation crítica (hierarquia de prioridades coerente). A falta de histerese no alarme é o ponto mais frágil em uma operação real.

### 8.3 Qualidade de código: **Boa**
- Modularização clara entre drivers e aplicação
- Tipos próprios bem definidos (`TempBuffer_t`, constantes `TEMP_STATUS_*`)
- Padrões claros: snapshot-then-release na leitura do buffer; mensagens UART fora da seção crítica do buffer
- Convenção de prefixos (`vTask_`, `LED_`, `UART_`, `x...Mutex`) consistente
- Falta apenas a checagem de retorno de `xTaskCreate`

### 8.4 Diferenciais técnicos: **Acima da média**
A inclusão do **Software Timer** atende ao requisito de "recurso não visto em aula" com profundidade — não é apenas usar uma API nova, mas mostrar compreensão dos cuidados específicos da Daemon Task (não bloquear, timeout curto). O design do buffer com dois mutexes separados (em vez de um único mutex global) também mostra maturidade em evitar deadlock por design, não apenas por sorte.

### 8.5 Nota técnica sugerida (estimativa)

| Critério | Peso sugerido | Nota |
|----------|---------------|------|
| Implementação das 4 tarefas | 25% | 10/10 |
| IPC (fila, buffer, mutex, semáforo) | 25% | 10/10 |
| Análise de prioridades / exclusão mútua | 15% | 10/10 |
| Recurso adicional (Software Timer) | 15% | 10/10 |
| Robustez (race, deadlock, starvation) | 10% | 9/10 |
| Qualidade de código e documentação | 10% | 9.5/10 |
| **Total estimado** | 100% | **~9.8 / 10** |

### 8.6 Recomendações para a defesa oral

1. **Justifique a struct do buffer**: tenha uma frase pronta sobre por que o buffer precisa de mutex — "três campos (`temperatura`, `timestamp`, `status`) não são escritos atomicamente em uma instrução; sem mutex o leitor poderia capturar uma struct rasgada com temperatura nova e timestamp antigo".
2. **Justifique dois mutexes separados**: explique que separar `xBufferMutex` de `xUARTMutex` evita acoplamento e deadlock por *lock ordering* — nenhuma tarefa precisa tomar os dois ao mesmo tempo.
3. **Mostre o padrão snapshot-then-release**: aponte que a tarefa UART copia a struct para uma variável local e libera o mutex *antes* de chamar `sprintf` e a UART — isso minimiza o tempo na seção crítica e evita bloquear o controle por causa de I/O lento.
4. **Demonstre o software timer**: mostre que o `[WATCHDOG]` continua aparecendo no monitor mesmo quando a temperatura está estável — evidencia visualmente que o recurso extra está funcionando.
5. **Mostre a inversão de prioridade resolvida**: faça uma demonstração com o controle segurando o buffer e o relatório (P=1) esperando, então um alarme (P=4) chegando — a herança de prioridade do mutex eleva temporariamente o controle, e ele libera rapidamente.
6. **Reconheça a falta de histerese**: se o professor pedir uma melhoria, este é o gancho perfeito para mostrar maturidade — "sim, em produção eu adicionaria um estado de alarme já-ativo para evitar o re-trigger contínuo".
7. **Cite a fonte do recurso extra**: deixe claro que o software timer veio do capítulo 6 do *Mastering the FreeRTOS Real-Time Kernel*, com as regras específicas da Daemon Task.

### 8.7 Conclusão

O projeto cumpre integralmente o escopo do segundo trabalho prático, faz uso adequado e justificado de todos os primitivos de IPC pedidos, demonstra compreensão das implicações de escalonamento, prioridade e exclusão mútua, e oferece um recurso extra (software timer) com profundidade técnica suficiente para ser apresentado como "não trivial". As ressalvas listadas em §6 são, em sua maioria, do tipo "qualidade de produto" — não comprometem a avaliação acadêmica e podem ser convertidas em argumentos de maturidade técnica durante a defesa.

**Parecer geral: aprovado com distinção.**
