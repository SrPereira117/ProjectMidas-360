#!/usr/bin/env bash
# Marco 2: compila DE VERDADE (gera objetos) o Midas com o compilador do xsdk e tenta ligar tudo
# com um main vazio, so pra listar o que ainda falta (simbolos nao resolvidos).
# uso: build_core.sh <pasta-do-midas> <pasta-do-xsdk> <prefixo-do-toolchain> <saida>
set -u
MIDAS=$(cd "$1" && pwd); XSDK=$(cd "$2" && pwd); PFX=$3
mkdir -p "$4"; OUT=$(cd "$4" && pwd)
CC="${PFX}gcc"
OBJ="$OUT/obj"; rm -rf "$OBJ"; mkdir -p "$OBJ"
cd "$MIDAS"

export CC OBJ XSDK
export CFLAGS_ALL="-mcpu=cell -fno-pic -O2 -std=gnu11 -ffreestanding -fms-extensions -fno-strict-aliasing
 -fwrapv -funsigned-char -fcommon -ffp-contract=off -w -DXENON
 -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
 -Wno-error=implicit-int -Wno-error=return-mismatch
 -DVERSION_US -DLANG_US -DREFRESH_NTSC -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBUGFIX_R0 -DNONMATCHING -DAVOID_UB -D_LANGUAGE_C
 -I. -Iinclude -Iinclude/ultra64 -Iinclude/PR -Isrc -Isrc/game -Isrc/inflate -Iport/include -Isrc/libultra"

compile_one() {
  f=$1; o="$OBJ/${f//\//__}.o"
  if $CC $CFLAGS_ALL -c "$f" -o "$o" 2>"$o.err"; then echo "OK    $f"; else echo "FAIL  $f"; fi
}
export -f compile_one

EXCLUDE=" boot tlb_manage tlb_hardwire rmon usb debug crash motor "
files=()
for f in src/game/*.c; do files+=("$f"); done
for f in src/*.c; do b=$(basename "$f" .c); case "$EXCLUDE" in *" $b "*) ;; *) files+=("$f");; esac; done
while read -r f; do [ -f "$f" ] && files+=("$f"); done < <(grep -oE 'src/libultra(re)?/[A-Za-z0-9_/]+\.c' CMakeLists.txt | sort -u)
for f in port/src/*.c; do files+=("$f"); done

printf '%s\n' "${files[@]}" | xargs -P"$(nproc)" -I{} bash -c 'compile_one {}' | sort -k2 > "$OUT/compile.txt"
ok=$(grep -c '^OK' "$OUT/compile.txt"); bad=$(grep -c '^FAIL' "$OUT/compile.txt")
echo "COMPILACAO: $ok objetos ok, $bad falharam, de ${#files[@]} arquivos" | tee "$OUT/summary.txt"

: > "$OUT/compile_errors.txt"
for f in $(grep '^FAIL' "$OUT/compile.txt" | awk '{print $2}'); do
  o="$OBJ/${f//\//__}.o.err"
  { echo "=== $f"; grep -m4 -E 'error|Error|fatal' "$o"; } >> "$OUT/compile_errors.txt"
done

# Liga o que compilou, com um main vazio, so pra ver os simbolos que faltam.
echo 'int main(void){return 0;}' > "$OUT/stub_main.c"
$CC -mcpu=cell -fno-pic -c "$OUT/stub_main.c" -o "$OBJ/zz_stub_main.o"
$CC -mcpu=cell -fno-pic -static -nostdlib -T "$XSDK/src/xsdk.ld" "$OBJ"/*.o -o "$OUT/core.elf" \
  -Wl,--start-group -lstdc++ "$XSDK/build/libxsdk.a" -lc -lm -lgcc -Wl,--end-group \
  -Wl,--unresolved-symbols=report-all > "$OUT/link.txt" 2>&1
echo "LIGACAO: codigo de saida $? ($(grep -c 'undefined reference' "$OUT/link.txt") referencias nao resolvidas)" | tee -a "$OUT/summary.txt"
