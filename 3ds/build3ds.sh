#!/bin/bash
set -e

# Make sure we are in the 3ds folder
cd "$(dirname "$0")"
DIR="$(pwd)"
cd ..

if [ ! -f "3ds/zelda3.sfc" ]; then
    echo "Error: Please place your zelda3.sfc ROM inside the 3ds/ folder before building."
    exit 1
fi

echo "Select the language for the build:"
options=(
    "English"
    "Español"
    "Deutsch"
    "Français"
    "Français Canadien"
    "Português"
    "Polski"
    "Nederlands"
    "Svenska"
    "ALttP Redux"
)
select opt in "${options[@]}"; do
    case $opt in
        "English") LANG_OPT="en"; break;;
        "Español") LANG_OPT="es"; break;;
        "Deutsch") LANG_OPT="de"; break;;
        "Français") LANG_OPT="fr"; break;;
        "Français Canadien") LANG_OPT="fr-c"; break;;
        "Português") LANG_OPT="pt"; break;;
        "Polski") LANG_OPT="pl"; break;;
        "Nederlands") LANG_OPT="nl"; break;;
        "Svenska") LANG_OPT="sv"; break;;
        "ALttP Redux") LANG_OPT="redux"; break;;
        *) echo "Invalid option $REPLY";;
    esac
done
echo "Selected language: $opt ($LANG_OPT)"

RESTOOL_ARGS="--extract-from-rom"
if [ "$LANG_OPT" != "en" ] && [ "$LANG_OPT" != "redux" ]; then
    ROM_LANG="${LANG_OPT}.sfc"
    if [ ! -f "3ds/$ROM_LANG" ]; then
        echo "========================================================"
        echo "FATAL ERROR: Missing language ROM."
        echo "To build the game in '$LANG_OPT', you MUST provide"
        echo "a translated SNES ROM in that language and place it"
        echo "inside the 3ds/ folder with the exact name: $ROM_LANG"
        echo "========================================================"
        exit 1
    fi
    cp 3ds/$ROM_LANG tables/$ROM_LANG
    RESTOOL_ARGS="--extract-from-rom --languages=$LANG_OPT"
elif [ "$LANG_OPT" == "redux" ]; then
    RESTOOL_ARGS="--extract-from-rom --languages=redux"
fi


echo "Copying ROM for extraction..."
mkdir -p tables
cp 3ds/zelda3.sfc tables/zelda3.sfc

echo "Starting Docker build..."
docker run --rm -v "$(pwd):/src" devkitpro/devkitarm bash -c "
    echo 'Installing extraction dependencies (Python)...' &&
    apt-get update && apt-get install -y python3 python3-pillow python3-yaml unzip curl &&
    echo 'Extracting game resources...' &&
    cd /src &&
    if [ ! -d 'zelda3-0.3' ]; then
      echo 'Downloading extraction tools...' &&
      curl -L https://github.com/snesrev/zelda3/archive/refs/tags/v0.3.zip -o zelda3-0.3.zip &&
      unzip -q zelda3-0.3.zip
    fi &&
    cp tables/zelda3.sfc zelda3-0.3/zelda3.sfc &&
    if [ '${LANG_OPT}' != 'en' ] && [ '${LANG_OPT}' != 'redux' ]; then
        cp tables/${LANG_OPT}.sfc zelda3-0.3/${LANG_OPT}.sfc &&
        cd zelda3-0.3 &&
        echo 'Extracting dialogue for language ${LANG_OPT}...' &&
        python3 assets/restool.py --extract-dialogue -r ${LANG_OPT}.sfc &&
        cd ..
    fi &&
    cd zelda3-0.3 &&
    python3 assets/restool.py $RESTOOL_ARGS &&
    cp zelda3_assets.dat /src/3ds/ &&
    echo 'Building SDL2 for 3DS...' &&
    if [ ! -d '/src/SDL2-3DS' ]; then
      git clone --depth 1 -b SDL2 https://github.com/libsdl-org/SDL.git /src/SDL2-3DS
    fi &&
    cd /src/SDL2-3DS &&
    cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE='/opt/devkitpro/cmake/3DS.cmake' -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX='/opt/devkitpro/portlibs/3ds' &&
    cmake --build build -j4 &&
    cmake --install build &&
    echo 'Preparing Makerom...' &&
    if ! command -v makerom &> /dev/null; then
      curl -L https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.3/makerom-v0.18.3-ubuntu_x86_64.zip -o makerom.zip &&
      unzip makerom.zip &&
      chmod +x makerom &&
      mv makerom /usr/local/bin/
    fi &&
    echo 'Preparing Bannertool...' &&
    if ! command -v bannertool &> /dev/null; then
      curl -L https://github.com/diasurgical/bannertool/releases/download/1.2.0/bannertool.zip -o bannertool.zip &&
      unzip -o bannertool.zip &&
      chmod +x linux-x86_64/bannertool &&
      mv linux-x86_64/bannertool /usr/local/bin/bannertool &&
      rm -rf linux-x86_64 windows-i686 windows-x86_64 bannertool.zip
    fi &&
    echo 'Building Banner...' &&
    cd /src/3ds &&
    bannertool makebanner -i banner_image.png -a clean_audio.wav -o banner.bin &&
    echo 'Building Zelda3 (3DSX and CIA)...' &&
    cd /src/3ds &&
    make clean &&
    make -j4 cia &&
    echo 'Build process completed successfully! Your files are ready in the 3ds/ folder.'
"
rm -f zelda3.3dsx zelda3.smdh zelda3.elf

echo "[General]" > 3ds/zelda3.ini
echo "Language = $LANG_OPT" >> 3ds/zelda3.ini
echo "zelda3.ini generated with Language = $LANG_OPT"
