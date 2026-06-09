# FreeRTOS — Sistema de Monitoramento de Temperatura

Sistema embarcado de tempo real para monitoramento de temperatura com detecção de alarme, controle de LED e comunicação serial, implementado sobre FreeRTOS em um microcontrolador PIC24.

---

## 🚀 Visão Geral

Este projeto implementa um sistema de monitoramento de temperatura com resposta a eventos críticos, executando de forma concorrente e determinística sobre o kernel FreeRTOS v11.1.0.

O sistema lê continuamente a temperatura ambiente via sensor LM35, processa os dados em uma pipeline de tarefas com prioridades distintas e atua sobre um LED de alarme e uma interface serial UART. Quando a temperatura ultrapassa o limiar crítico de 50 °C, uma tarefa de emergência de alta prioridade é ativada por semáforo e aciona o alarme visual e o alerta serial.

**Público-alvo:** estudantes e professores de sistemas embarcados que desejam um exemplo prático e completo de aplicação FreeRTOS em microcontrolador PIC de 16 bits.

---

## 🛠️ Tecnologias Utilizadas

| Categoria | Item |
|-----------|------|
| **Microcontrolador** | PIC24FJ128GA010 — 16 bits, 128 KB Flash, 8 KB SRAM, FCY = 16 MHz |
| **RTOS** | FreeRTOS v11.1.0 |
| **Compilador** | MPLAB XC16 v2.10 |
| **IDE** | MPLAB X (baseado em NetBeans) |
| **Simulador** | Proteus (esquemático incluído) |
| **Sensor** | LM35 — saída linear 10 mV/°C |
| **Comunicação** | UART1 — 9600 baud, 8N1 (somente TX) |
| **Build system** | Make (Makefile gerado pelo MPLAB X) |

---

## 🎯 Principais Funcionalidades

- **Leitura periódica de temperatura** via ADC (canal AN2 / pino RB2), a cada 500 ms
- **Detecção de temperatura crítica** com limiar configurável (padrão: 50 °C)
- **Alarme visual** por LED em RB15 — pisca 3 vezes na detecção e fica aceso enquanto a temperatura se mantiver alta
- **Alarme serial** via UART — mensagem de alerta crítico imediata ao acionar o alarme
- **Relatório periódico** de status e temperatura a cada 3 s pela UART
- **Heartbeat via software timer** — mensagem `[WATCHDOG] Sistema ativo.` a cada 5 s, confirmando que o scheduler está rodando
- **Acesso concorrente seguro** ao buffer de temperatura e à UART usando mutexes com herança de prioridade
- **Pipeline produtor-consumidor** com fila de 5 elementos entre a tarefa ADC e a tarefa de controle

---

## 🏗️ Arquitetura

O sistema é estruturado em quatro tarefas de usuário mais um software timer, todos gerenciados pelo scheduler preemptivo do FreeRTOS.

### Fluxo principal

```
LM35 → ADC (AN2) → vTask_ReadLevel → xLevelQueue → vTask_ControlLogic
                                                          │
                                          ┌───────────────┼───────────────┐
                                          │               │               │
                                       LED RB15   xBufferMutex   xEmergencySemaphore
                                                       │                  │
                                               vTask_ReportStatus  vTask_EmergencyHandler
                                                  (UART, 3 s)       (UART + LED, evento)
```

O software timer `xWatchdogTimer` dispara independentemente a cada 5 s e envia o heartbeat pela UART, protegido pelo mesmo `xUARTMutex`.

### Hierarquia de tarefas

| Tarefa | Prioridade | Período / Ativação | Responsabilidade |
|--------|-----------|-------------------|-----------------|
| `vTask_EmergencyHandler` | 4 (máxima) | Semáforo binário | Pisca LED 3×, envia alerta UART |
| `vTask_ControlLogic` | 3 | Bloqueio na fila | Processa ADC, atualiza buffer, controla LED, sinaliza alarme |
| `vTask_ReadLevel` | 2 | 500 ms | Lê LM35 via ADC e envia para fila |
| `vTask_ReportStatus` | 1 (mínima) | 3 s | Snapshot do buffer e envio de relatório via UART |
| Timer Service Task | 4 | 5 s (auto-reload) | Heartbeat UART via `vWatchdogTimerCallback` |

### Objetos de IPC

| Objeto | Tipo | Finalidade |
|--------|------|-----------|
| `xLevelQueue` | Fila (5 × `uint16_t`) | ADC → Controle |
| `xUARTMutex` | Mutex (herança de prioridade) | Exclusão mútua da UART1 |
| `xBufferMutex` | Mutex (herança de prioridade) | Protege `g_xBuffer` (Controle ↔ Report) |
| `xEmergencySemaphore` | Semáforo binário | Controle → EmergencyHandler |

