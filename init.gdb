set kernel memcheck on
handle SIGSEGV nostop noprint nopass
break dbg_panic_halt
break hard_shutdown
break bootstrap
break dir_namev
break lookup
break ramfs_lookup

continue
