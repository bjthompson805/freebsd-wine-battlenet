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
@@ -278,6 +323,12 @@ static BOOL save_fpu( NEON128 vregs[32], ULONG *fpcr, ULONG *fpsr, const ucontex
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
@@ -302,6 +353,12 @@ static void restore_fpu( const CONTEXT *context, ucontext_t *sigcontext )
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
 
@@ -1176,7 +1233,7 @@ static void segv_handler( int signal, siginfo_t *siginfo, void *_sigcontext )
     struct thread_data *data = get_thread_data();
     CONTEXT context;
     EXCEPTION_RECORD rec = { .ExceptionAddress = (void *)PC_sig(sigcontext) };
-    DWORD64 esr = get_fault_esr( sigcontext );
+    DWORD64 esr = get_fault_esr( sigcontext, siginfo );
 
     switch (ESR_ELx_EC(esr))
     {
@@ -1263,7 +1320,7 @@ static void trap_handler( int signal, siginfo_t *siginfo, void *_sigcontext )
 
     save_context( &context, sigcontext );
 
-#ifdef linux
+#if defined(linux) || defined(__FreeBSD__)
     /* Only SIGSEGV/SIGBUS expose ESR, synthesize it instead. */
     switch (siginfo->si_code)
     {
@@ -1277,7 +1334,7 @@ static void trap_handler( int signal, siginfo_t *siginfo, void *_sigcontext )
         break;
     }
 #else
-    esr = get_fault_esr( sigcontext );
+    esr = get_fault_esr( sigcontext, siginfo );
 #endif
 
     switch (ESR_ELx_EC(esr))
@@ -1574,6 +1631,11 @@ static void usr2_handler( int signal, siginfo_t *siginfo, void *_sigcontext )
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
 
