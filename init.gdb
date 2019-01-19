set kernel memcheck on
handle SIGSEGV nostop noprint nopass
break dbg_panic_halt
break hard_shutdown
break bootstrap
add-symbol-file user/usr/bin/eatmem.exec 0x080483f0
b usr/bin/tests/eatmem.c:142
b pagefault.c:60
b pagefault.c:65
b pagefault.c:75
b pagefault.c:84
b pagefault.c:97

continue
