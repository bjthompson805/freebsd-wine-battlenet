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
@@ -1617,6 +1626,28 @@ static NTSTATUS create_logical_proc_info(void)
     if (nt_status != STATUS_SUCCESS)
         goto end;
 
+    /* With no topology from the OS (FreeBSD on arm64), hwloc reports bare PUs:
+     * make each one a core in a single package. */
+    if (hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_CORE) <= 0)
+    {
+        hwloc_obj_t pu;
+
+        if (hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_PACKAGE) <= 0 &&
+            !logical_proc_info_add_by_id(RelationProcessorPackage, 0, hwloc_bitmap_to_ulong(root_obj->cpuset)))
+        {
+            nt_status = STATUS_NO_MEMORY;
+            goto end;
+        }
+        for (pu = hwloc_get_obj_by_type(topology, HWLOC_OBJ_PU, 0); pu != NULL; pu = pu->next_cousin)
+        {
+            if (!logical_proc_info_add_by_id(RelationProcessorCore, pu->logical_index, hwloc_bitmap_to_ulong(pu->cpuset)))
+            {
+                nt_status = STATUS_NO_MEMORY;
+                goto end;
+            }
+        }
+    }
+
     nt_status = add_hwloc_numa_nodes(topology);
     if (nt_status != STATUS_SUCCESS)
         goto end;
