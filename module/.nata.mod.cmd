savedcmd_/app/module/nata.mod := printf '%s\n'   nata_main.o nata_net.o nata_blk.o | awk '!x[$$0]++ { print("/app/module/"$$0) }' > /app/module/nata.mod
