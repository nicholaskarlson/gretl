#ifndef FILELISTS_H
#define FILELISTS_H

#define MAXRECENT 4

void mkfilelist (int filetype, char *newfile);

void init_fileptrs (void);

void initialize_file_lists (void);

void delete_from_filelist (int filetype, const char *fname);

void add_files_to_menus (void);

void trim_homedir (char *fname);

void rc_save_file_lists (FILE *fp);

GList *get_working_dir_list (void);

int rc_read_file_lists (FILE *fp, char *prev);

#ifdef G_OS_WIN32

void reg_save_file_lists (void);
void reg_read_file_lists (void);

#endif

#endif /* FILELISTS_H */
