savedcmd_nata.mod := printf '%s\n'   nata_main.o nata_net.o nata_blk.o | awk '!x[$$0]++ { print("./"$$0) }' > nata.mod
