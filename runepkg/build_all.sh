#!/usr/bin/env bash
set -euo pipefail

STUB_SRC=$(cat <<'EOF'
int runepkg_highlight_stub(void) { return 0; }
int runepkg_rust_ffi_available(void) { return 1; }
EOF
)

make_stub_lib() {
	mkdir -p target/release
	printf '%s\n' "$STUB_SRC" > target/release/stub_runepkg_ffi.c
	gcc -c -fPIC -O2 target/release/stub_runepkg_ffi.c -o target/release/stub_runepkg_ffi.o
	ar rcs target/release/librunepkg_ffi.a target/release/stub_runepkg_ffi.o
	echo "Rust: created stub static library target/release/librunepkg_ffi.a"
}

# Clean and build everything with full output using manual commands.
echo "== Clean =="
rm -f *.o *.d runepkg

echo "== Rust (clean-slate) =="
if command -v cargo >/dev/null 2>&1; then
	if cargo build --release --no-default-features --features clean-slate 2>/dev/null; then
		if [ -f "target/release/librunepkg_ffi.a" ] || [ -f "target/release/librunepkg_ffi.so" ]; then
			echo "Rust: built library successfully"
		else
			echo "Rust: cargo build did not produce expected library, creating stub"
			make_stub_lib
		fi
	else
		echo "Rust: cargo build failed, creating stub library"
		make_stub_lib
	fi
else
	echo "Cargo not found, creating stub library"
	make_stub_lib
fi

echo "== C compile =="
set -x
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -DWITH_RUST_FFI -c runepkg_rust_ffi.c -o runepkg_rust_ffi.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_cli.c -o runepkg_cli.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_config.c -o runepkg_config.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_handle.c -o runepkg_handle.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_util.c -o runepkg_util.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_pack.c -o runepkg_pack.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_hash.c -o runepkg_hash.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_storage.c -o runepkg_storage.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_defensive.c -o runepkg_defensive.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_completion.c -o runepkg_completion.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_install.c -o runepkg_install.o
set +x

echo "== C++ compile =="
set -x
g++ -Wall -Wextra -std=c++17 -D_GNU_SOURCE -g -MMD -MP -c runepkg_cpp_ffi.cpp -o runepkg_cpp_ffi.o
set +x

echo "== Link =="
set -x
g++ runepkg_cli.o runepkg_config.o runepkg_handle.o runepkg_util.o runepkg_pack.o runepkg_hash.o \
		runepkg_storage.o runepkg_defensive.o runepkg_rust_ffi.o runepkg_cpp_ffi.o \
		runepkg_completion.o runepkg_install.o \
		-o runepkg \
		-L./target/release -lrunepkg_ffi -ldl -lpthread -lm -lcurl -lssl -lcrypto
set +x
