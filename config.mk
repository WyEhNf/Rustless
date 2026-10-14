# RxOJ C/C++ submission: all compiler dependencies are stored in this repository.
# Keep the judge build separate from existing local CMake caches.
BUILD = cmake -S . -B build/oj -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build build/oj --target rxc -j2 && : > {runtime}

# Required for semantic tests: exit 0 to accept {source}, 1 to reject it.
SEMANTIC = ./build/oj/rxc --stage semantic {source}

# These stages have no compiler CLI entry yet. Leave them unset so the runner
# reports unsupported stages instead of invoking the Rust reference compiler.
IR =
CODEGEN =
RUN =
