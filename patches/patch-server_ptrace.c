--- server/ptrace.c.orig
+++ server/ptrace.c
@@ -186,6 +186,11 @@
     return thread->unix_pid;
 }
 
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+/* signal that stopped the thread instead of the attach SIGSTOP, delivered again on detach */
+static int ptrace_stop_sig;
+#endif
+
 /* wait for a ptraced child to get a certain signal */
 static int waitpid_thread( struct thread *thread, int signal )
 {
@@ -209,7 +214,16 @@
             else perror( "waitpid" );
             stop_watchdog();
             return 0;
+        }
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+        /* FreeBSD may report a pending signal instead of the PT_ATTACH stop, and the
+         * attach stop is not reported afterwards; the thread is stopped either way */
+        if (signal == SIGSTOP && WIFSTOPPED(status) && WSTOPSIG(status) != SIGSTOP)
+        {
+            ptrace_stop_sig = WSTOPSIG(status);
+            break;
         }
+#endif
         res = handle_child_status( thread, res, status, signal );
         if (!res || res == signal) break;
     }
@@ -273,8 +287,14 @@
 /* resume a thread after we have used ptrace on it */
 static void resume_after_ptrace( struct thread *thread )
 {
+    int sig = 0;
+
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+    sig = ptrace_stop_sig;
+    ptrace_stop_sig = 0;
+#endif
     if (thread->unix_pid == -1) return;
-    if (ptrace( PTRACE_DETACH, get_ptrace_pid(thread), (caddr_t)1, 0 ) == -1)
+    if (ptrace( PTRACE_DETACH, get_ptrace_pid(thread), (caddr_t)1, sig ) == -1)
     {
         if (errno == ESRCH) thread->unix_pid = thread->unix_tid = -1;  /* thread got killed */
     }
