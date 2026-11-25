--- dlls/ntdll/unix/system.c.orig
+++ dlls/ntdll/unix/system.c
@@ -61,6 +61,15 @@
 #ifdef HAVE_SYS_AUXV_H
 # include <sys/auxv.h>
 #endif
+#if defined(__FreeBSD__) && defined(AT_HWCAP) && !defined(HAVE_GETAUXVAL)
+/* FreeBSD reads the aux vector with elf_aux_info() instead. */
+static unsigned long getauxval( int type )
+{
+    unsigned long value = 0;
+    elf_aux_info( type, &value, sizeof(value) );
+    return value;
+}
+#endif
 #ifdef __APPLE__
 # include <CoreFoundation/CoreFoundation.h>
 # include <IOKit/IOKitLib.h>
