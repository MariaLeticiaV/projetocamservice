## 📄 Radar Eletrônico Zephyr (MPS2-AN385/QEMU)

Este documento detalha a implementação de um sistema de radar eletrônico simplificado utilizando o **Zephyr RTOS** na plataforma emulada **mps2\_an385 (QEMU)**.

-----

## 1\. 📝 Descrição do Projeto

O objetivo principal deste projeto é consolidar conhecimentos em sistemas embarcados, aplicando multithreading, sincronização e comunicação inter-tarefas (ZBUS e Filas de Mensagens) do Zephyr.

O sistema simula um radar de controle de velocidade com as seguintes funcionalidades:

  * **Detecção e Cálculo de Velocidade:** Utiliza dois sensores simulados (**GPIO 5** e **GPIO 6**) para medir o tempo de passagem e calcular a velocidade.
  * **Classificação de Veículo:** Implementa uma **Máquina de Estados** baseada em pulsos (eixos) no primeiro sensor (GPIO 5) para classificar o veículo como **Leve** (2 eixos) ou **Pesado** ($\ge 3$ eixos).
  * **Detecção de Infração:** Compara a velocidade calculada com limites configuráveis (Kconfig) para cada tipo de veículo.
  * **Feedback Visual Colorido:** Exibe o status da passagem (Normal, Alerta ou Infração) no console QEMU (**Display Dummy**) utilizando **códigos de cores ANSI** (Verde, Amarelo, Vermelho).
  * **Captura de Placa (Simulada):** Em caso de infração, aciona o **Serviço de Câmera (LPR)** via **ZBUS** para registrar a placa.

-----

## 2\. 🏗️ Descrição da Arquitetura de Software

A arquitetura do projeto é baseada em quatro threads principais que se comunicam usando mecanismos robustos do Zephyr RTOS.

### 2.1. Organização em Threads

| Thread | Responsabilidade | Comunicação |
| :--- | :--- | :--- |
| **Thread Sensores** | Monitora interrupções de GPIO (Sensores S1 e S2). Implementa a Máquina de Estados para contar eixos e mede o *delta time* da passagem. | **Saída:** Fila de Mensagens (`sensor_msg_queue`) |
| **Thread Principal / Controle** | Recebe dados dos sensores, calcula a velocidade (km/h), aplica os limites de velocidade, determina o status (Normal/Alerta/Infração) e aciona a Câmera. | **Entrada:** Fila de Mensagens. **Saída:** ZBUS (`display_data_chan`). **Requisição/Resposta:** ZBUS (`chan_camera_evt`). |
| **Thread Display** | Aguarda status de exibição via ZBUS. Formata a string de saída com códigos de cores ANSI e imprime no console QEMU (Display Dummy). | **Entrada:** ZBUS (`display_data_chan`). |
| **Thread Câmera/LPR (Módulo)** | **Requisição:** Aguarda comando de captura via ZBUS interno. **Processamento:** Simula o tempo de LPR e a lógica de falha de leitura (baseada em Kconfig). | **Resposta:** ZBUS (`chan_camera_evt`). |

### 2.2. Lógica de Classificação e Velocidade (Thread Sensores)

1.  **S1 Ativado (GPIO 5):**
      * **Estado:** `STATE_IDLE` $\rightarrow$ `STATE_S1_ACTIVE`.
      * Inicia a contagem de tempo (`start_time_ms`).
      * Conta o primeiro eixo (`axles_count = 1`).
2.  **S1 Ativado Novamente:**
      * **Estado:** `STATE_S1_ACTIVE`.
      * Se o pulso for distinto (após debounce), incrementa `axles_count`.
3.  **S2 Ativado (GPIO 6):**
      * **Estado:** `STATE_S1_ACTIVE` $\rightarrow$ `STATE_S2_ACTIVE`.
      * Finaliza a contagem de tempo (`end_time_ms`).
      * Calcula `delta_time_ms`.
      * Se `axles_count` for 1, é ajustado para **2 (Leve)**. Se $\ge 2$, é mantido (potencialmente **Pesado**).
      * Envia `{delta_time_ms, final_axles}` para a **Thread Principal** via Fila de Mensagens.

### 2.3. Lógica do Display

A Thread Display utiliza códigos de escape ANSI (por exemplo, `\x1b[31m` para Vermelho) para colorir o texto no console do QEMU com base no `radar_status` recebido:

| Status | Condição | Cor ANSI |
| :--- | :--- | :--- |
| `STATUS_NORMAL` | Velocidade $\le$ limite de Alerta | **Verde** |
| `STATUS_WARNING` | Limite de Alerta $<$ Velocidade $\le$ Limite de Infração | **Amarelo** |
| `STATUS_INFRACTION` | Velocidade $>$ Limite de Infração | **Vermelho** |

