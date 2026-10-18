#!/bin/sh
SDK="${OO_PS4_TOOLCHAIN:-/usr/lib/OpenOrbisSDK}"
mkdir -p sce_sys/about
if [ -f /opt/sce_sys_default/about/right.sprx ]; then
    cp /opt/sce_sys_default/about/right.sprx sce_sys/about/
    echo "right.sprx: from /opt/sce_sys_default"
else
    find "$SDK" -name right.sprx -exec cp {} sce_sys/about/ \; 2>/dev/null
    echo "right.sprx: searched SDK"
fi
ls -la sce_sys/about/