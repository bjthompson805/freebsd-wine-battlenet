--- server/change.c.orig
+++ server/change.c
@@ -333,9 +333,10 @@
         (st.st_uid == dir->uid))
         return obj->sd;
 
-    sd = mode_to_sd( st.st_mode,
-                     security_unix_uid_to_sid( st.st_uid ),
-                     token_get_primary_group( current->process->token ));
+    if (!(sd = get_xattr_sd( unix_fd )))
+        sd = mode_to_sd( st.st_mode,
+                         security_unix_uid_to_sid( st.st_uid ),
+                         token_get_primary_group( current->process->token ));
     if (!sd) return obj->sd;
 
     dir->mode = st.st_mode;
@@ -360,6 +361,9 @@
 
     if (unix_fd == -1 || fstat( unix_fd, &st ) == -1) return 1;
 
+    /* default_set_sd() below merges the change into the current descriptor */
+    dir_get_sd( obj );
+
     if (set_info & OWNER_SECURITY_INFORMATION)
     {
         owner = sd_get_owner( sd );
@@ -389,7 +393,11 @@
             file_set_error();
             return 0;
         }
+        dir->mode = (st.st_mode & S_IFMT) | mode;
     }
+
+    if (!default_set_sd( obj, sd, set_info )) return 0;
+    set_xattr_sd( unix_fd, obj->sd );
     return 1;
 }
 
