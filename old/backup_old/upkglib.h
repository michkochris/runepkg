/******************************************************************************
 *  Filename:    upkglib.h
 *  Author:      <michkochris@gmail.com>
 *  Date:        started 12-31-2024
 *  Description: upkg manages linux .deb pkg's
 *
 *  Copyright (c) 2025 upkg (ulinux) All rights reserved.
 *  GPLV3
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation, either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program. If not, see <https://www.gnu.org/licenses/>.
 ******************************************************************************/
/*file description: file to export functions from upkglib.c*/
#ifndef UPKGLIB_H
#define UPKGLIB_H
#define NAME    "upkg"
#define VERSION "1.0"
#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define BLUE    "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN    "\x1b[36m"
#define WHITE   "\x1b[0;37m"
#define RESET   "\x1b[0m"
void badmsg(char *text);
void errormsg(char *text);
void goodmsg(char *text);
void usermsg(char *text);
void success(char *text);
void testmsg();
void usage_info();
void help_msg();
void version_info();
void license_info();
int create_dir(const char *path);
void sanitize_path(char *path);
int remove_dir(const char *destruct_dir);
char *concat_path(const char *dir, const char *filename);
void extract_deb(const char *deb_file, const char *dest_dir);
void extract_tar_xz(const char *tarxz,const char *tdest);
void extract_data_tar_xz(const char *data_tar_xz, const char *unpack_dir);
char *search_file(const char *control, const char *str_str);
char *searchandreadtoend(const char* filename, const char* searchString);
void rmstr(char *str, const char *sub);
void remove_white(char *str);
char **collect_file_list(const char *unpack_dir, int *file_count);
void print_pkg_file_list(const char *pkgname);
void print_pkg_file_list_glob_veiw(const char *pkgname);
#endif
