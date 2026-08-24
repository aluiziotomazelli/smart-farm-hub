# Plano de Refatoração: Load Control & Tank Controller

Este plano define a estratégia passo a passo para migrar o código atual (`smart-farm-hub`) para a nova arquitetura baseada em Domain Controllers, QueueSets e Snapshots, sem quebrar o sistema de uma vez. O plano segue estritamente as regras de arquitetura do projeto (Test-Driven, Host-first, Interfaces e HAL).

---

## Fase 0: Extensão do HAL FreeRTOS
**Objetivo:** Adicionar as primitivas de `QueueSet` e `QueueOverwrite` ao HAL, uma vez que elas ainda não existem no projeto.

1. **Atualizar `i_hal_freertos.hpp`:**
   - Adicionar `virtual QueueSetHandle_t queue_create_set(const UBaseType_t xEventQueueLength) = 0;`
   - Adicionar `virtual BaseType_t queue_add_to_set(QueueSetMemberHandle_t xQueueOrSemaphore, QueueSetHandle_t xQueueSet) = 0;`
   - Adicionar `virtual QueueSetMemberHandle_t queue_select_from_set(QueueSetHandle_t xQueueSet, TickType_t const xTicksToWait) = 0;`
   - Adicionar `virtual BaseType_t queue_overwrite(QueueHandle_t xQueue, const void * pvItemToQueue) = 0;`

2. **Atualizar Mocks e Implementações:**
   - Implementar na classe real `HalFreertos`.
   - Adicionar os `MOCK_METHOD` correspondentes na classe `MockHalFreertos` (`host_test_common/`).

---

## Fase 1: Fundações e Contratos (TDD Phase 1)
**Objetivo:** Definir os tipos de dados básicos, interfaces e mocks. Nenhuma alteração no código de produção existente nesta fase.

1. **Tipos Primitivos (`include/load_types.hpp` e novos headers):**
   - Definir `LoadProfile` (struct com configs estáticas).
   - Definir enumerações de urgência e preferência de fonte.
   - Definir os structs `LoadIntent` e `FillRequest` (herança ou composição).
   - Definir a tabela de prioridades `PriorityConfig`.

2. **Interfaces (`include/interfaces/`):**
   - Criar `ILoadDomainController` (`virtual LoadIntent get_current_intent() = 0`).
   - Criar `i_hal_energy_monitor.hpp` (`virtual bool is_solar_available() = 0`, etc.).

3. **Mocks (`host_test_common/`):**
   - Criar `MockEnergyMonitor` usando GMock.
   - Criar `MockLoadDomainController`.

---

## Fase 2: Domínios Isolados (Testáveis no Host)
**Objetivo:** Construir as "pontas" que alimentam a LCT, garantindo que suas lógicas de negócio estejam 100% testadas.

1. **Energy Monitor:**
   - Criar `NullEnergyMonitor` (para facilitar desenvolvimento e setups sem sensor).
   - Criar `HalEnergyMonitor` (wrapper das GPIOs de leitura do inversor e rede).
   - *Testes:* Validar a inicialização e leitura de GPIO via mocks.

2. **Controladores Triviais (`FridgeController`, `FreezerController`, etc.):**
   - Implementar controllers que recebem um `LoadProfile` no construtor e emitem sempre um `LoadIntent` estático correspondente.
   - *Testes:* Validar se os intents gerados estão corretos para cada tipo de carga.

3. **Tank Controller (`TankController`):**
   - Implementar a lógica de apuração de nível e as regras de horário.
   - Implementar a transição dinâmica da urgência (`OPPORTUNISTIC` -> `NORMAL` -> `URGENT`).
   - *Testes Exaustivos no Host:* Simular variações de nível (subindo/descendo) em diferentes horários (dia/noite) e validar a emissão correta do `FillRequest`.

---

## Fase 3: O Cérebro da LCT (Core Logic Engine)
**Objetivo:** Implementar o algoritmo de arbitragem **puramente em C++**, desconectado do FreeRTOS. Isso permite testar todos os tiebreakers e o Scenario C instantaneamente no host.

1. **Criar `LoadControlEngine` (sem dependência de Queue/Task):**
   - Mantém o mapa de estado interno das cargas.
   - Recebe dados via chamadas de método explícitas (`on_solar_update`, `on_load_intent`, etc.).
   - Aplica as Regras de Prioridade (Section 5.9).
   - Mantém a Máquina de Estados de Janela (Scenario C).

2. **Testes do Engine (Crucial):**
   - Testar Tiebreakers: injetar cargas simultâneas e verificar quem é desligado/movido para Grid.
   - Testar Scenario C: Simular freezer desligando, verificar se a FSM entra em `FILLING_SOLAR_WINDOW`, simular timeout, verificar fallback.
   - Testar Comportamento de Override (`AUTO` vs `SOLAR` vs `GRID`).

---

## Fase 4: O Wrapper FreeRTOS (Synchronization)
**Objetivo:** Envolver o Engine (Fase 3) nas primitivas de sistema operacional (QueueSet) e configurar a saída para a UI.

1. **A Task e o QueueSet (`LoadControlTask`):**
   - Instanciar `QueueSet`, `solar_queue` (overwrite), array de `status_queues` (overwrite).
   - Instanciar `command_queue` fora do QueueSet.
   - Implementar o loop exato detalhado no documento (Section 5.8: Drena comandos -> QueueSet Select -> Tick timeout).

2. **O Snapshot da UI:**
   - Definir `UiSnapshot` struct.
   - Implementar o spinlock (`portMUX_TYPE`) e o time-based check (10Hz) dentro do loop da LCT.
   - *Testes:* Como FreeRTOS interage com mocks é mais difícil no host, usar o `RealFreeRTOSHAL` provido pelo projeto para validar o comportamento do QueueSet no Linux.

---

## Fase 5: Integração e Desacoplamento do SystemState
**Objetivo:** "Desplugar" os handlers do antigo `SystemState` e "Plugar" nas novas Filas.

1. **Refatorar Handlers do ESP-NOW:**
   - `SolarSensorHandler` escreve na `solar_queue` (em vez de `SystemState.solar`).
   - `LoadControlHandler` escreve na `status_queue` específica do nó (em vez de atualizar diretamente no `SystemState`).
   - *Nota:* O payload do `TankReport` deve ser direcionado para atualizar o `TankController`.

2. **Refatorar a UI (Desacoplar do SystemState original):**
   - Atualizar a `UIController` e todas as telas (PUMP_SCREEN, SOLAR_SCREEN, etc.) para aceitarem e lerem do novo `UiSnapshot` gerado pela LCT.
   - Destruir o objeto monolítico `SystemState` antigo.

---

## Fase 6: On-Target Validation
**Objetivo:** Testar tudo na ESP32 real.

1. Rodar `test_apps/test_build` para garantir que o código compila para a arquitetura target (xtensa).
2. Fazer o flash no hub e testar os cenários ao vivo:
   - Resposta a quedas de nível de água.
   - Comportamento de Overrides via botões físicos.
   - Mudanças de potência solar lidas do sensor real.
