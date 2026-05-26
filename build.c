#include "build.h"

void setup_nodejs_targets() {
    printf("\n<-- [Installing NodeJS headers] -->\n");
    run("mkdir \"targets\"");
    START_FOREACH_NODEJS(i);
      const char* version = versions[i].name;
      const int hasHeader = run("mkdir \"targets/node-%s\"", version);
      run("mkdir \"targets/node-%s/" PER_TARGET_ARTIFACTS_FOLDER "\"", version);
      if(hasHeader) { printf("  [NodeJS %s is already installed]\n", version); goto before_end; }

      run("curl -sSL \"https://nodejs.org/dist/%s/node-%s-headers.tar.gz\" -o targets/node-%s-headers.tar.gz"
        " && "
        "tar xzf targets/node-%s-headers.tar.gz -C targets"
        " && "
        "rm targets/node-%s-headers.tar.gz",
        version, version, version, version, version);
      run(
        /*fetch v8 fast-api manually*/
        "curl -sSfL"
        " \"https://raw.githubusercontent.com/nodejs/node/%s/deps/v8/include/v8-fast-api-calls.h\""
        " -o \"targets/node-%s/include/node/v8-fast-api-calls.h\"",
        version, version);

      #ifdef IS_WINDOWS /* fetch node.lib */
        run("curl -sSL \"https://nodejs.org/dist/%s/win-x64/node.lib\""
            " -o \"targets/node-%s/node.lib\"",
            version, version);
      #endif
    before_end:
    END_FOREACH_NODEJS;

    printf("[Fetched NodeJS headers v22,v24,v26]\n");
}

void build_lsquic() {
  printf("\n<-- [Started building lsquic] -->\n");
    
#if defined(IS_LINUX)
#define MACRO ""
#elif defined (IS_MACOS)
#if defined(CROSS_COMPILE_MACOS)
#define MACRO "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DCMAKE_OSX_ARCHITECTURES=x86_64"  
#else
#define MACRO "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DCMAKE_OSX_ARCHITECTURES=arm64"
#endif

#elif defined(IS_WINDOWS)
    /* lsquic on Windows is not configured to use crlf for generating scripts*/
    run("dos2unix uWebSockets\\uSockets\\lsquic\\include\\lsquic.h");
    run("curl -sSOL \"https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz\""
        " && tar xzf zlib-1.3.1.tar.gz && rm zlib-1.3.1.tar.gz");
    /* /Wv:18 and /wd4201 suppress warnings */
#define MACRO " -DCMAKE_C_FLAGS=\"/Wv:18 /DWIN32 /wd4201\""
#endif
  
    run("cd uWebSockets/uSockets/lsquic &&"
      " cmake -G Ninja . " MACRO
      " -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
      " -DBORINGSSL_DIR=../boringssl"
      " -DZLIB_INCLUDE_DIR=../../../zlib-1.3.1"
      " -DCMAKE_POSITION_INDEPENDENT_CODE=ON"
      " -DCMAKE_BUILD_TYPE=Release"
      " -DLSQUIC_BIN=Off "
      " && ninja -j%i lsquic", threads_quantity);


#undef MACRO
  printf("\n[Finished building lsquic]\n");
}

void build_boringssl() {
  printf("\n<-- [Started building boringssl] -->\n");

#if defined(IS_MACOS)

#if defined(CROSS_COMPILE_MACOS)
#define MACRO " -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 "
#else 
#define MACRO " -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 "
#endif

#elif defined(IS_WINDOWS)
#define MACRO " -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "
#else
#define MACRO " "
#endif

  run("cd uWebSockets/uSockets/boringssl &&"
      " cmake -G Ninja . " MACRO 
      " -DCMAKE_BUILD_TYPE=Release"
      " -DCMAKE_POSITION_INDEPENDENT_CODE=ON"
      " -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
      " && ninja -j%i crypto ssl", 
      threads_quantity);
  printf("\n[Finished building boringssl: %s]\n", ARCH);

#undef MACRO
}

