#!/usr/bin/env bash
# Re-encode the sound effects for the client (issue #17).
#
# QSoundEffect plays WAV only, so the effects stay PCM WAV but drop to
# 22.05 kHz mono 16-bit, which is what a UI blip or a strike needs and a
# quarter of the original 44.1 kHz stereo payload. The music is already
# Ogg Vorbis and is left alone. Idempotent: re-running converts nothing that
# is already in the target format.
set -euo pipefail
cd "$(dirname "$0")/../assets/audio"

convert() {
  local f="$1"
  local info
  info=$(ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate,channels -of csv=p=0 "$f")
  if [[ "$info" == "22050,1" ]]; then
    return
  fi
  local tmp="${f%.wav}.tmp.wav"
  ffmpeg -v error -y -i "$f" -ac 1 -ar 22050 -sample_fmt s16 "$tmp"
  mv "$tmp" "$f"
  echo "converted $f ($info -> 22050,1)"
}

for f in *.wav units/*.wav; do
  convert "$f"
done
du -sh . units
