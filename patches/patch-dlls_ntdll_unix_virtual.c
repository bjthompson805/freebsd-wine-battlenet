--- dlls/ntdll/unix/virtual.c.orig
+++ dlls/ntdll/unix/virtual.c
@@ -5186,6 +5186,10 @@
         if (get_committed_size( view, base, size, &vprot, VPROT_COMMITTED ) >= size && (vprot & VPROT_COMMITTED))
         {
             old = get_win32_prot( vprot, view->protect );
+            /* copy-on-write pages are normally already written (and thus private read-write
+             * pages on Windows) by the time their protection is changed; Chromium checks this */
+            if (old & PAGE_WRITECOPY) old = (old & ~PAGE_WRITECOPY) | PAGE_READWRITE;
+            if (old & PAGE_EXECUTE_WRITECOPY) old = (old & ~PAGE_EXECUTE_WRITECOPY) | PAGE_EXECUTE_READWRITE;
             status = set_protection( view, base, size, new_prot );
         }
         else status = STATUS_NOT_COMMITTED;
