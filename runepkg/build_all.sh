#!/usr/bin/env bash
set -euo pipefail

# Clean and build everything with full output using manual commands.
echo "== Clean =="
rm -f *.o *.d runepkg

echo "== Rust (clean-slate) =="
if command -v cargo >/dev/null 2>&1; then
	cargo build --release --no-default-features --features clean-slate
else
	echo "Cargo not found. Install rustc + cargo." >&2
	exit 1
fi

echo "== C compile =="
set -x
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_cli.c -o runepkg_cli.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_config.c -o runepkg_config.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_util.c -o runepkg_util.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_pack.c -o runepkg_pack.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_hash.c -o runepkg_hash.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_storage.c -o runepkg_storage.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_defensive.c -o runepkg_defensive.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_rust_api.c -o runepkg_rust_api.o
gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -c runepkg_cpp_api.c -o runepkg_cpp_api.o
set +x

echo "== C++ compile =="
set -x
g++ -Wall -Wextra -std=c++17 -D_GNU_SOURCE -g -MMD -MP -c runepkg_cpp_impl.cpp -o runepkg_cpp_impl.o
set +x

echo "== Link =="
set -x
g++ runepkg_cli.o runepkg_config.o runepkg_util.o runepkg_pack.o runepkg_hash.o \
		runepkg_storage.o runepkg_defensive.o runepkg_rust_api.o runepkg_cpp_api.o \
		runepkg_cpp_impl.o -o runepkg \
		-L./target/release -lrunepkg_highlight -ldl -lpthread -lm -lcurl -lssl -lcrypto
set +x
