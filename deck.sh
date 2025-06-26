#!/usr/bin/env bash
#
# deck.sh — запускает skydeck_joystick_sender.py с автоперезапуском при падении или переподключении USB

set -euo pipefail

ENV_DIR="./skydeck_env"
SCRIPT="skydeck_joystick_sender.py"

# Функция запуска скрипта в venv
run_sender() {
  # Активируем venv, если он есть
  if [[ -f "$ENV_DIR/bin/activate" ]]; then
    # shellcheck source=/dev/null
    source "$ENV_DIR/bin/activate"
  else
    echo "Warning: виртуальное окружение '$ENV_DIR' не найдено. Запустите './install.sh'."
  fi

  # Проверяем наличие скрипта
  if [[ ! -f "$SCRIPT" ]]; then
    echo "Error: '$SCRIPT' не найден."
    exit 1
  fi

  # Запускаем sender
  exec python3 "$SCRIPT"
}

# Главный цикл: если sender вернётся ненулевым кодом, ждём и перезапускаем
while true; do
  echo "=== Запускаю skydeck_joystick_sender.py ==="
  run_sender
  EXIT_CODE=$?

  echo "!! skydeck_joystick_sender завершился с кодом $EXIT_CODE"
  echo "Буду пытаться перезапустить через 1 секунду… (Ctrl+C для выхода)"
  sleep 1
done
