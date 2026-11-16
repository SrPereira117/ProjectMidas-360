#!/usr/bin/env bash
# Verifica quais arquivos do Project Midas compilam para o 360 (powerpc-xsdk-elf-gcc),
# sem linkar nada. Serve de mapa de erros: roda em segundos e lista tudo de uma vez.
#
# uso: compile_check.sh <pasta-do-midas> <caminho-do-gcc> <pasta-de-saida>
set -u
MIDAS=$(cd "$1" && pwd)
CC=$2
mkdir -p "$3"
OUT=$(cd "$3" && pwd)   # caminho absoluto: o script entra na pasta do Midas
: > "$OUT/errors.log"
: > "$OUT/summary.txt"

cd "$MIDAS"

DEFS=(-DVERSION_US -DLANG_US -DREFRESH_NTSC -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM
      -DBUGFIX_R0 -DNONMATCHING -DAVOID_UB -D_LANGUAGE_C ${EXTRA_DEFS:-})
INCS=(-I. -Iinclude -Iinclude/ultra64 -Iinclude/PR -Isrc -Isrc/game -Isrc/inflate
      -Iport/include -Isrc/libultra)
FLAGS=(-std=gnu11 -ffreestanding -fms-extensions -fno-strict-aliasing -fwrapv
       -funsigned-char -fcommon -w -fsyntax-only)

# Mesma lista do CMakeLists do Midas: jogo, motor (menos o que o port apaga) e libultra.
EXCLUDE=" boot tlb_manage tlb_hardwire rmon usb debug crash motor "
files=()
for f in src/game/*.c; do files+=("$f"); done
for f in src/*.c; do
  b=$(basename "$f" .c)
  case "$EXCLUDE" in *" $b "*) ;; *) files+=("$f");; esac
done
while read -r f; do [ -f "$f" ] && files+=("$f"); done < <(
  grep -oE 'src/libultra(re)?/[A-Za-z0-9_/]+\.c' CMakeLists.txt | sort -u)
# Camada de port: espera-se que falhe (SDL e Windows) ate eu reescrever.
for f in port/src/*.c; do files+=("$f"); done

ok=0; bad=0
for f in "${files[@]}"; do
  if log=$("$CC" "${FLAGS[@]}" "${DEFS[@]}" "${INCS[@]}" "$f" 2>&1); then
    echo "OK    $f" >> "$OUT/summary.txt"; ok=$((ok+1))
  else
    n=$(grep -c 'error' <<<"$log")
    echo "FAIL($n) $f" >> "$OUT/summary.txt"; bad=$((bad+1))
    { echo "=== $f"; grep -m8 -E 'error|fatal' <<<"$log"; } >> "$OUT/errors.log"
  fi
done
echo "TOTAL: $ok ok, $bad com erro, de ${#files[@]} arquivos" | tee -a "$OUT/summary.txt"
