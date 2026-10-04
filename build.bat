@echo off
setlocal EnableDelayedExpansion

echo ========================================
echo           waveOS build system
echo ========================================

if not exist build mkdir build

set CLANG_FLAGS=-ffreestanding -fno-builtin -fno-stack-protector ^
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx ^
    -Wall -Wextra

REM ============================================================
echo.
echo [1/6] Compiling UEFI bootloader...
REM ============================================================

clang ^
    --target=x86_64-pc-win32-coff ^
    %CLANG_FLAGS% ^
    -fshort-wchar ^
    -fno-builtin ^
    -fno-builtin-memcpy ^
    -c boot\boot.c ^
    -o build\boot.o

if errorlevel 1 goto :error

REM ============================================================
echo [2/6] Linking BOOTX64.EFI...
REM ============================================================

lld-link ^
    /subsystem:efi_application ^
    /entry:efi_main ^
    /machine:x64 ^
    /nodefaultlib ^
    /dll ^
    /out:build\BOOTX64.EFI ^
    build\boot.o

if errorlevel 1 goto :error

REM ============================================================
echo [3/6] Compiling kernel...
REM ============================================================

set KFLAGS=--target=x86_64-unknown-elf %CLANG_FLAGS% ^
    -mcmodel=kernel -O2 -fno-pic -fno-pie

clang %KFLAGS% -c kernel\kernel.c      -o build\kernel.o
if errorlevel 1 goto :error

clang %KFLAGS% -c kernel\framebuffer.c -o build\framebuffer.o
if errorlevel 1 goto :error

clang %KFLAGS% -c kernel\font.c        -o build\font.o
if errorlevel 1 goto :error

clang %KFLAGS% -c kernel\idt.c        -o build\idt.o
if errorlevel 1 goto :error

echo Compiling idt_load...
nasm -f elf64 kernel\idt_load.asm -o build\idt_load.o
if errorlevel 1 goto :error

echo Compiling isr stubs (asm)...
nasm -f elf64 kernel\isr_stubs.asm -o build\isr_stubs.o
if errorlevel 1 goto :error

echo Compiling isr dispatcher (c)...
clang %KFLAGS% -c kernel\isr.c -o build\isr.o
if errorlevel 1 goto :error

echo Compiling keyhboard (c)...
clang %KFLAGS% -c kernel\keyboard.c -o build\keyboard.o
if errorlevel 1 goto :error

echo Compiling shell (c)...
clang %KFLAGS% -c kernel\shell.c -o build\shell.o
if errorlevel 1 goto :error

echo Compiling pit (c)...
clang %KFLAGS% -c kernel\pit.c -o build\pit.o
if errorlevel 1 goto :error

echo Compiling edit (c)...
clang %KFLAGS% -c kernel\edit.c -o build\edit.o
if errorlevel 1 goto :error

echo Compiling ramdisk (c)...
clang %KFLAGS% -c kernel\ramdisk.c -o build\ramdisk.o
if errorlevel 1 goto :error

echo Compiling wfs (c)...
clang %KFLAGS% -c kernel\wfs.c -o build\wfs.o
if errorlevel 1 goto :error

echo Compiling ata (c)...
clang %KFLAGS% -c kernel\ata.c -o build\ata.o
if errorlevel 1 goto :error

echo Compiling mouse (c)...
clang %KFLAGS% -c kernel\mouse.c -o build\mouse.o
if errorlevel 1 goto :error

echo Compiling cursor (c)...
clang %KFLAGS% -c kernel\cursor.c -o build\cursor.o
if errorlevel 1 goto :error

echo Compiling desktop (c)...
clang %KFLAGS% -c kernel\desktop.c -o build\desktop.o
if errorlevel 1 goto :error

echo Compiling mtrr (c)...
clang %KFLAGS% -c kernel\mtrr.c -o build\mtrr.o
if errorlevel 1 goto :error

echo Compiling fat32 (c)...
clang %KFLAGS% -c kernel\fat32.c -o build\fat32.o
if errorlevel 1 goto :error


REM ============================================================
echo [4/6] Linking kernel...
REM ============================================================

ld.lld ^
    -T kernel\kernel.ld ^
    -nostdlib ^
    -static ^
    --no-dynamic-linker ^
    -o build\kernel.elf ^
    build\kernel.o ^
    build\framebuffer.o ^
    build\font.o ^
    build\idt.o ^
    build\isr.o ^
    build\isr_stubs.o ^
    build\idt_load.o ^
    build\keyboard.o ^
    build\shell.o ^
    build\pit.o ^
    build\edit.o ^
    build\ramdisk.o ^
    build\wfs.o ^
    build\ata.o ^
    build\mouse.o ^
    build\cursor.o ^
    build\desktop.o ^
    build\mtrr.o ^
    build\fat32.o

if errorlevel 1 goto :error

REM ============================================================
echo [5/6] Converting kernel to flat binary...
REM ============================================================

llvm-objcopy -O binary build\kernel.elf build\KERNEL.BIN
if errorlevel 1 goto :error

REM ============================================================
echo [6/6] Building FAT32 EFI image...
REM ============================================================

python tools\make_efi_image.py
if errorlevel 1 goto :error

echo.
echo ========================================
echo             BUILD SUCCESS
echo ========================================
echo.
echo Output:
echo   build\BOOTX64.EFI
echo   build\KERNEL.BIN
echo   build\waveOS.img
echo.
echo Run:
echo   launch.bat
echo.

launch.bat
goto :end

:error
echo.
echo ========================================
echo             BUILD FAILED
echo ========================================
echo.
exit /b 1

:end
endlocal