---

## 📂 Estrutura do Projeto

```
FreeRTOS/
├── main.c                  # Ponto de entrada: inicialização, IPC, tasks, scheduler
├── user_app.c / .h         # Implementação das tarefas e callback do timer
├── user_drivers.c / .h     # HAL: LED, UART1, ADC (LM35)
├── FreeRTOSConfig.h        # Configuração do kernel FreeRTOS
├── Proteus/
│   └── Simulation.pdsprj   # Esquemático de simulação (PIC24 + LM35 + LED + UART)
├── Diagrama_T2.png         # Diagrama da arquitetura do sistema
├── nbproject/              # Arquivos de projeto do MPLAB X
├── build/                  # Arquivos objeto gerados pela compilação
└── dist/                   # Binários finais (.elf, .hex, .map)
```

**`main.c`** — cria os objetos de IPC, registra as tarefas, inicia o software timer e entrega o controle ao scheduler.

**`user_app.c`** — contém toda a lógica da aplicação: leitura, processamento, alarme e relatório.

**`user_drivers.c`** — abstração de hardware para LED (RB15), UART1 (RF3, TX only) e ADC (AN2/RB2).

**`FreeRTOSConfig.h`** — configura tick em 100 Hz, heap de 4 096 bytes (heap_1.c), 5 prioridades e timers de software habilitados.

---

## ⚙️ Como Executar

### Pré-requisitos

- **MPLAB X IDE** v6.x ou superior
- **MPLAB XC16** v2.10 (compilador C para PIC24/dsPIC)
- **Proteus** 8.x (opcional — para simulação)
- Hardware real: PIC24FJ128GA010, sensor LM35, LED, resistores e fonte 5 V

### Compilação

Clone ou baixe o repositório e abra o projeto no MPLAB X:

1. Abra o MPLAB X IDE
2. Vá em **File → Open Project** e selecione a pasta `FreeRTOS/`
3. Clique em **Build** (martelo) ou use os comandos via terminal:

```bash
make build     # compila o projeto
make clean     # remove arquivos objeto
make clobber   # remove todos os artefatos de build
```

Os binários são gerados em `dist/default/production/`:

- `FreeRTOS.production.hex` — firmware para gravação no PIC
- `FreeRTOS.production.elf` — binário com símbolos de debug

### Gravação no hardware

Use o **MPLAB IPE** ou qualquer gravador compatível com PIC24 (PICkit 4, ICD 4, Snap) para gravar o `.hex` no microcontrolador.

### Simulação no Proteus

Abra `Proteus/Simulation.pdsprj` no Proteus e pressione **Play**. O terminal virtual exibe as mensagens UART em tempo real. O potenciômetro no circuito simula a variação de tensão do LM35.

---

## 🔧 Configurações Relevantes

### Limiar de temperatura

Definido diretamente em `user_app.c`:

```c
if (uiReceivedTemp > 50) { // 50 °C → alarme
```

Altere o valor `50` para ajustar o ponto de disparo do alarme.

### Referência de tensão do ADC

Configurada em `user_drivers.h`:

```c
#define VREF_MV       5000   // tensão de referência em mV (AVdd)
#define LM35_MV_PER_C   10  // sensibilidade do LM35: 10 mV/°C
```

Ajuste `VREF_MV` para `3300` se o circuito operar em 3,3 V.

### Heap do FreeRTOS

Configurado em `FreeRTOSConfig.h`:

```c
#define configTOTAL_HEAP_SIZE  4096
```

O uso estimado é de ~3 636 bytes. Adicionar novas tarefas ou objetos de IPC pode estourar o heap — monitore com `xPortGetFreeHeapSize()`.

---

## 📈 Melhorias Futuras

- Adicionar histerese no alarme de temperatura para evitar re-disparo contínuo na fronteira do limiar
- Implementar recepção UART (RX) para permitir reconfiguração do limiar em tempo de execução
- Substituir `heap_1.c` por `heap_4.c` para permitir criação e exclusão dinâmica de tarefas
- Adicionar verificação do retorno de `xTaskCreate()` para detecção de falhas de alocação
- Registrar o timestamp das leituras usando `xTaskGetTickCount()` nos relatórios UART

---

## 👨‍💻 Desenvolvedor

**Júlio Cézar** — Universidade Federal de Santa Catarina (UFSC)

---

## 📚 Contexto Acadêmico

Projeto desenvolvido para a disciplina **DEC7562 — Sistemas Embarcados II** (UFSC).

O trabalho demonstra na prática o uso de um RTOS em microcontrolador de recursos limitados, abordando conceitos como escalonamento preemptivo por prioridades, comunicação entre tarefas via filas e semáforos, exclusão mútua com herança de prioridade e temporização por software timer.
