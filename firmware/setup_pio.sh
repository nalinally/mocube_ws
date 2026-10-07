sudo apt update
sudo apt install python3-venv
python3 -m venv venv_pio
source venv_pio/bin/activate
pip install uv
uv pip install platformio
