set -x
echo "=== ELF ==="
ls -la build/pong2.elf
/usr/lib/OpenOrbisSDK/bin/linux/readelf -h build/pong2.elf || readelf -h build/pong2.elf
echo
echo "=== create-eboot ==="
file /usr/lib/OpenOrbisSDK/bin/linux/create-eboot
ls -la /usr/lib/OpenOrbisSDK/bin/linux/create-eboot
ls -la /usr/lib/OpenOrbisSDK/bin/linux/create-fself
echo
echo "=== manual conversion ==="
rm -f build/manual.oelf eboot.bin
/usr/lib/OpenOrbisSDK/bin/linux/create-eboot -in=build/pong2.elf -out=build/manual.oelf -paid=0x3800000000000011 -ptype=fake -sdkver=72384769 -fwversion=0 -appversion=0
echo "exit=$?"
ls -la build/manual.oelf eboot.bin 2>&1
