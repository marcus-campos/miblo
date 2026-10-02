# PlatformIO extra script (env:native_asan): build_flags only reach the compiler, so the
# sanitizer runtimes must also be passed to the linker.
Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

env.Append(LINKFLAGS=["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])  # noqa: F821
