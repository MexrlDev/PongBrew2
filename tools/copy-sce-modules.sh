#!/bin/sh
set -e
SDK="${OO_PS4_TOOLCHAIN:-/usr/lib/OpenOrbisSDK}"
rm -rf sce_module sce_sys/about
mkdir -p sce_module sce_sys/about

if [ -d /opt/sce_module_default ] && \
   find /opt/sce_module_default -name '*.prx' -print -quit 2>/dev/null | grep -q .; then
  cp /opt/sce_module_default/*.prx sce_module/
  echo "sce_module: copied from /opt/sce_module_default"
elif find "$SDK" -name '*.prx' -path '*sce_module*' -print -quit 2>/dev/null | grep -q .; then
  find "$SDK" -name '*.prx' -path '*sce_module*' -exec cp {} sce_module/ \;
  echo "sce_module: copied from SDK"
else
  echo "WARNING: no .prx files found - PKG will install but may not launch"
fi

if [ -f /opt/sce_sys_default/about/right.sprx ]; then
  cp /opt/sce_sys_default/about/right.sprx sce_sys/about/
else
  find "$SDK" -name right.sprx -exec cp {} sce_sys/about/ \; 2>/dev/null || true
fi

echo "--- sce_module/ ---"
ls -la sce_module/
echo "--- sce_sys/about/ ---"
ls -la sce_sys/about/