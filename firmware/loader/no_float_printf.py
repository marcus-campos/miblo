# PlatformIO extra script for the loader env: drops the framework's forced float printf/scanf
# support ("-u _printf_float -u _scanf_float", ~10 KB). The loader never formats floats, and every
# byte counts: the image must fit the stock GeekMagic firmware's small OTA space.
Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

flags = list(env["LINKFLAGS"])  # noqa: F821
out = []
i = 0
while i < len(flags):
    if flags[i] == "-u" and i + 1 < len(flags) and flags[i + 1] in ("_printf_float", "_scanf_float"):
        i += 2
        continue
    out.append(flags[i])
    i += 1
env.Replace(LINKFLAGS=out)  # noqa: F821
