FROM debian:trixie-slim

ARG CMAKE_VERSION=4.4.3
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=1000

ENV DEBIAN_FRONTEND=noninteractive

# Core toolchain and C++ dev tools.
#
# - build-essential: gcc/g++ (-> gcc-13/g++-13 on trixie, the same major
#   version this project has been built against all along -- GCC's
#   std::format support only became usable starting GCC 13, so this
#   matters, not just a version nicety).
# - clang/clang-tidy/clangd/clang-format/clang-tools: LLVM 19 by default
#   on trixie, no extra apt.llvm.org repo needed.
# - libclang-rt-19-dev: compiler-rt sanitizer runtimes for clang
#   (libclang_rt.asan/tsan/ubsan...), required for native clang
#   sanitizer linking in the *-linux sanitizer presets.
# - gdb, lldb: debuggers for GCC- and Clang-built binaries respectively.
# - valgrind: memory/threading error detection (Linux-only -- this is a
#   large part of why a Linux devcontainer is worth having alongside the
#   native Windows/MinGW build, which also can't link ASan/UBSan/TSan at
#   all; see CMakePresets.json's *-linux presets).
# - ninja-build: faster parallel builds than make; used by the *-linux
#   CMake presets.
# - ccache: speeds up repeated builds by caching object files.
# - cppcheck: a second, differently-focused static analyzer alongside
#   clang-tidy.
# - doxygen, graphviz: this codebase is already written with Doxygen-style
#   comments throughout (@file/@brief/@class); this lets you actually
#   generate the docs, graphviz for the call/inheritance graphs.
# - git: required at configure time -- CMakeLists.txt FetchContents spdlog
#   and googletest straight from GitHub.
# - pkg-config: commonly needed by CMake's find_package().
# - python3: general scripting utility.
# - curl, ca-certificates: needed below to fetch CMake's official release.
# - sudo: for the non-root dev user created below.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gdb \
    valgrind \
    clang \
    clang-tidy \
    clangd \
    clang-format \
    clang-tools \
    libclang-rt-19-dev \
    lldb \
    ninja-build \
    ccache \
    cppcheck \
    doxygen \
    graphviz \
    git \
    pkg-config \
    python3 \
    curl \
    ca-certificates \
    sudo \
    less \
    libspdlog-dev \
    libgtest-dev \
    libgmock-dev \
    && rm -rf /var/lib/apt/lists/*

ENV TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1:history_size=7"
# CMake: Debian trixie only ships 3.31, but this project requires >= 4.1
# (see cmake_minimum_required() in CMakeLists.txt), and Kitware's APT
# repository (apt.kitware.com) only supports Ubuntu, not Debian -- install
# the official prebuilt release directly instead.
RUN ARCH="$(dpkg --print-architecture)" && \
    case "$ARCH" in \
    amd64) CMAKE_ARCH="x86_64" ;; \
    arm64) CMAKE_ARCH="aarch64" ;; \
    *) echo "Unsupported architecture for CMake install: $ARCH" >&2; exit 1 ;; \
    esac && \
    curl -fsSL "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz" \
    -o /tmp/cmake.tar.gz && \
    tar -xzf /tmp/cmake.tar.gz -C /usr/local --strip-components=1 && \
    rm /tmp/cmake.tar.gz

# Non-root user matching VS Code's devcontainer convention (and avoiding
# root-owned files being written into the bind-mounted workspace on the
# host). Passwordless sudo for local dev convenience.
RUN groupadd --gid ${USER_GID} ${USERNAME} \
    && useradd --uid ${USER_UID} --gid ${USER_GID} -m -s /bin/bash ${USERNAME} \
    && echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
    && chmod 0440 /etc/sudoers.d/${USERNAME}

RUN echo "alias ll='ls -alhF --color=auto'" >> /etc/bash.bashrc \
    && echo "alias lt='ls -alorth --color=auto'" >> /etc/bash.bashrc 
USER ${USERNAME}
