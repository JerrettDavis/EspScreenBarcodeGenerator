Import("env")

# QRCode 0.0.1 declares bool/true/false for C before C23 made them keywords.
# Keep C dependencies on C17 while C++ sources retain the project's C++17 flags.
env.Append(CFLAGS=["-std=gnu17"])
