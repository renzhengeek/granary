#!/bin/bash

SMP=2
MEM=1024
NETWORK=e1000
#NETWORK=virtio
GRAPHIC= #-nographic
GRAPHIC_FLAG=0

QEMU=$(which qemu-system-x86_64)
$QEMU -cpu host,level=9 -enable-kvm -d int,cpu_reset -smp $SMP -m $MEM $GRAPHIC -hda ~/vm.img -net nic,model=$NETWORK -net "user,hostfwd=tcp::5556-:22" -gdb tcp::9999 & 
#-monitor telnet:127.0.0.1:1234,server,nowait &

# To get into the Qemu console, run `telnet 127.0.0.1 1234`.
