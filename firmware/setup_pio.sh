#!/bin/bash
# PlatformIO を firmware/venv_pio にインストールする (初回のみ)
set -e
cd "$(dirname "$0")"

sudo apt update
sudo apt install -y python3-venv
python3 -m venv venv_pio
source venv_pio/bin/activate
pip install uv
uv pip install platformio
