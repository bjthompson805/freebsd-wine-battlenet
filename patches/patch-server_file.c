--- server/file.c.orig
+++ server/file.c
@@ -36,6 +36,13 @@
 #include <utime.h>
 #endif
 #include <poll.h>
+#ifdef HAVE_SYS_XATTR_H
+#include <sys/xattr.h>
+#endif
+#ifdef HAVE_SYS_EXTATTR_H
+#undef XATTR_ADDITIONAL_OPTIONS
+#include <sys/extattr.h>
+#endif
 
 #include "ntstatus.h"
 #include "windef.h"
@@ -364,7 +371,56 @@
         if (mode & S_IWOTH) ace->mask |= FILE_GENERIC_WRITE | DELETE | FILE_DELETE_CHILD;
     }
 
+    return sd;
+}
+
+/* The Unix mode holds only part of a security descriptor, so the whole
+ * descriptor is also kept in an extended attribute (same format as Proton). */
+#define WINE_XATTR_SD "user.wine.sd"
+#define XATTR_SD_MAX  65536
+
+struct security_descriptor *get_xattr_sd( int fd )
+{
+    struct security_descriptor *sd;
+    char *buffer;
+    ssize_t len;
+
+    if (!(buffer = malloc( XATTR_SD_MAX ))) return NULL;
+#if defined(HAVE_SYS_XATTR_H) && !defined(XATTR_ADDITIONAL_OPTIONS)
+    len = fgetxattr( fd, WINE_XATTR_SD, buffer, XATTR_SD_MAX );
+#elif defined(HAVE_SYS_EXTATTR_H)
+    len = extattr_get_fd( fd, EXTATTR_NAMESPACE_USER, WINE_XATTR_SD + 5, buffer, XATTR_SD_MAX );
+#else
+    len = -1;
+#endif
+    /* the descriptor follows its revision and the resource manager control bits */
+    if (len < 2 + (ssize_t)sizeof(*sd) || buffer[0] != SECURITY_DESCRIPTOR_REVISION ||
+        !sd_is_valid( (struct security_descriptor *)(buffer + 2), len - 2 ) ||
+        !(sd = mem_alloc( len - 2 )))
+    {
+        free( buffer );
+        return NULL;
+    }
+    memcpy( sd, buffer + 2, len - 2 );
+    free( buffer );
     return sd;
+}
+
+void set_xattr_sd( int fd, const struct security_descriptor *sd )
+{
+    data_size_t len = sizeof(*sd) + sd->owner_len + sd->group_len + sd->sacl_len + sd->dacl_len;
+    char *buffer;
+
+    if (2 + len > XATTR_SD_MAX || !(buffer = malloc( 2 + len ))) return;
+    buffer[0] = SECURITY_DESCRIPTOR_REVISION;
+    buffer[1] = 0;
+    memcpy( buffer + 2, sd, len );
+#if defined(HAVE_SYS_XATTR_H) && !defined(XATTR_ADDITIONAL_OPTIONS)
+    fsetxattr( fd, WINE_XATTR_SD, buffer, 2 + len, 0 );
+#elif defined(HAVE_SYS_EXTATTR_H)
+    extattr_set_fd( fd, EXTATTR_NAMESPACE_USER, WINE_XATTR_SD + 5, buffer, 2 + len );
+#endif
+    free( buffer );
 }
 
 static struct security_descriptor *file_get_sd( struct object *obj )
@@ -386,9 +442,10 @@
         (st.st_uid == file->uid))
         return obj->sd;
 
-    sd = mode_to_sd( st.st_mode,
-                     security_unix_uid_to_sid( st.st_uid ),
-                     token_get_primary_group( current->process->token ));
+    if (!(sd = get_xattr_sd( unix_fd )))
+        sd = mode_to_sd( st.st_mode,
+                         security_unix_uid_to_sid( st.st_uid ),
+                         token_get_primary_group( current->process->token ));
     if (!sd) return obj->sd;
 
     file->mode = st.st_mode;
@@ -492,6 +549,9 @@
 
     if (unix_fd == -1 || fstat( unix_fd, &st ) == -1) return 1;
 
+    /* default_set_sd() below merges the change into the current descriptor */
+    file_get_sd( obj );
+
     if (set_info & OWNER_SECURITY_INFORMATION)
     {
         owner = sd_get_owner( sd );
@@ -523,7 +583,11 @@
             file_set_error();
             return 0;
         }
+        file->mode = (st.st_mode & S_IFMT) | mode;
     }
+
+    if (!default_set_sd( obj, sd, set_info )) return 0;
+    set_xattr_sd( unix_fd, obj->sd );
     return 1;
 }
 
