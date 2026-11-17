# V1.6.0 — CAJOE / J305 e monitorização avançada

Esta versão mantém o contador CAJOE RadiationD-v1.1, GPIO13, interrupção em
flanco descendente, sensores opcionais, alarmes originais, tópicos MQTT e
tabelas históricas. O utilizador confirmou que o módulo corresponde ao da
outra estação, onde o tubo estava identificado como **J305 βγ**.

## Radiação: grandezas distintas

Não altera a contagem física do tubo: melhora a integração temporal e a
utilidade estatística da leitura. O ISR continua a contar sem reset de
janela; a diferença modular é amostrada a cada 15 s (tempo real medido por
`millis()`). A janela circular guarda 240 pares de contagem/duração. A
medição da taxa usa a soma de contagens dividida pela duração real, não
uma média não ponderada de valores CPM. A RAM do anel é aproximadamente
1920 bytes, além das estruturas de amostras e do buffer MQTT.

| Campo MQTT `esp32/geiger/stats` | Significado | Primeira disponibilidade |
|---|---|---|
| `count`, `elapsed_ms`, `total` | Pulsos da última janela, duração e total desde boot | 15 s |
| `cpm` | CPM normalizado da última janela | 15 s |
| `cpm60` | CPM móvel (aprox. 60 s de cobertura) | 60 s |
| `cpm300` | CPM móvel (aprox. 300 s de cobertura) | 5 min |
| `cph_est` | Projeção para uma hora, baseada em CPM60 ou na janela inicial | 15 s |
| `estimate_ms` | Duração efetiva usada na projeção | 15 s |
| `cph_rolling` | CPH normalizado pela janela móvel de aproximadamente 60 min | 60 min |
| `window60_ms`, `window300_ms`, `window3600_ms` | Cobertura efetiva das janelas prontas, 0 enquanto indisponíveis | 15 s |
| `sigma_cpm` | Incerteza estatística aproximada de 1 sigma do CPM60 (N >= 20) | 60 s, conforme contagem |
| `dose_rate_est`, `dose_total_est`, `dose_ratio` | Estimativas não calibradas, opt-in | Desativadas por defeito |

Os campos não disponíveis são JSON `null` e não alimentam os gráficos com
zeros. Quando a contagem medida é efetivamente zero, o CPM é zero. A janela
móvel inclui buckets completos, sem inventar distribuição dos impulsos no
interior: a duração está entre o período pretendido e esse período +15 s;
se exceder, não é apresentada como janela válida. A contagem continua se
o Wi-Fi falhar. MQTT envia a amostra mais recente (não um histórico completo
de todas as janelas); a nova amostra não é reenviada se tiver mais de 30 s.

O tópico legado `esp32/cpm` continua a cada 15 s; `esp32/radiation` continua
após cada hora completa, normalizado pelo tempo real. O CPH estimado não
é o CPH efetivamente observado. O alarme existente de 200/100 CPM permanece
baseado no CPM rápido. Não constitui um alarme de segurança radiológica.

## Estimativa indicativa de dose (J305)

No projeto anterior, o J305 utilizava o fator experimentalmente **não
calibrado** de 0,00332 µSv/h por CPM. Não há confirmação metrológica do fator
nem de tempo morto. Portanto, não aplicar correção de tempo morto por defeito,
nem apresentar a estimativa como dose física validada.

Se o utilizador quiser ver apenas a estimativa, editar os valores existentes
em `HardwareConfig.h`, recompilar e carregar:

```cpp
#define ENABLE_DOSE_ESTIMATE 1
#define GEIGER_USVH_PER_CPM 0.00332
```

Estimativa da taxa = CPM60 × fator; estimativa acumulada desde o arranque =
total de impulsos × fator / 60. Reinicia com o ESP32. A tabela guarda o fator
para identificar o cálculo usado. Não usar estes valores para decisões de
proteção radiológica, nem testar com fontes radioativas improvisadas.
Para uma medição de dose, é necessário calibrar o instrumento e verificar
resposta energética, geometria e tempo morto do conjunto tubo+eletrónica.

