sudo apt update
sudo apt install python3-venv
pip install uv
python3 -m venv venv_pio
source venv_pio/bin/activate
uv pip install platformio
