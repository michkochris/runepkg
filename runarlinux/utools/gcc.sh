#!/usr/bin/env bash
#
# initial temporary gcc build
#
# gcc 1st phaze disable libs
# gcc 2nd phaze not disable libs
# gcc 3rd phase enable libs
# uports: a universal linux/gnu ports collection
. ./uports-config && PATH=$tpath

pkgname=utoolscross-gcc
pkgver=13.2.0
pkgrel=1
maintainer="linuxchrist@gmail.com"
description="contains the GNU compiler collection, which includes the C and C++ compilers."
homepage="https://gcc.gnu.org/"
sources="https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz"
arch=amd64
depends=
comment="uports: a universal linux/gnu ports collection"
checkpkginfo

cd $src
if [ ! -f gcc-13.2.0.tar.xz ]; then wget $sources; fi
tar -xvf gcc-13.2.0.tar.xz
cd gcc-$pkgver

#--------------------------------------------------------------------------------------------------------
case $(uname -m) in
  x86_64)
    sed -e '/m64=/s/lib64/lib/' \
        -i.orig gcc/config/i386/t-linux64
 ;;
esac
# change GCC's default dynamic linker to use the one installed in /tools.
sed -i 's,/lib/ld-linux.so.2,/utools/lib32/ld-linux.so.2,g' gcc/config/i386/linux64.h
sed -i 's,/lib64/ld-linux-x86-64.so.2,/utools/lib/ld-linux-x86-64.so.2,g' gcc/config/i386/linux64.h
sed -i 's,/libx32/ld-linux-x32.so.2,/utools/libx32/ld-linux-x32.so.2,g' gcc/config/i386/linux64.h

echo -en '\n#undef STANDARD_STARTFILE_PREFIX_1\n#define STANDARD_STARTFILE_PREFIX_1 "/utools/lib/"\n' >> gcc/config/i386/linux64.h
#--------------------------------------------------------------------------------------------------------
mkdir ../gccbuild && cd ../gccbuild
../gcc-$pkgver/configure --target=$crostarget		\
			 --prefix=$utoolscross		\
			 --with-glibc-version=2.38	\
			 --with-sysroot=$ulinux		\
			 --with-local-prefix=$utools	\
			 --includedir=/usr/include
			 --with-native-system-header-dir=/utools/include \
			 --with-newlib			\
			 --without-headers		\
			 --with-system-zlib		\
			 --with-system-zstd		\
	    		 --with-system-gmp		\
	    		 --with-system-mpfr		\
	    		 --with-system-mpc		\
	    		 --with-system-isl		\
			 --disable-nls			\
                         --disable-libatomic		\
                         --disable-libgomp		\
                         --disable-libmudflap		\
			 --disable-libitm		\
                         --disable-libmpx		\
                         --disable-libssp		\
                         --disable-libvtv		\
                         --disable-libquadmath		\
                         --disable-libstdcxx		\
                         --disable-libstdc++-v3		\
                         --disable-libsanitizer		\
                         --disable-decimal-float	\
			 --disable-shared		\
                         --disable-threads		\
                         --disable-multilib		\
			 --enable-default-pie		\
			 --enable-default-ssp		\
			 --enable-languages=c,c++

make -j$(nproc)
make install DESTDIR=$installdir || exit $?

defaultpackager
cd $src && rm -rf gcc-$pkgver gccbuild
cd $pkgdir && dpkg --force-all -i $fullpkgname.deb
# end of file
