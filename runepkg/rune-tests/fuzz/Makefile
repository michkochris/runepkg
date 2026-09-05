CXX = clang++
CXXFLAGS = -std=c++17 -fsanitize=fuzzer,address -g -O1 -Wall
LDFLAGS = -lcrypto

all: fuzz_sanitize_path

fuzz_sanitize_path: fuzz_sanitize_path.cpp ../../runepkg_security.cpp ../../runepkg_util_cpp.cpp
	$(CXX) $(CXXFLAGS) fuzz_sanitize_path.cpp ../../runepkg_security.cpp ../../runepkg_util_cpp.cpp -o fuzz_sanitize_path $(LDFLAGS)

clean:
	rm -f fuzz_sanitize_path
