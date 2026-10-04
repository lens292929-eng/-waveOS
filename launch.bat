@echo off

qemu-system-x86_64 ^
    -machine pc ^
    -m 256M ^
    -drive file=build/waveOS.img,format=raw,if=ide,index=0,media=disk ^
    -drive file=build/data.img,format=raw,if=ide,index=1,media=disk ^
    -bios tools\OVMF.fd ^
    -audiodev sdl,id=snd0 -machine pcspk-audiodev=snd0