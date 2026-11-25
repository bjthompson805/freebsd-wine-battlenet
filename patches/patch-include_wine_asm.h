--- include/wine/asm.h.orig
+++ include/wine/asm.h
@@ -252,11 +252,9 @@
                        ".seh_endprologue\n\t" \
                        "mov x8, #(" #id ")\n\t" \
                        "mov x9, x30\n\t" \
-                       "ldr x16, 1f\n\t" \
-                       "ldr x16, [x16]\n\t" \
+                       "ldr x16, [x18, #0x2f8]\n\t" /* TEB: arm64_thread_data.syscall_dispatcher */ \
                        "blr x16\n\t" \
-                       "ret\n" \
-                       "1:\t.quad " __ASM_NAME("__wine_syscall_dispatcher") )
+                       "ret" )
 #elif defined __arm64ec__
 # define __ASM_SYSCALL_FUNC(id,name) \
     asm( ".seh_proc \"#" #name "$hp_target\"\n\t" \