-----

## 3\. ⌨️ Instruções de Configuração e Execução

### 3.1. Pré-requisitos

Certifique-se de ter o ambiente de desenvolvimento Zephyr configurado (ZEPHYR\_TOOLCHAIN\_VARIANT, ZEPHYR\_BASE, etc.).

### 3.2. Configuração e Build

1.  **Navegue** para o diretório raiz do projeto:

    ```bash
    cd /caminho/para/projeto_radar
    ```

2.  **Build** o projeto para a placa `mps2_an385`:

    ```bash
    west build -b mps2_an385
    ```

### 3.3. Execução no QEMU

Execute a imagem gerada (que inclui a configuração de *overlay* para os GPIOs 5 e 6):

```bash
west build -t run
```

### 3.4. Simulação de Passagem de Veículos (Input GPIO)

O Zephyr QEMU permite injetar eventos de GPIO via console de monitoramento QEMU.

1.  Enquanto o programa estiver rodando, abra o console de monitoramento do QEMU (**Ctrl+A**, depois **C**).
2.  Use o comando `sendkey` para simular a ativação (borda ascendente) dos pinos GPIO 5 (S1) e 6 (S2).

A **velocidade (km/h)** é calculada a partir do **tempo** entre o primeiro pulso de S1 e o pulso de S2.

| Veículo (Simulado) | GPIO 5 (S1) | GPIO 5 (S1) | GPIO 6 (S2) | Tempo S1 $\rightarrow$ S2 | Velocidade (1m) | Status (Leve=80) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Leve Rápido** | Pulso 1 | (Pausa 20ms) | Pulso 2 | $0.035$s | $102.8$ km/h | **INFRAÇÃO** |
| **Pesado Normal** | Pulso 1 | Pulso 2 | Pulso 3 | $0.060$s | $60.0$ km/h | **NORMAL** |

**Comandos de Exemplo (Leve e Rápido - Infração):**

| Comando | Tempo Aprox. | Efeito |
| :--- | :--- | :--- |
| `sendkey 5` | $t_0$ | Ativa S1. Inicia contagem de tempo. Eixo 1. |
| (Esperar 20 ms) | $t_0 + 20$ms | (Pausa mínima para debounce do hardware simulado) |
| `sendkey 5` | $t_0 + 20$ms | Ativa S1 novamente. **Eixo 2**. |
| (Esperar 15 ms) | $t_0 + 35$ms | Pausa para velocidade alta (35ms = $102.8$ km/h) |
| `sendkey 6` | $t_0 + 35$ms | Ativa S2. Passagem completa\! Envia Infração. |

-----

## 4\. ⚙️ Opções Kconfig

As seguintes opções são configuráveis no arquivo `Kconfig` na raiz do projeto, ajustando os parâmetros do radar:

| Opção Kconfig | Descrição | Exemplo Padrão |
| :--- | :--- | :--- |
| `CONFIG_RADAR_SENSOR_DISTANCE_MM` | Distância entre os sensores S1 e S2 (em milímetros). | `1000` (1 metro) |
| `CONFIG_RADAR_SPEED_LIMIT_LIGHT_KMH` | Limite de velocidade para veículos leves (2 eixos). | `80` km/h |
| `CONFIG_RADAR_SPEED_LIMIT_HEAVY_KMH` | Limite de velocidade para veículos pesados ($\ge 3$ eixos). | `60` km/h |
| `CONFIG_RADAR_WARNING_THRESHOLD_PERCENT` | Percentual do limite que ativa o display **Amarelo** (Alerta). | `90` (%) |
| `CONFIG_RADAR_CAMERA_FAILURE_RATE_PERCENT` | Porcentagem de chance da câmera simular uma falha na leitura da placa. | `10` (%) |

-----

## 5\. 🧪 Instruções para Rodar os Testes

O projeto inclui testes de unidade automatizados (`ztest`) para validar as lógicas críticas (cálculo de velocidade e classificação/infração).

1.  **Certifique-se** de ter o projeto *buildado* (`west build -b mps2_an385`).

2.  **Execute** o alvo de teste `run_test`:

    ```bash
    west build -t run_tests
    ```

3.  O console QEMU será inicializado e o *framework* `ztest` executará automaticamente os testes definidos em `tests/src/test_radar.c`, verificando:

      * Se o cálculo de velocidade para tempos conhecidos está correto.
      * Se a classificação de status (NORMAL, WARNING, INFRACTION) está correta para limites leves e pesados.

O resultado final indicará se todos os testes (`test_speed_calculation`, `test_light_vehicle_infraction`, etc.) passaram com sucesso.
