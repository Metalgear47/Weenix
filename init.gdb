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
# break s5fs_subr.c:602
# break ata.c:533
# break vmmap.c:443
# break vmmap_remove
#break handle_pagefault
#break pt_virt_to_phys
add-symbol-file user/usr/bin/hello.exec 0x08048094
b main
b pagefault.c:56
#b sys_open

continue
