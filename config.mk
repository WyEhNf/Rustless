# RxOJ C/C++ submission: all compiler dependencies are stored in this repository.
# Keep the judge build separate from existing local CMake caches.
BUILD = cmake -S . -B build/oj -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DRX_JUDGE_BUILD=ON \
    && cmake --build build/oj --target rxc -j2 && : > {runtime}

# Required for semantic tests: exit 0 to accept {source}, 1 to reject it.
SEMANTIC = ./build/oj/rxc --stage semantic {source}

IR = ./build/oj/rxc --stage ir {source} -o {output}
CODEGEN =
RUN = xmake run -P vendor/REIMU reimu -f {output},{runtime} -o {stdout} -p {profile} 1>&2
