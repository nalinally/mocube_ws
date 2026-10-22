#!/usr/bin/env bash
# mocube_ws 用の tmux セッションを開く。既にあればそれに attach する。
#
#   ./tmux.sh                 # ポートは /dev/ttyACM0
#   ./tmux.sh /dev/ttyACM1    # 書き込み・シリアルモニタのポートを指定
#
# window:
#   claude : Claude Code
#   ros    : 左上 = device_manager / 左下 = servo_teleop / 右上 = /modules の echo / 右下 = colcon build
#   pio    : 左上 = 書き込み / 左下 = シリアルモニタ / 右 = ポート確認 (WSL なら usbipd のメモ)
#            書き込み中はポートを掴めないので、モニタは Ctrl+C で止めてから書き込む
#   memo   : 落書き用
#
# 実行してほしいコマンドは「入力済み・未実行」にしてあるので、Enter で実行する。

set -eu

SESSION=mocube
PORT=${1:-/dev/ttyACM0}
WS=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FW=$WS/firmware/mocube_main

if tmux has-session -t "$SESSION" 2>/dev/null; then
    if [ -n "${TMUX:-}" ]; then
        exec tmux switch-client -t "$SESSION"
    fi
    exec tmux attach -t "$SESSION"
fi

# ---- claude ----
tmux new-session -d -s "$SESSION" -n claude -c "$WS"
tmux send-keys -t "$SESSION:claude" "claude" Enter

# ---- ros ----
# ペインはユーザーの pane-base-index に依存しないよう ID で扱う
ROS_MAIN=$(tmux new-window -t "$SESSION" -n ros -c "$WS" -P -F '#{pane_id}')
ROS_ECHO=$(tmux split-window -t "$ROS_MAIN" -h -c "$WS" -P -F '#{pane_id}')
ROS_BUILD=$(tmux split-window -t "$ROS_ECHO" -v -c "$WS" -P -F '#{pane_id}')
ROS_TELEOP=$(tmux split-window -t "$ROS_MAIN" -v -c "$WS" -P -F '#{pane_id}')
for pane in "$ROS_MAIN" "$ROS_TELEOP" "$ROS_ECHO" "$ROS_BUILD"; do
    tmux send-keys -t "$pane" "source install/setup.bash && clear" Enter
done
tmux send-keys -t "$ROS_MAIN" "ros2 run mocube_bridge device_manager"
tmux send-keys -t "$ROS_TELEOP" "ros2 run mocube_bridge servo_teleop"
tmux send-keys -t "$ROS_ECHO" "ros2 topic echo /modules"
tmux send-keys -t "$ROS_BUILD" "colcon build && source install/setup.bash"

# ---- pio ----
PIO_MAIN=$(tmux new-window -t "$SESSION" -n pio -c "$FW" -P -F '#{pane_id}')
PIO_PORT=$(tmux split-window -t "$PIO_MAIN" -h -l 40% -c "$FW" -P -F '#{pane_id}')
PIO_MON=$(tmux split-window -t "$PIO_MAIN" -v -l 70% -c "$FW" -P -F '#{pane_id}')
for pane in "$PIO_MAIN" "$PIO_MON" "$PIO_PORT"; do
    tmux send-keys -t "$pane" "source $WS/firmware/activate.sh && clear" Enter
done
tmux send-keys -t "$PIO_MAIN" "pio run -t upload --upload-port $PORT"
tmux send-keys -t "$PIO_MON" "pio device monitor -p $PORT -b 115200"
tmux send-keys -t "$PIO_PORT" "pio device list" Enter
if grep -qi microsoft /proc/version; then
    tmux send-keys -t "$PIO_PORT" "cat <<'EOF'
# WSL: AtomS3 の USB を WSL に渡す (Windows の PowerShell で。attach は管理者権限)
#   usbipd list                              # BUSID を確認
#   usbipd bind --busid <busid>              # 初回だけ (管理者)
#   usbipd attach --wsl --busid <busid>
EOF" Enter
fi

# ---- memo ----
tmux new-window -t "$SESSION" -n memo -c "$WS"

tmux select-window -t "$SESSION:claude"
if [ -n "${TMUX:-}" ]; then
    exec tmux switch-client -t "$SESSION"
fi
exec tmux attach -t "$SESSION"
