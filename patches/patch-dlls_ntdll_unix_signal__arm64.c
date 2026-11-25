--- dlls/ntdll/unix/signal_arm64.c.orig
+++ dlls/ntdll/unix/signal_arm64.c
@@ -108,7 +108,7 @@ static struct fpsimd_context *get_fpsimd_context( const ucontext_t *sigcontext )
     return (struct fpsimd_context *)get_extended_sigcontext( sigcontext, FPSIMD_MAGIC );
 }
 
-static DWORD64 get_fault_esr( ucontext_t *sigcontext )
+static DWORD64 get_fault_esr( ucontext_t *sigcontext, const siginfo_t *siginfo )
 {
 #ifdef ESR_MAGIC
     struct esr_context *esr = (struct esr_context *)get_extended_sigcontext( sigcontext, ESR_MAGIC );
@@ -135,11 +135,56 @@ static DWORD64 make_esr( ULONG ec, ULONG info )
 # define FP_sig(context)            REG_sig(fp, context)    /* Frame pointer */
 # define LR_sig(context)            REG_sig(lr, context)    /* Link Register */
 
-static DWORD64 get_fault_esr( ucontext_t *sigcontext )
+static DWORD64 get_fault_esr( ucontext_t *sigcontext, const siginfo_t *siginfo )
 {
     return sigcontext->uc_mcontext->__es.__esr;
 }
 
+#elif defined(__FreeBSD__)
+
+/* All Registers access - only for local access; gp_x holds x0-x29, lr is separate */
+# define REG_sig(reg_name, context) ((context)->uc_mcontext.mc_gpregs.gp_ ## reg_name)
+# define REGn_sig(reg_num, context) ((context)->uc_mcontext.mc_gpregs.gp_x[reg_num])
+
+/* Special Registers access  */
+# define SP_sig(context)            REG_sig(sp, context)    /* Stack pointer */
+# define PC_sig(context)            REG_sig(elr, context)   /* Program counter */
+# define PSTATE_sig(context)        REG_sig(spsr, context)  /* Current State Register */
+# define FP_sig(context)            REGn_sig(29, context)   /* Frame pointer */
+# define LR_sig(context)            REG_sig(lr, context)    /* Link Register */
+
+static DWORD64 make_esr( ULONG ec, ULONG info )
+{
+    return ((DWORD64)ec << 26) | (info & 0xffff);
+}
+
+/* Whether the instruction at pc writes memory: a store of the load/store class (bit 22, L, clear),
+ * or an atomic, which reads and writes. */
+static BOOL insn_writes_memory( DWORD64 pc )
+{
+    UINT insn;
+
+    if (pc & 3) return FALSE;
+    insn = *(const UINT *)pc;
+    if ((insn & 0x0a000000) != 0x08000000) return FALSE;  /* not a load/store */
+    if ((insn & 0x3b200c00) == 0x38200000) return TRUE;   /* LDADD, SWP, ... */
+    if ((insn & 0x3fa07c00) == 0x08a07c00) return TRUE;   /* CAS */
+    if ((insn & 0xbfa07c00) == 0x08207c00) return TRUE;   /* CASP */
+    return !(insn & (1 << 22));
+}
+
+/* FreeBSD passes no ESR, only the exception class as si_trapno. Rebuild what Wine reads from it: the
+ * class, alignment faults (SIGBUS/BUS_ADRALN), and the write flag, from the faulting instruction. */
+static DWORD64 get_fault_esr( ucontext_t *sigcontext, const siginfo_t *siginfo )
+{
+    ULONG ec = siginfo->si_trapno & 0x3f, iss = 0;
+
+    if (siginfo->si_signo == SIGBUS && siginfo->si_code == BUS_ADRALN)
+        return make_esr( 0x24 /* data abort */, 0x21 /* alignment fault */ );
+    if ((ec == 0x24 || ec == 0x25) && insn_writes_memory( PC_sig(sigcontext) )) iss |= 1 << 6;  /* WnR */
+    return make_esr( ec, iss );
+}
+
 #endif /* linux */
 
 /* stack layout when calling KiUserExceptionDispatcher */
@@ -183,6 +228,7 @@ C_ASSERT( offsetof(struct callback_stack_layout, sp) == 0x20 );
 C_ASSERT( sizeof(struct callback_stack_layout) == 0x30 );
 
 #define RESTORE_FLAGS_EMULATION  0x00010000
+#define RESTORE_FLAGS_SINGLE_STEP 0x00020000  /* return through usr2_handler to re-arm PSTATE.SS */
 
 struct syscall_frame
 {
@@ -278,6 +324,12 @@ static BOOL save_fpu( NEON128 vregs[32], ULONG *fpcr, ULONG *fpsr, const ucontex
     *fpsr = sigcontext->uc_mcontext->__ns.__fpsr;
     memcpy( vregs, sigcontext->uc_mcontext->__ns.__v, 32 * sizeof(*vregs) );
     return TRUE;
+#elif defined(__FreeBSD__)
+    if (!(sigcontext->uc_mcontext.mc_flags & _MC_FP_VALID)) return FALSE;
+    *fpcr = sigcontext->uc_mcontext.mc_fpregs.fp_cr;
+    *fpsr = sigcontext->uc_mcontext.mc_fpregs.fp_sr;
+    memcpy( vregs, sigcontext->uc_mcontext.mc_fpregs.fp_q, 32 * sizeof(*vregs) );
+    return TRUE;
 #else
     return FALSE;
 #endif
@@ -302,6 +354,12 @@ static void restore_fpu( const CONTEXT *context, ucontext_t *sigcontext )
     sigcontext->uc_mcontext->__ns.__fpcr = context->Fpcr;
     sigcontext->uc_mcontext->__ns.__fpsr = context->Fpsr;
     memcpy( sigcontext->uc_mcontext->__ns.__v, context->V, sizeof(context->V) );
+#elif defined(__FreeBSD__)
+    /* The kernel reloads FP state from the frame only when it's marked valid. */
+    sigcontext->uc_mcontext.mc_fpregs.fp_cr = context->Fpcr;
+    sigcontext->uc_mcontext.mc_fpregs.fp_sr = context->Fpsr;
+    memcpy( sigcontext->uc_mcontext.mc_fpregs.fp_q, context->V, sizeof(context->V) );
+    sigcontext->uc_mcontext.mc_flags |= _MC_FP_VALID;
 #endif
 }
 
@@ -1176,7 +1234,7 @@ static void segv_handler( int signal, siginfo_t *siginfo, void *_sigcontext )
     struct thread_data *data = get_thread_data();
     CONTEXT context;
     EXCEPTION_RECORD rec = { .ExceptionAddress = (void *)PC_sig(sigcontext) };
-    DWORD64 esr = get_fault_esr( sigcontext );
+    DWORD64 esr = get_fault_esr( sigcontext, siginfo );
 
     switch (ESR_ELx_EC(esr))
     {
@@ -1247,6 +1305,47 @@ static void ill_handler( int signal, siginfo_t *siginfo, void *_sigcontext )
 }
 
 
+static void save_syscall_entry_frame( ucontext_t *sigcontext );
+
+/**********************************************************************
+ *           handle_syscall_trap
+ *
+ * Handle a single-step trap at a syscall dispatcher entry. The PE side
+ * enters the dispatchers with a branch, not svc, so without this a
+ * stepped thread steps through the Unix side too.
+ */
+static BOOL handle_syscall_trap( ucontext_t *sigcontext, siginfo_t *siginfo )
+{
+    extern const ULONG_PTR __wine_syscall_dispatcher_kernel_stack_ptr;
+    extern const ULONG_PTR __wine_unix_call_dispatcher_kernel_stack_ptr;
+    struct syscall_frame *frame = get_syscall_frame( get_thread_data() );
+
+    if (siginfo->si_code != TRAP_TRACE) return FALSE;
+
+    /* disallow single-stepping through a syscall */
+
+    if (PC_sig(sigcontext) == (ULONG_PTR)__wine_syscall_dispatcher)
+    {
+        save_syscall_entry_frame( sigcontext );
+        PC_sig(sigcontext) = __wine_syscall_dispatcher_kernel_stack_ptr;
+    }
+    else if (PC_sig(sigcontext) == (ULONG_PTR)__wine_unix_call_dispatcher)
+    {
+        save_syscall_entry_frame( sigcontext );
+        frame->lr = LR_sig(sigcontext);  /* x9 holds no return address here */
+        PC_sig(sigcontext) = __wine_unix_call_dispatcher_kernel_stack_ptr;
+    }
+    else return FALSE;
+
+    TRACE_(seh)( "ignoring trap in syscall pc=%p\n", (void *)frame->pc );
+
+    /* frame->cpsr keeps SS: the step resumes once the call returns */
+    frame->restore_flags |= RESTORE_FLAGS_SINGLE_STEP;
+    PSTATE_sig(sigcontext) &= ~0x200000;  /* PSTATE.SS */
+    return TRUE;
+}
+
+
 /**********************************************************************
  *		trap_handler
  *
@@ -1261,9 +1360,11 @@ static void trap_handler( int signal, siginfo_t *siginfo, void *_sigcontext )
                              .ExceptionAddress = (void *)PC_sig(sigcontext) };
     DWORD64 esr = 0;
 
+    if (handle_syscall_trap( sigcontext, siginfo )) return;
+
     save_context( &context, sigcontext );
 
-#ifdef linux
+#if defined(linux) || defined(__FreeBSD__)
     /* Only SIGSEGV/SIGBUS expose ESR, synthesize it instead. */
     switch (siginfo->si_code)
     {
@@ -1277,7 +1378,7 @@ static void trap_handler( int signal, siginfo_t *siginfo, void *_sigcontext )
         break;
     }
 #else
-    esr = get_fault_esr( sigcontext );
+    esr = get_fault_esr( sigcontext, siginfo );
 #endif
 
     switch (ESR_ELx_EC(esr))
@@ -1540,6 +1641,8 @@ static void usr2_handler( int signal, siginfo_t *siginfo, void *_sigcontext )
     if (!is_inside_syscall( data, SP_sig(sigcontext) )) return;
     if (!frame) return;
 
+    frame->restore_flags &= ~RESTORE_FLAGS_SINGLE_STEP;
+
     if (is_emulated_code( frame->pc ))
     {
         CONTEXT *user_context = (CONTEXT *)((frame->sp - sizeof(CONTEXT)) & ~15);
@@ -1574,6 +1677,11 @@ static void usr2_handler( int signal, siginfo_t *siginfo, void *_sigcontext )
     sigcontext->uc_mcontext->__ns.__fpcr = frame->fpcr;
     sigcontext->uc_mcontext->__ns.__fpsr = frame->fpsr;
     memcpy( sigcontext->uc_mcontext->__ns.__v, frame->v, sizeof(frame->v) );
+#elif defined(__FreeBSD__)
+    sigcontext->uc_mcontext.mc_fpregs.fp_cr = frame->fpcr;
+    sigcontext->uc_mcontext.mc_fpregs.fp_sr = frame->fpsr;
+    memcpy( sigcontext->uc_mcontext.mc_fpregs.fp_q, frame->v, sizeof(frame->v) );
+    sigcontext->uc_mcontext.mc_flags |= _MC_FP_VALID;
 #endif
 }
 
@@ -1841,7 +1949,10 @@ __ASM_GLOBAL_FUNC( __wine_syscall_dispatcher,
                    __ASM_CFI_CFA_IS_AT2(sp, 0x98, 0x02) /* frame->syscall_cfa */
                    __ASM_LOCAL_LABEL("__wine_syscall_dispatcher_return") ":\n\t"
                    "ldr w16, [sp, #0x10c]\n\t"  /* frame->restore_flags */
-                   "tbz x16, #16, 1f\n\t"       /* RESTORE_FLAGS_EMULATION */
+                   "tbz x16, #17, 1f\n\t"       /* RESTORE_FLAGS_SINGLE_STEP */
+                   "str x0, [sp, #0x00]\n\t"    /* frame->x[0] = return value */
+                   "bl " __ASM_NAME("syscall_dispatcher_return_slowpath") "\n"
+                   "1:\ttbz x16, #16, 1f\n\t"   /* RESTORE_FLAGS_EMULATION */
                    "bl " __ASM_NAME("syscall_dispatcher_return_slowpath") "\n"
                    "1:\ttbz x16, #1, 2f\n"      /* CONTEXT_INTEGER */
                    "ldp x12, x13, [sp, #0x80]\n\t" /* frame->x[16..17] */