void build_uSockets() {

#define SHARED_MACRO \
  " -DUWS_WITH_PROXY" \
  " -DLIBUS_USE_QUIC" \
  " -DLIBUS_USE_LIBUV" \
  " -DLIBUS_USE_OPENSSL " \
  " -DWIN32_LEAN_AND_MEAN" \
  " -D_CRT_SECURE_NO_WARNINGS" \
  " -Wno-deprecated-declarations" \
  " -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded" \
  " -DUWS_REMOTE_ADDRESS_USERSPACE" 

#define SHARED_INCLUDE(CWD, node_version) \
  " -I " CWD "uWebSockets/src" \
  " -I " CWD "uWebSockets/uSockets/src" \
  " -I " CWD "uWebSockets/uSockets/lsquic/include/" \
  " -I " CWD "uWebSockets/uSockets/lsquic/wincompat" \
  " -I " CWD "uWebSockets/uSockets/boringssl/include/" \
  " -I " CWD "targets/node-" node_version "/include/node "

#if !defined(IS_WINDOWS)
#define UNIX_MACRO " -pthread -fPIC "
#else
#define UNIX_MACRO ""
#endif

  START_FOREACH_NODEJS(i);
  run("cd targets/node-%s/" PER_TARGET_ARTIFACTS_FOLDER " && " C_COMPILER SHARED_MACRO UNIX_MACRO OPT_FLAGS SHARED_INCLUDE("../../../", "%s")
      " -c ../../../uWebSockets/uSockets/src/*.c "
      " ../../../uWebSockets/uSockets/src/eventing/*.c "
      " ../../../uWebSockets/uSockets/src/crypto/*.c",

      versions[i].name, versions[i].name);
  END_FOREACH_NODEJS;

#undef SHARED_MACRO
#undef SHARED_INCLUDE
#undef UNIX_MACRO
}

void build(char *special_options) {
  printf("\n<-- [Started building uWebSockets.js] -->\n");

  
  START_FOREACH_NODEJS(i);
    const char* version = versions[i].name;
    const char* abi = versions[i].abi;
    run(CXX_COMPILER OPT_FLAGS
        " -DUWS_WITH_PROXY" 
        " -DLIBUS_USE_QUIC" 
        " -DLIBUS_USE_LIBUV" 
        " -DLIBUS_USE_OPENSSL" 
        " -DWIN32_LEAN_AND_MEAN" 
        " -DUWS_REMOTE_ADDRESS_USERSPACE"

        " -I uWebSockets/src" 
        " -I uWebSockets/uSockets/src" 
        " -I uWebSockets/uSockets/lsquic/include" 
        " -I uWebSockets/uSockets/lsquic/wincompat" 
        " -I uWebSockets/uSockets/boringssl/include" 
        " -I targets/node-%s/include/node"

        " -std=c++20 -Wno-deprecated-declarations" 
        STATIC_LIB("uWebSockets/uSockets/boringssl", "ssl")
        STATIC_LIB("uWebSockets/uSockets/boringssl", "crypto")
        STATIC_LIB("uWebSockets/uSockets/lsquic/src/liblsquic", "lsquic")

#if defined(IS_WINDOWS)
        STATIC_LIB("targets/node-%s", "node")
#endif

        " -shared %s"
        " ./targets/node-%s/" PER_TARGET_ARTIFACTS_FOLDER "/*.o src/addon.cpp uWebSockets/uSockets/src/crypto/sni_tree.cpp"
        " -o dist/uws_%s_%s_%s.node",

        version,
#if defined(IS_WINDOWS) /* for node.lib */
        version, 
#endif
        special_options, version, OS, ARCH, abi);

  END_FOREACH_NODEJS;

  printf("\n[Finished building uWebSockets.js]\n");

}

int main(int argc, const char* argv[]) {
    /* see console output IMMEDIATELY for debugging purposes */
    setbuf(stdout, 0);
    signal(SIGINT, SIGINTHandler);

    threads_quantity = get_cpu_count();
    printf("<-- ENTRY POINT!!! -->\n[Parallel threads available: %i]\n", threads_quantity);

  if(argc == 1 || argc > 1 && !strcmp(argv[1], "deps")) {
    printf("<--[Fetching + Compiling dependencies]-->\n");
    setup_nodejs_targets();
    build_boringssl();
    build_lsquic();
    build_uSockets();
    printf("\n[Finished fetching + compiling dependencies]\n");
    if (argc > 1) return 0;
  }

#ifdef IS_WINDOWS
    build("-ladvapi32 -fuse-ld=lld");
#elif defined(IS_MACOS)
    /* for MacOS we compile one architecture at a time */
    build("-pthread -fPIC -undefined dynamic_lookup" MACOS_LINK_EXTRAS);
    /* -undefined dynamic_lookup lets library be linked dynamically */
#else
    build("-pthread -fPIC" LINUX_LINK_EXTRAS);
#endif
}
