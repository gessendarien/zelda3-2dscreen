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
    cd zelda3-0.3 &&
    python3 assets/restool.py --extract-from-rom &&
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
    echo 'Building Zelda3 (3DSX and CIA)...' &&
    cd /src/3ds &&
    make clean &&
    make -j4 cia &&
    echo 'Build process completed successfully! Your files are ready in the 3ds/ folder.'
"
rm -f zelda3.3dsx zelda3.smdh zelda3.elf
