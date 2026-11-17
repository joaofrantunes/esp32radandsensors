# Arduino IDE — exemplo de configuração

Referência de configuração da V1.5.0, compilada pelo utilizador com Arduino IDE
1.8.18 e o pacote de placas Arduino-ESP32 2.0.16, em novembro de 2026.
O exemplo reproduz as opções observadas; a porta série e os sensores devem
ser adaptados à estação real. A compilação não comprova a ligação física
nem o funcionamento de todos os módulos opcionais.

A V1.7.0 mantém a mesma placa e core e acrescenta a biblioteca Adafruit BME280
ao perfil predefinido. A compilação real deve ser confirmada no Arduino IDE.
Ver [guia V1.7](hardware-v17.md) e [guia V1.6](geiger-v16.md).

## 1. Instalar o suporte ESP32

1. No Arduino IDE, abrir **Ficheiro > Preferências**.
2. Em URLs adicionais para o gestor de placas, acrescentar o índice oficial da Espressif:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`.
   Ver [instruções oficiais da Espressif](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html).
3. Em **Ferramentas > Placa > Gestor de Placas**, pesquisar `esp32` e
   instalar **esp32 by Espressif Systems**, versão **2.0.16**.
4. Selecionar a placa **WEMOS LOLIN32 Lite**.

Esta versão do core é a referência de compilação desta instalação, não uma
recomendação de atualização automática para qualquer outra placa. Em particular,
o Hall interno do ESP32 clássico depende do core 2.x (`hallRead`).

## 2. Opções do menu Ferramentas

| Opção | Valor de exemplo |
|---|---|
| Placa (Board) | WEMOS LOLIN32 Lite |
| Upload Speed | 921600 |
| CPU Frequency | 240MHz (WiFi/BT) |
| Flash Frequency | 80MHz |
| Partition Scheme | Por omissão (Default) |
| Core Debug Level | Nenhum (None) |
| Erase All Flash Before Sketch Upload | Disabled |
| Porta (Port) | COM5 (exemplo Windows; escolher a porta real) |
| Monitor Série | 115200 baud |

Se o carregamento falhar a 921600, experimentar 115200. A velocidade de
upload é independente da velocidade do monitor série.

## 3. Bibliotecas Arduino necessárias

Abrir **Ferramentas > Gerir Bibliotecas** (Ctrl+Shift+I no IDE 1.8.18).
Pesquisar os nomes abaixo e instalar as dependências propostas pelo gestor.

### Instalação para o perfil predefinido

| Nome no gestor de bibliotecas | Autor | Header / função | Quando instalar |
|---|---|---|---|
| [PubSubClient](https://github.com/knolleary/pubsubclient) | Nick O'Leary | `PubSubClient.h` — MQTT (usar 2.8 com V1.6, que utiliza `setBufferSize`) | Sempre |
| [Adafruit BMP280 Library](https://github.com/adafruit/Adafruit_BMP280_Library) | Adafruit | `Adafruit_BMP280.h` — temperatura, pressão e altitude | `ENABLE_BMP280=1` |
| [Adafruit BME280 Library](https://github.com/adafruit/Adafruit_BME280_Library) | Adafruit | `Adafruit_BME280.h` — variante BME da placa 280 | `ENABLE_BMP280=1` e `ENABLE_BME280=1` |
| [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor) | Adafruit | `Adafruit_Sensor.h` — dependência BMP280 | `ENABLE_BMP280=1` |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) | Adafruit | Adaptadores I²C/SPI — dependência BMP280 | `ENABLE_BMP280=1` |
| [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) | Mikal Hart | `TinyGPSPlus.h` — interpretação NMEA | `ENABLE_GPS=1` |

As versões exatas das bibliotecas externas da compilação de referência não
foram registadas. Anotar as versões efetivamente instaladas quando se reproduzir
o ensaio. Não confundir TinyGPSPlus com variantes de outros autores.

### Bibliotecas adicionais (apenas para módulos opcionais)

| Nome a procurar | Autor/projeto | Header | Condição |
|---|---|---|---|
| [ESP8266 and ESP32 Oled Driver for SSD1306 display](https://github.com/ThingPulse/esp8266-oled-ssd1306) | ThingPulse | `SSD1306.h` | `ENABLE_LCD=1` |
| [LoRa](https://github.com/sandeepmistry/arduino-LoRa) | Sandeep Mistry | `LoRa.h` | `ENABLE_LORA=1` |
| [ThingSpeak](https://github.com/mathworks/thingspeak-arduino) | MathWorks | `ThingSpeak.h` | `ENABLE_GEIGER=1` e `ENABLE_THINGSPEAK=1` |
| [IFTTTWebhook](https://github.com/romkey/IFTTTWebhook) (legado, arquivado) | John Romkey | `IFTTTWebhook.h` | `ENABLE_GEIGER=1` e `ENABLE_IFTTT=1` |

Verificar que a versão OLED disponibiliza o header legado `SSD1306.h`.
IFTTTWebhook está descontinuada pelo autor; é uma referência histórica de
compatibilidade, não uma recomendação para novas instalações. Manter IFTTT
desativado até migrar/validar a integração, incluindo o construtor
`IFTTTWebhook(key, event)` e a convenção de retorno do método `trigger`.

### Incluídas no core ESP32 / no próprio firmware

- `Arduino.h`, `WiFi.h`, `WiFiClientSecure.h`, `Wire.h`, `SPI.h`, FreeRTOS,
  watchdog, NTP/tempo e APIs ESP32: não instalar uma segunda biblioteca WiFi.
- AHT10/AHT20 (humidade), HMC5883L/QMC5883L (magnetómetro): drivers I²C integrados
  no firmware; não exigem bibliotecas Arduino adicionais.
- Geiger, ADC, Hall e temperatura interna: utilizam APIs do core ESP32.

## 4. Preparar o sketch e as credenciais

Abrir a pasta [ArduinoGeigerCounterMix](../firmware/ArduinoGeigerCounterMix/)
e manter todos os seus ficheiros juntos. Copiar localmente
`credentials.example.h` para `credentials.h` e configurar SSID, palavra-passe,
host, porta MQTT (1883 no exemplo), utilizador e palavra-passe MQTT.
Não publicar este ficheiro no GitHub. Manter serviços externos desativados
quando não forem configurados.

## 5. Exemplos de seleção de sensores

Editar [HardwareConfig.h](../firmware/ArduinoGeigerCounterMix/HardwareConfig.h)
e alterar os valores dos `#define` existentes (1 = ativado, 0 = desativado).
Não duplicar as definições. Se existirem flags antigas em `credentials.h`,
retirá-las para que não substituam estas opções.

