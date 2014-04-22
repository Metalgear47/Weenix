set kernel memcheck on
handle SIGSEGV nostop noprint nopass
break dbg_panic_halt
break hard_shutdown
break bootstrap
# break dir_namev
# break lookup
# break ramfs_lookup
# break special_file_write
# break null_write
# break s5fs_subr.c:133
break s5fs_subr.c:602
break ata.c:533

continue
