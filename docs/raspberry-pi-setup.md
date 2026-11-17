# Raspberry Pi — instalador/atualizador Python

Para Raspberry Pi OS/Debian com Node-RED como serviço systemd, broker MQTT e
MySQL/MariaDB já instalados. Python 3.9+; não requer pip ou ambiente virtual.
Não altera o sistema operativo, o Mosquitto, as regras de firewall ou o
firmware ESP32. Não descodifica credenciais Node-RED.

## Atualizar uma estação existente

No terminal SSH do Raspberry Pi, obter a versão do repositório e entrar na sua pasta.
Se já houver uma cópia com alterações locais, não as substituir: usar uma nova pasta.

```sh
git clone https://github.com/joaofrantunes/esp32radandsensors.git
cd esp32radandsensors
sudo python3 -B -m pi_setup plan
```

O plano identifica o utilizador do serviço `nodered.service`, procura um
ficheiro `flows*.json` único na pasta `.node-red`, verifica os IDs conhecidos,
confirma a configuração MySQL existente e lista as tabelas a criar. Não escreve
no sistema nem reinicia serviços. Se houver vários ficheiros ou uma instalação
personalizada, confirmar o caminho real no log de arranque do Node-RED:

```sh
sudo journalctl -u nodered.service -n 100 --no-pager
```

Se necessário, indicar `--flow-file /home/UTILIZADOR/.node-red/flows.json`.
Usar o caminho fornecido pelo log, não copiar o exemplo literalmente.
Se o nome da base de dados for diferente de `nodered`, indicar `--database NOME`.

Depois de rever o plano, executar com os mesmos argumentos (incluindo o caminho,
se necessário):

```sh
sudo python3 -B -m pi_setup update --yes
```

A operação deteta conflitos e aborta em vez de adivinhar. Para atualizar,
os IDs da estação precisam de corresponder aos exports deste repositório;
flows personalizados, subscrições MQTT concorrentes e IDs diferentes requerem
uma migração manual. A atualização preserva nós fora do projeto, o broker e
a configuração MySQL existente, incluindo os seus IDs e credenciais separadas.

Se faltarem bibliotecas Node-RED, instalar no gestor de paletas, ou repetir
com `--install-palettes` para instalar somente as faltantes pelo npm como o
utilizador do serviço (requer rede). As alterações npm não são desfeitas
automaticamente em caso de falha; os manifests anteriores ficam no backup.

## Autenticação MySQL

A instalação local pode permitir autenticação por socket do utilizador root,
por exemplo `sudo mariadb`. Se for necessária outra conta, criar um ficheiro
privado de opções MySQL, com as credenciais de uma conta autorizada a ler,
fazer backup e criar as tabelas. Não usar a palavra-passe na linha de comandos.

```ini
[client]
user=UTILIZADOR_ADMINISTRATIVO
password=PALAVRA_PASSE_LOCAL
```

Dar permissões `chmod 600` ao ficheiro e usar `--mysql-options CAMINHO` em ambos
os comandos. O host e a porta são obtidos da configuração MySQL existente no
Node-RED. Para uma base remota é obrigatório o ficheiro privado de opções.
O instalador não cria utilizadores nem concede privilégios automaticamente:
se necessário, o administrador autoriza INSERT nas novas tabelas ao utilizador
Node-RED. Não usar uma conta administrativa para os INSERT de produção.

## O que acontece durante update

1. Valida o plano, a conta do serviço, as dependências e a base existente.
2. Para temporariamente o Node-RED (pequena janela de indisponibilidade).
3. Faz cópia do flow, ficheiro de credenciais, configuração e backup SQL em
   `/var/backups/esp32radandsensors/<data-UTC>`; acesso restrito ao administrador.
4. Cria apenas ESP32GEIGERSTATS, ESP32DEVICE e ESP32EXTRA se ausentes.
5. Atualiza o flow por substituição atómica, mantendo os IDs e as configurações existentes.
6. Reinicia Node-RED; se falhar, tenta repor o flow original. As tabelas novas
   não são apagadas e o backup SQL não é importado automaticamente.
7. Numa segunda execução sem alterações, não reinicia desnecessariamente.

Se ocorrer uma falha, consultar o backup e o estado de `systemctl status nodered`.
O instalador confirma que o serviço ficou ativo, não que as medições reais já
chegaram ao dashboard. Confirmar MQTT, dashboard e INSERTs; só depois carregar
o firmware V1.7 se ainda não estiver instalado. Nunca publicar os backups,
que podem conter credenciais e localização GPS.

## Novo projeto num Raspberry Pi já preparado

Se Node-RED, MQTT, MySQL e uma base de dados vazia já estiverem instalados,
`plan` seguido de `install --yes --install-palettes` cria as 20 tabelas e
importa este projeto sem apagar outros flows. É necessário configurar as
credenciais locais MQTT/MySQL no editor após a primeira instalação e fazer
Deploy. Não inclui instalação de serviços de raiz nem abertura de portas.

Referências: [Node-RED no Raspberry Pi](https://nodered.org/docs/getting-started/raspberrypi),
[configuração runtime](https://nodered.org/docs/user-guide/runtime/configuration)
e [backup MariaDB](https://mariadb.com/docs/server/clients-and-utilities/backup-restore-and-import-clients/mariadb-dump).
