# V1.7.0 — Perfil CAJOE e deteção automática de sensores

Esta revisão preserva os GPIOs, tópicos MQTT, flows Node-RED e esquema MySQL
da V1.6.0. O perfil `STATION_PROFILE=1` é o predefinido; destina-se à WEMOS
LOLIN32 Lite da estação fotografada. Alterar para 0 recupera as predefinições
genéricas V1.6. Cada flag individual continua configurável.

## Módulos e diagnóstico

| Módulo | Configuração / identificação | Resultado esperado |
|---|---|---|
| Geiger CAJOE | GPIO13, `ENABLE_GEIGER=1` | CPM, CPH estimado, CPH medido após 1 h, médias móveis e estado |
| u-blox NEO-6M | UART2, RX16/TX17, 9600 baud | Coordenadas e dados GPS após fix recente válido |
| Placa GY-BM E/P 280 | I²C 0x76 ou 0x77; registo 0xD0 = 0x58 (BMP) ou 0x60 (BME) | Temperatura, pressão, altitude estimada |
| Placa AHT, modelo exato por confirmar | I²C 0x38; comandos de inicialização AHT10/AHT20 | Humidade e temperatura; não identifica univocamente o chip |
| Placa marcada HMC5883L | I²C 0x1E, ID H43; alternativa QMC5883L 0x0D, ID FF | X/Y/Z em µT e orientação magnética aproximada |
| Hall / temperatura interna | Core ESP32 2.x para Hall | Diagnóstico interno, não temperatura ambiente |
| OLED / ADC / LoRa | Desativados por defeito | Ativar individualmente após confirmação de ligações e tensão |

I²C SDA=21 e SCL=22. Não alterar ligações apenas com base nas fotos;
confirmar pinagem, alimentação e níveis lógicos compatíveis com 3,3 V.
Nunca ligar a alta tensão do módulo Geiger nem 5 V diretamente ao ESP32.
Para o tubo J305, a estimativa de dose continua desativada: um fator não
calibrado não substitui um dosímetro certificado. Não alterar o fator
com base apenas na marca da placa. Ver [Geiger V1.6](geiger-v16.md).

## Atualizar a estação existente

1. No Raspberry Pi, fazer `sudo python3 -B -m pi_setup plan` na nova cópia
   do repositório. Se a instalação ainda for anterior à V1.6, seguir o
   [guia de atualização](raspberry-pi-setup.md) e confirmar o dashboard/SQL.
   Se já estiver na V1.6 com os flows e tabelas atuais, não reimportar flows.
2. No Arduino IDE 1.8.18, selecionar WEMOS LOLIN32 Lite / ESP32 core 2.0.16.
   Instalar **Adafruit BME280 Library** e conservar as restantes dependências
   do [guia Arduino](arduino-ide.md). Confirmar `ENABLE_BME280=1` para auto-deteção.
3. Copiar o `credentials.h` local para a nova pasta do sketch (nunca para GitHub).
   Rever flags antigas que possam substituir as predefinições do perfil.
4. Verificar, carregar e abrir Monitor Série a 115200 baud. Guardar os
   resultados de compilação, sem publicar credenciais ou coordenadas.
5. Procurar `[I2C]`, `[BARO]`, `[AHT]`, `[MAG]`, `[GPS]`; aguardar um minuto
   para os módulos ambientais e colocar a antena GPS com vista para o céu.
6. Confirmar os gráficos e novas linhas MySQL. O GPS sem fix e o sensor
   indisponível não originam leituras falsas. A humidade BME280, se disponível,
   é mostrada no monitor série; o tópico de humidade existente permanece AHT.

O diagnóstico inicial verifica ACK nos endereços esperados, mas ACK não é
identificação de chip. A identificação usa os registos descritos acima.
Se um módulo não responder, a aquisição dos restantes continua; sensores
I²C são novamente tentados nos ciclos seguintes. Não desligar fichas com
alimentação ligada, em particular a placa de alta tensão Geiger.

## Calibrar o magnetómetro (opcional)

1. Em `HardwareConfig.h`, ativar `ENABLE_MAG_CAL_LOG 1`, compilar e carregar.
2. Guardar as linhas `[MAGCAL] X,Y,Z` do monitor série (µT, valores brutos).
   Rodar lentamente a estação em várias orientações, com distância de metais,
   ímanes e fontes de interferência, recolhendo mínimos e máximos de cada eixo.
3. Para cada eixo, calcular `offset=(max+min)/2` e `r=(max-min)/2`.
   Calcular a média dos três r; usar `scale=média/r` para cada eixo, apenas
   se os três r forem positivos e houver cobertura suficiente das orientações.
4. Preencher `MAG_OFFSET_X/Y/Z` e `MAG_SCALE_X/Y/Z`; manter a declinação a zero
   para orientação magnética ou preencher `MAG_DECLINATION_DEG` com valor
   local verificado (este positivo, oeste negativo). Não inventar coeficientes.
5. Desativar o registo `ENABLE_MAG_CAL_LOG` e recompilar/carregar. Testar
   com o sensor nivelado. Não há compensação de inclinação, nem correção
   completa de interferência magnética ou garantia de precisão em graus.

## Validação

Os testes host usam interfaces simuladas Arduino/I²C/FreeRTOS, incluindo
ambas as variantes barométricas e magnéticas, endereços e IDs inválidos,
perda/recuperação, conversões e correções. Não substituem a compilação
no core ESP32 real nem a confirmação física da instalação.

Referências de fabricantes/projetos: [Bosch BMP280](https://www.bosch-sensortec.com/products/environmental-sensors/pressure-sensors/bmp280/),
[Bosch BME280](https://www.bosch-sensortec.com/products/environmental-sensors/humidity-sensors-bme280/),
[Adafruit BME280](https://github.com/adafruit/Adafruit_BME280_Library),
[TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus).
