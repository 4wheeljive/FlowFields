import os
Import("env")

# Use 12 cores (leave headroom to avoid OOM with large IDF builds)
env.SetOption("num_jobs", 12)

# Enable ccache for ESP-IDF CMake build (where most compile time lives)
os.environ["IDF_CCACHE_ENABLE"] = "1"
os.environ["CCACHE_COMPILER_TYPE"] = "gcc"

# Also wrap SCons-level compilers for any non-CMake compilation
env.Replace(
    CC='"ccache" "${CC}"',
    CXX='"ccache" "${CXX}"',
    AS='"ccache" "${AS}"',
)