| Flag | Perfil mínimo¹ | Perfil predefinido | Perfil diagnóstico² | Para ativar |
|---|---:|---:|---:|---|
| `ENABLE_GEIGER` | 1 | 1 | 1 | Geiger ligado; primeira leitura CPH após uma hora |
| `ENABLE_BMP280` | 1 | 1 | 1 | BMP280 ligado ao I²C |
| `ENABLE_BME280` | 0 | 1 | 1 | Deteção adicional de BME280 e biblioteca Adafruit BME280 |
| `ENABLE_AHT` | 0 | 1 | 1 | AHT10/AHT20 ligado ao I²C |
| `ENABLE_GPS` | 0 | 1 | 1 | GPS UART ligado, com fix válido |
| `ENABLE_NTP` | 1 | 1 | 1 | Rede e servidor NTP acessíveis |
| `ENABLE_HALL` | 0 | 1 | 1 | ESP32 clássico e core 2.x |
| `ENABLE_INTTEMP` | 0 | 1 | 1 | Temperatura interna de diagnóstico, não ambiental |
| `ENABLE_VOLTAGE` | 0 | 0 | 0 | Divisor resistivo e calibração confirmados; entrada ADC protegida |
| `ENABLE_MAG` | 0 | 1 | 1 | HMC5883L ou QMC5883L; calibração para orientação |
| `ENABLE_MAG_CAL_LOG` | 0 | 0 | 1 | Registo de amostras brutas para calibração |
| `ENABLE_LCD` | 0 | 0 | 0 | OLED SSD1306 e biblioteca compatível |
| `ENABLE_LED` | 0 | 0 | 0 | GPIO de saída livre configurado |
| `ENABLE_LORA` | 0 | 0 | 0 | Rádio, antena, frequência e gateway/recetor compatíveis |
| `ENABLE_IFTTT` | 0 | 0 | 0 | Bibliotecas e credenciais válidas |
| `ENABLE_THINGSPEAK` | 0 | 0 | 0 | Bibliotecas e credenciais válidas |
| `ENABLE_DOSE_ESTIMATE` | 0 | 0 | 0 | Estimativa não calibrada; consultar guia V1.6 |

