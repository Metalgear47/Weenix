set kernel memcheck on
handle SIGSEGV nostop noprint nopass
break dbg_panic_halt
break hard_shutdown
break bootstrap
add-symbol-file user/usr/bin/vfstest.exec 0x08048094
#b main
#b usr/bin/tests/memtest.c:145
#b usr/bin/tests/memtest.c:590
b usr/bin/tests/vfstest.c:849

continue
