#!/bin/sh
# Builds sce_sys/param.sfo with the toolchain's PkgTool. Fails loudly on bad IDs.
set -e
TITLE_ID="$1"; CONTENT_ID="$2"; TITLE="$3"; PKGTOOL="$4"
[ "${#TITLE_ID}" -eq 9 ]    || { echo "ERROR: TITLE_ID must be 9 chars (got ${#TITLE_ID})"; exit 1; }
[ "${#CONTENT_ID}" -eq 36 ] || { echo "ERROR: CONTENT_ID must be 36 chars (got ${#CONTENT_ID})"; exit 1; }
case "$CONTENT_ID" in *"$TITLE_ID"*) ;; *) echo "ERROR: CONTENT_ID must contain TITLE_ID"; exit 1;; esac
OUT=sce_sys/param.sfo
rm -f "$OUT"
"$PKGTOOL" sfo_new "$OUT"
set_i() { "$PKGTOOL" sfo_setentry "$OUT" "$1" --type Integer --maxsize 4 --value "$2"; }
set_s() { "$PKGTOOL" sfo_setentry "$OUT" "$1" --type Utf8 --maxsize "$2" --value "$3"; }
set_i APP_TYPE 0
set_s APP_VER 8 "01.00"
set_i ATTRIBUTE 0
set_s CATEGORY 4 "gd"
set_s CONTENT_ID 48 "$CONTENT_ID"
set_i DOWNLOAD_DATA_SIZE 0
set_s FORMAT 4 "obs"
set_i PARENTAL_LEVEL 1
set_i SYSTEM_VER 0
set_s TITLE 128 "$TITLE"
set_s TITLE_ID 12 "$TITLE_ID"
set_s VERSION 8 "01.00"
echo "param.sfo written"