¹ Exemplo de estação com apenas Geiger e BMP280 instalados.  
² Exemplo do perfil predefinido com registo de calibração ativado;
requer validação física e compilação com os módulos selecionados. Ajustar AHT/GPS
para 0 se não estiverem instalados.

O perfil predefinido corresponde a `STATION_PROFILE=1` na V1.7.0.
Usar `STATION_PROFILE=0` para as predefinições genéricas V1.6 e ajustar flags.
Ativar módulos opcionais individualmente e
recompilar antes de carregar. Não ligar 5 V diretamente ao GPIO34. Não reutilizar
GPIO22 como LED, pois é SCL do I²C. Consultar o [guia de arquitetura](architecture.md)
para ligações, tópicos MQTT e dados encaminhados para o Node-RED.

## 6. Compilação e resolução de erros

1. Confirmar a placa e a porta série.
2. Clicar em **Verificar** (Ctrl+R) e confirmar que não existem erros.
3. Clicar em **Carregar** (Ctrl+U); abrir o monitor série a 115200 baud.
4. Confirmar o estado dos módulos, a conexão Wi-Fi/MQTT e a chegada das
   medições ao Node-RED e ao MySQL.

| Mensagem | Solução |
|---|---|
| `credentials.h: No such file or directory` | Copiar `credentials.example.h` para `credentials.h` na pasta do sketch. |
| `PubSubClient.h: No such file or directory` | Instalar PubSubClient, de Nick O'Leary. |
| `Adafruit_BMP280.h: No such file or directory` | Instalar Adafruit BMP280 Library e dependências ou desativar BMP280 se ausente. |
| `Adafruit_BME280.h: No such file or directory` | Instalar Adafruit BME280 Library ou usar `ENABLE_BME280 0` se BMP280 estiver confirmado. |
| `TinyGPSPlus.h: No such file or directory` | Instalar TinyGPSPlus ou desativar GPS se ausente. |
| Múltiplas bibliotecas para `WiFi.h` | Confirmar que o IDE seleciona a biblioteca do pacote `esp32` 2.0.16. O aviso por si só não é erro. |
| Gráfico vazio no Node-RED | Confirmar flag, sensor físico, ligação MQTT e existência de uma leitura válida. Não fabricar zeros. |

### Resultado de compilação de referência

Resultado comunicado pelo utilizador para a V1.5.0:

| Recurso | Utilizado | Máximo | Ocupação |
|---|---:|---:|---:|
| Flash (programa) | 789 221 bytes | 1 310 720 bytes | 60% |
| RAM (variáveis globais) | 47 276 bytes | 327 680 bytes | 14% |

RAM restante indicada pelo compilador: 280 404 bytes. Este valor não mede a
RAM livre durante a execução (Wi-Fi, FreeRTOS, MQTT e outros módulos também
alocam memória dinamicamente). A utilização pode variar com flags e versões.
