savedcmd_/app/module/nata.ko := ld -r -m elf_x86_64 -z noexecstack --no-warn-rwx-segments --build-id=sha1  -T scripts/module.lds -o /app/module/nata.ko /app/module/nata.o /app/module/nata.mod.o
