# Notas do port Nintendo 3DS

## Como compilar (Docker)

A compilação usa a imagem oficial `devkitpro/devkitarm` (devkitARM + libctru já
instalados). Na raiz do repositório:

```bash
docker run --rm --user $(id -u):$(id -g) \
  -v $(pwd)/app/jni/src:/source \
  devkitpro/devkitarm \
  bash -c 'export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM HOME=/tmp; \
           cd /source/src/platform/3ds && make'
```

O resultado fica em `app/jni/src/src/platform/3ds/zelda3.3dsx` (~1 MB — os
assets do jogo **não** são embutidos no executável).

Para recompilar do zero, acrescente `make clean &&` antes do `make`.
O `--user $(id -u):$(id -g)` evita que o diretório `build/` fique com dono root.

## Onde colocar os arquivos no 3DS (cartão SD)

| Arquivo | Destino no SD | Obrigatório |
|---|---|---|
| `zelda3.3dsx` | `sd:/3ds/zelda3.3dsx` | Sim |
| `zelda3_assets.dat` | `sd:/3ds/zelda3/zelda3_assets.dat` | Sim |
| `zelda3.ini` (exemplo em `app/jni/src/src/platform/3ds/zelda3.ini`) | `sd:/3ds/zelda3/zelda3.ini` | Não (sem ele valem os padrões) |
| `dspfirm.cdc` (dump do firmware DSP) | `sd:/3ds/dspfirm.cdc` | Não (sem ele o jogo roda **sem áudio**) |

- Os saves são criados automaticamente em `sd:/3ds/zelda3/saves/`.
- O jogo é iniciado pelo **Homebrew Launcher**.
- O `zelda3_assets.dat` é gerado a partir da sua ROM com o
  `assets/restool.py` do repositório original (`python3 assets/restool.py
  --extract-from-rom`) — veja o README para o passo a passo.
- O `dspfirm.cdc` é o firmware do chip DSP de áudio (código da Nintendo,
  não pode ser distribuído junto). Extraia do próprio console, uma única
  vez, pelo menu do Luma3DS: `L+Baixo+Select → Miscellaneous options →
  Dump DSP firmware`. Sem ele o jogo funciona normalmente, porém sem som.

## Gerar o .cia (instalável no menu HOME, requer CFW)

O `.cia` usa o mesmo binário do `.3dsx`; a spec fica em
`app/jni/src/src/platform/3ds/cia/` (zelda3.rsf + banner). São necessárias
duas ferramentas extras que não vêm na imagem Docker (baixar uma vez):

```bash
cd app/jni/src/src/platform/3ds
mkdir -p tools && cd tools
curl -sLO https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.19.0/makerom-v0.19.0-ubuntu_x86_64.zip
unzip -o makerom-v0.19.0-ubuntu_x86_64.zip && rm makerom-v0.19.0-ubuntu_x86_64.zip
curl -sL https://github.com/carstene1ns/3ds-bannertool/releases/download/1.2.3/bannertool-1.2.3-linux.tar.gz | tar xz
chmod +x makerom bannertool
```

Depois, o mesmo comando Docker do build normal, trocando `make` por
`make cia` — o resultado é `zelda3.cia`. Instale com o FBI (ou similar) e
coloque os arquivos no SD nos mesmos lugares da tabela acima (o .cia
também lê tudo de `sd:/3ds/zelda3/`, nada é embutido).

## Músicas MSU-1 (trilha sonora em CD-quality)

O port toca packs MSU-1 do A Link to the Past. Onde colocar as músicas:

```
sd:/3ds/zelda3/msu/1.pcm
sd:/3ds/zelda3/msu/2.pcm
...
```

E no `sd:/3ds/zelda3/zelda3.ini`:

```ini
[Sound]
EnableMSU = 1        # 1 = pack .pcm | opuz = pack .opuz | deluxe / deluxe-opuz
MSUPath = msu/       # subpasta dentro de sd:/3ds/zelda3/
MSUVolume = 100
AudioFreq = 44100    # OBRIGATÓRIO: 44100 para .pcm, 48000 para .opuz
```

- Os arquivos seguem a numeração de faixas MSU-1 padrão do ALttP
  (`<número>.pcm`). Packs "MSU Deluxe" (com faixas extras por área) usam
  `EnableMSU = deluxe`.
- **Recomendado no 3DS: packs `.pcm`** — custo de CPU quase zero (leitura
  direta do SD). Packs `.opuz` exigem decodificação Opus na CPU, que é
  pesada no 3DS (especialmente no Old 3DS).
- Se um arquivo de faixa não existir, o jogo cai na música original do
  SNES para aquela faixa.

## Desempenho

- **New 3DS**: widescreen 400×240 + clock de 804 MHz, automático.
- **Old 3DS**: 256×240 (mais leve), automático.
- Para forçar um modo: `Widescreen3DS = on/off/auto` na seção `[Graphics]`
  do `zelda3.ini` no SD.
- A lógica do jogo roda sempre a 60 Hz; se a renderização não acompanhar,
  frames de desenho são pulados (o jogo não fica em câmera lenta).
- O overlay de números coloridos na tela de baixo mostra o custo de cada
  etapa (🔴 lógica, 🟢 render PPU, 🔵 blit, 🟡 segunda tela, 🟣 áudio, em
  décimos de ms) e o fps (⚪). Ligado por padrão durante a fase de ajuste
  do port.

## Testar no emulador (Azahar)

```bash
# assets no SD virtual do emulador (uma vez):
mkdir -p ~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/3ds/zelda3
cp zelda3_assets.dat ~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/3ds/zelda3/

flatpak run org.azahar_emu.Azahar app/jni/src/src/platform/3ds/zelda3.3dsx
```

Obs.: no Azahar o `APT_CheckNew3DS` retorna falso mesmo com o perfil New 3DS
ativo, então o emulador sempre exercita o caminho 256×240. Os tempos do
overlay no emulador não representam o hardware real.
