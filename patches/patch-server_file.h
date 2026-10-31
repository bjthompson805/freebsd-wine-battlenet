--- server/file.h.orig
+++ server/file.h
@@ -162,6 +162,8 @@
 extern void file_set_error(void);
 extern struct security_descriptor *mode_to_sd( mode_t mode, const struct sid *user, const struct sid *group );
 extern mode_t sd_to_mode( const struct security_descriptor *sd, const struct sid *owner );
+extern struct security_descriptor *get_xattr_sd( int fd );
+extern void set_xattr_sd( int fd, const struct security_descriptor *sd );
 extern int is_file_executable( const char *name );
 
 /* file mapping functions */
