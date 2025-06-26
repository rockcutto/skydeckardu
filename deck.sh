#!/usr/bin/env bash
#
# deck.sh — запускает skydeck_joystick_sender.py с автоподхватом venv

set -euo pipefail

ENV_DIR="./skydeck_env"
SCRIPT="skydeck_joystick_sender.py"

# Если есть виртуальное окружение — активируем
if [[ -f "$ENV_DIR/bin/activate" ]]; then
  # shellcheck source=/dev/null
  source "$ENV_DIR/bin/activate"
else
  echo "Warning: virtualenv not found in '$ENV_DIR'."
  echo "Run './install.sh' first."
fi

# Проверим, что скрипт на месте
if [[ ! -f "$SCRIPT" ]]; then
  echo "Error: $SCRIPT not found."
  exit 1
fi

# Запускаем интерпретатор python3 из PATH (будет взят из venv, если активирован)
exec python3 "$SCRIPT" "$@"
