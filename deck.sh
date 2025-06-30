#!/usr/bin/env bash
#
# deck.sh — универсальный лаунчер skydeck_joystick_sender.py для Steam Deck.
#  - Автоматический контроль версий скрипта
#  - Помощь и self-doc
#  - Автоперезапуск и временный перевод hid_steam→hid_generic (с восстановлением)
#  - Поддержка venv
#  - Лог в файл + опционально на экран

set -euo pipefail
IFS=$'\n\t'

VERSION="1.3"
LOGFILE="./deck.log"
ENV_DIR="./skydeck_env"
SCRIPT="skydeck_joystick_sender.py"
PORT=""
SHOW_LOG=0   # 1 = лог еще и на экран

show_version() {
  echo "deck.sh v$VERSION — Steam Deck Skydeck Sender Wrapper"
  exit 0
}

show_help() {
  cat <<EOF
Использование: $0 [/dev/ttyACM0] [--help|--version] [--log]
  Автоматически переключает Steam Deck gamepad в hid_generic,
  запускает skydeck_joystick_sender.py с автоперезапуском.

  Аргументы:
    /dev/ttyACM*   — явное указание порта для передачи
    --log          — вывод лога еще и на экран
    --help         — показать справку
    --version      — показать версию

  Запускайте из домашней папки проекта. Лог — deck.log.
EOF
  exit 0
}

# --- Опции ---
for arg in "$@"; do
  case $arg in
    --help|-h) show_help ;;
    --version|-v) show_version ;;
    --log) SHOW_LOG=1 ;;
    /dev/ttyACM*) PORT="$arg" ;;
  esac
done

log() {
  local ts msg
  ts="$(date +'%Y-%m-%d %H:%M:%S')"
  msg="[$ts] $*"
  (( SHOW_LOG )) && echo "$msg" | tee -a "$LOGFILE" || echo "$msg" >> "$LOGFILE"
}

check_script_version() {
  if [[ ! -f "$SCRIPT" ]]; then
    log "Ошибка: $SCRIPT не найден!"
    exit 2
  fi
  local sha dt
  sha=$(sha1sum "$SCRIPT" | cut -d' ' -f1)
  dt=$(stat -c '%y' "$SCRIPT" | cut -d'.' -f1)
  log "Версия $SCRIPT: $sha ($dt)"
}

# --- Сбор HID-устройств Steam Deck ---
mapfile -t steam_ids < <(ls /sys/bus/hid/drivers/hid_steam/ 2>/dev/null | grep -E '\.')
if (( ${#steam_ids[@]} )); then
  log "Найдены устройства hid_steam: ${steam_ids[*]}"
else
  log "Нет устройств hid_steam — переключение пропущено."
fi

cleanup() {
  log "Восстановление hid_steam привязок..."
  for id in "${steam_ids[@]:-}"; do
    if [[ -e "/sys/bus/hid/drivers/hid_generic/$id" ]]; then
      sudo tee "/sys/bus/hid/drivers/hid_generic/unbind" <<< "$id" >/dev/null || true
    fi
    sudo tee "/sys/bus/hid/drivers/hid_steam/bind" <<< "$id" >/dev/null || true
    log "  • $id → hid_steam"
  done
  log "Восстановление завершено."
}
trap cleanup EXIT INT TERM

if (( ${#steam_ids[@]} )); then
  log "Загрузка hid_generic..."
  sudo modprobe hid_generic
  for id in "${steam_ids[@]}"; do
    log "Отключаем $id от hid_steam"
    sudo tee "/sys/bus/hid/drivers/hid_steam/unbind" <<< "$id" >/dev/null
    log "Подключаем $id к hid_generic"
    sudo tee "/sys/bus/hid/drivers/hid_generic/bind" <<< "$id" >/dev/null
  done
  log "Контроллер доступен через hid_generic"
fi

wait_for_port() {
  if [[ -n "$PORT" ]]; then
    log "Используем заданный порт: $PORT"
    return
  fi
  log "Жду /dev/ttyACM*..."
  while true; do
    local devs
    mapfile -t devs < <(compgen -G '/dev/ttyACM*' || true)
    if [[ ${#devs[@]} -gt 0 ]]; then
      PORT="${devs[0]}"
      log "Найдено устройство: $PORT"
      break
    fi
    sleep 1
  done
}

run_sender() {
  log "Запуск $SCRIPT на $PORT"
  if [[ -f "$ENV_DIR/bin/activate" ]]; then
    # shellcheck source=/dev/null
    source "$ENV_DIR/bin/activate"
    log "Venv $ENV_DIR активирован"
  else
    log "Warning: venv '$ENV_DIR' не найден"
  fi
  check_script_version
  [[ -f "$SCRIPT" ]] || { log "Error: '$SCRIPT' не найден"; exit 1; }
  exec python3 "$SCRIPT" -p "$PORT" >>"$LOGFILE" 2>&1
}

log "==== Запуск deck.sh v$VERSION ===="
while true; do
  wait_for_port
  run_sender
  log "$SCRIPT завершился с кодом $? — перезапуск через 1s"
  sleep 1
done
