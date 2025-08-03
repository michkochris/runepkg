/******************************************************************************
 *  Filename:    upkgstruct.h
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
/*file description: file to export function from upkgstruct.c*/
#ifndef UPKGSTRUCT_H
#define UPKGSTRUCT_H

#define PKGNAME_SIZE     32
#define VERSION_SIZE     32
#define ARCH_SIZE        32
#define MAINTAINER_SIZE 128
#define HOMEPAGE_SIZE    64
#define SOURCES_SIZE    128
#define SECTION_SIZE     32
#define PRIORITY_SIZE    32
#define DEPENDS_SIZE    128
#define COMMENT_SIZE    128
#define DESCRIPTION_SIZE 1028

struct Pkginfo gatherinfo();
void printpkginfo(char *name);
void resetstruct(struct Pkginfo *p);
void add_files_to_pkginfo(Pkginfo *info, const char *unpack_dir);

#endif
