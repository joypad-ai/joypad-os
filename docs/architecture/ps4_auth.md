# 🎮 Autenticação PS4 (DS4) no Joypad OS

Este documento descreve como o Joypad OS gerencia a autenticação necessária para que o dispositivo seja reconhecido como um controle oficial pelo console PlayStation 4. O projeto implementa dois métodos principais: **Passthrough** (através de um controle real) e **Local (RSA)** (usando chaves armazenadas na Flash).

## 1. Fluxo Geral de Autenticação
A autenticação ocorre via **USB HID Feature Reports** específicos (IDs `0xF0` a `0xF3`):
- **0xF0 (Nonce):** O console envia um desafio (nonce) de 280 bytes em 5 páginas de 56 bytes.
- **0xF1 (Signature):** O dispositivo responde com uma assinatura de 1064 bytes em 19 páginas.
- **0xF2 (Status):** O console consulta se o processo de assinatura foi concluído (0x00 para pronto, 0x10 para ocupado).
- **0xF3 (Reset):** Reinicia o estado da máquina de autenticação.

## 2. Métodos de Autenticação

### A. Autenticação Local (RSA)
Permite usar o Joypad OS sem um controle original conectado, utilizando chaves RSA-2048 extraídas de um controle real.
- **Arquivos Principais:** 
  - `src/usb/usbd/modes/ps4_local_auth.c`: Lógica de assinatura RSA.
  - `src/core/services/storage/ps4_auth_flash.c`: Gerenciamento das chaves na Flash.
- **Funções Chave:**
  - `ps4_local_auth_init()`: Carrega as chaves da Flash e inicializa o contexto `mbedTLS`.
  - `ps4_do_sign()`: Executa a assinatura **RSA-PSS** (SHA-256) do nonce. No RP2040, essa tarefa é delegada ao **Core 1** (`core1_idle_hook`) para não bloquear o processamento USB no Core 0.
  - `ps4_local_auth_get_next_page()`: Monta e retorna as páginas da assinatura final, incluindo o serial do dispositivo e o arquivo `sig.bin` (Sony Device Signature).

### B. Autenticação Passthrough
Funciona como uma ponte entre o console PS4 e um controle original conectado à porta USB Host do dispositivo.
- **Arquivos Principais:**
  - `src/usb/usbh/hid/devices/vendors/sony/sony_ds4.c`
- **Funções Chave:**
  - `ds4_auth_send_nonce()`: Recebe o nonce do console e o encaminha ao controle real via `tuh_hid_set_report`.
  - `ds4_auth_task()`: Uma máquina de estados que monitora o progresso da assinatura no controle real e busca o resultado via `tuh_hid_get_report`.
  - `ds4_auth_get_next_signature()`: Repassa as páginas da assinatura recebidas do controle real de volta para o console.

## 3. Upload e Gerenciamento de Chaves
As chaves para a autenticação local são enviadas ao dispositivo via comandos JSON por uma conexão serial (USB CDC).
- **Arquivos Principais:**
  - `src/usb/usbd/cdc/cdc_commands.c`: Tratamento dos comandos de configuração.
- **Comandos Utilizados:**
  - `PS4AUTH.SET`: Recebe as chaves (N, E, P, Q, serial, signature) em Base64, decodifica e as salva na Flash usando `ps4_auth_flash_save()`.
  - `PS4AUTH.STATUS`: Verifica se as chaves estão instaladas e se a autenticação local está ativa.
  - `PS4AUTH.CLEAR`: Apaga os dados de autenticação da memória Flash.

## 4. Orquestração de Modo
O arquivo `src/usb/usbd/modes/ps4_mode.c` atua como o driver do dispositivo USB e decide qual método usar:
- Em `ps4_mode_get_report()`, ele tenta primeiro a **autenticação local** (`ps4_local_auth_is_available()`). 
- Se não houver chaves instaladas, ele recorre ao **passthrough** (`ds4_auth_is_available()`) se um controle DS4 estiver conectado.

## 5. Log de Eventos de Autenticação
Para fins de depuração, o sistema possui um log de eventos persistente que registra o progresso e possíveis falhas do processo de autenticação.

### A. Armazenamento e Capacidade
O log compartilha o setor de 4 KB da Flash com os dados de autenticação:
- **Espaço do Log:** Inicia no deslocamento de 1024 bytes do setor de autenticação.
- **Capacidade:** Suporta até **48 registros** de 64 bytes cada.
- **Persistência:** Por ser armazenado na Flash, o log sobrevive a reinicializações. Quando o limite de 48 registros é atingido, novas entradas são descartadas até que o log seja limpo.

### B. Condições de Uso
- **Habilitação:** O log é opcional e pode ser ativado/desativado via comando `PS4LOG.ENABLE.SET` (configuração `ps4_auth_log` na Flash).
- **Dependência de Chaves:** O funcionamento do log independe da presença de chaves RSA válidas. No entanto, o comando para apagar as chaves (`PS4AUTH.CLEAR`) também limpa o log, pois ambos residem no mesmo setor físico da Flash.
- **Segurança:** O comando `PS4LOG.CLEAR` é projetado para preservar as chaves de autenticação (fazendo backup e restauração) enquanto limpa apenas a área de registros.

### C. Comandos de Gerenciamento (via CDC)
- `PS4LOG.DUMP`: Retorna o conteúdo atual do log em formato legível.
- `PS4LOG.CLEAR`: Apaga todos os registros do log na Flash.
- `PS4LOG.ENABLE.GET/SET`: Consulta ou altera o estado de habilitação do log.

## 6. Observações Técnicas
No hardware RP2040, o sistema utiliza overclock para **250MHz** durante o processo de autenticação local. Isso reduz o tempo de assinatura RSA pela metade (de ~3.4s para ~1.7s), garantindo que a resposta seja entregue dentro da janela de tempo exigida pelo PlayStation 4.
