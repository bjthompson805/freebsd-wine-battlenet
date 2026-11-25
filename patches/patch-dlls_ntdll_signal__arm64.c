--- dlls/ntdll/signal_arm64.c.orig
+++ dlls/ntdll/signal_arm64.c
@@ -233,9 +233,20 @@ NTSTATUS call_seh_handlers( EXCEPTION_RECORD *rec, CONTEXT *orig_context )
 
     for (;;)
     {
+        DWORD64 prev_pc = context.Pc, prev_sp = context.Sp;
+
         status = virtual_unwind( UNW_FLAG_EHANDLER, &dispatch, &context );
         if (status != STATUS_SUCCESS) return status;
 
+        /* like Windows, stop when unwinding makes no progress (a leaf frame without unwind
+         * info whose LR points back into itself, as in World of Warcraft's generated code) */
+        if (context.Pc == prev_pc && context.Sp == prev_sp)
+        {
+            WARN( "no unwind progress at pc %I64x sp %I64x\n", context.Pc, context.Sp );
+            rec->ExceptionFlags |= EXCEPTION_STACK_INVALID;
+            break;
+        }
+
     unwind_done:
         if (!dispatch.EstablisherFrame) break;
 