Referências: [NIST — Uncertainty of nuclear counting](https://www.nist.gov/publications/uncertainty-nuclear-counting)
e [IAEA — Radiation detectors](https://nucleus-qa.iaea.org/sites/nuclear-instrumentation/Pages/Radiation-detectors.aspx).

## Flows e novos dados

O ficheiro completo `node-red/flows.json` mantém o dashboard original e
acrescenta três separadores do editor e do dashboard: Radiation — extended,
ESP32 diagnostics e Environmental and GPS extended. Não requer novas
bibliotecas Node-RED (continua a usar node-red-dashboard e Stackhero MySQL).

| Tópico novo | Medidas | Tabela |
|---|---|---|
| `esp32/geiger/stats` | Snapshot acima; JSON | ESP32GEIGERSTATS |
| `esp32/device/stats` | RSSI, heap livre, uptime em segundos; JSON, 60 s | ESP32DEVICE |
| `esp32/dewpoint` | Ponto de orvalho estimado a partir da mesma amostra AHT | ESP32EXTRA |
| `esp32/gps/altitude` | Altitude GPS, m (não altitude barométrica) | ESP32EXTRA |
| `esp32/gps/hdop` | HDOP adimensional; não é erro em metros | ESP32EXTRA |
| `esp32/gps/satellites` | Satélites do fix válido | ESP32EXTRA |
| `esp32/mag/magnitude` | Norma de X/Y/Z em µT | ESP32EXTRA |

Os dados ambientais/GPS/magnéticos são atualizados a cada 60 s, só quando
válidos. GPS requer fix e campos recentes; magnetómetro requer HMC5883L em
I²C 0x1E; ponto de orvalho requer AHT, humidade superior a zero. Para GPS,
confirmar TX do módulo a RX16 do ESP32, RX a TX17, GND comum, alimentação
compatível e baud 9600. Experimentar ao ar livre com visão do céu. Ver
`esp32/gps/status` (unavailable = sem bytes recentes; no_fix = sem posição
válida). Para magnetómetro, usar `ENABLE_MAG 1` somente se instalado; o driver
não suporta QMC5883L. Ver status MAG; calibração e nivelamento continuam
necessários para orientação. Nunca simular coordenadas ou campos reais.

### GPS confirmado: u-blox NEO-6M-0-001

Identificação confirmada pela marcação da fotografia do módulo, 17/11/2026.
O [datasheet NEO-6 da u-blox](https://content.u-blox.com/sites/default/files/products/documents/NEO-6_DataSheet_%28GPS.G6-HW-09005%29.pdf)
indica UART predefinida 9600 baud, 8N1, com mensagens NMEA GGA e RMC, entre
outras. Esta configuração já corresponde ao firmware V1.6 e TinyGPSPlus.
O módulo NEO-6M em si usa alimentação 2,7–3,6 V; não inferir a tensão de entrada
da placa adaptadora só pela marcação do chip. Confirmar o regulador e os pinos
da placa antes de ligar alimentação. Os sinais UART devem ser compatíveis
com a lógica de 3,3 V do ESP32.

Se o GPS estiver sem leitura, confirmar `ENABLE_GPS 1`, `GPS_BAUD 9600`,
TX do GPS para GPIO16, GND comum e antena ligada; levar para o exterior com
visão do céu e aguardar fix. Se não houver bytes UART recentes, investigar
alimentação/ligação/baud; com `no_fix`, investigar antena e visibilidade.

## Atualizar uma instalação existente

1. Guardar a cópia atual do sketch e o `credentials.h` local. Exportar o fluxo
   Node-RED atual e fazer backup da base de dados.
2. Copiar a nova pasta do sketch do GitHub (inclui `GeigerStats.h`), preservar
   credenciais locais e escolher flags de sensores em `HardwareConfig.h`.
3. Compilar na WEMOS LOLIN32 Lite, Arduino ESP32 2.0.16 conforme
   [guia Arduino IDE](arduino-ide.md). Ainda não foi validado no hardware.
4. Aplicar apenas as três novas instruções `CREATE TABLE IF NOT EXISTS` do
   `database/schema.sql` numa base existente, após confirmar nomes/estrutura.
   Dar ao utilizador Node-RED INSERT nas novas tabelas; SELECT apenas se
   usar consultas históricas. Não existe DROP, DELETE ou ALTER neste upgrade.
5. Importar `node-red/flows.json` como atualização do projeto: não manter
   duas cópias ativas da subscrição e do INSERT dos tópicos antigos. Preservar
   outros projetos; remover/substituir apenas o separador deste projeto e
   os seus grupos, recorrendo ao backup se necessário. Configurar novamente
   os nós MQTT e MySQL com as credenciais locais. Fazer Deploy.
6. Carregar o firmware e observar o monitor série a 115200 baud e a página
   `/ui`. Após 15 s, verificar CPM, CPH estimado e impulsos. Após 60 s e
   5 min verificar médias, após uma hora verificar CPH real/móvel.
7. Verificar INSERT em `ESP32GEIGERSTATS`, `ESP32DEVICE`, `ESP32EXTRA`.
   `database/queries.sql` inclui média ponderada diária, cobertura, extremos,
   histórico de diagnósticos e dados para exportação CSV via cliente SQL.

Os timestamps são de receção UTC. Sem identificador persistente e entrega
garantida, o histórico pode conter lacunas ou duplicados. O resumo diário
mede exposição temporal das amostras recebidas, não cobertura integral.
O histórico GPS é confidencial; não publicar coordenadas ou enviar para
mapas/serviços externos sem autorização. O dashboard de mapas e exportação
CSV automática não foi adicionado; o histórico é consultável na base de dados.
