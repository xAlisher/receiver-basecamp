// #75 Headless regression for the zero-install binary resolution.
//
// Proves the mechanism the receiver backend uses to prefer self-contained helpers bundled next to
// the plugin (…/plugins/receiver_ui/bin/ffplay) over anything the user installed — WITHOUT dladdr
// (which pulls dladdr@GLIBC_2.34 the AppImage's glibc can't resolve). The backend reads the already-
// mapped plugin's path out of /proc/self/maps; this test dlopen()s a stub named receiver_ui_plugin.so
// so it appears in this process's maps exactly as the real plugin does, then runs the same three steps
// the backend runs — findModuleDir → bundledBin → spawn — against the REAL bundled ffplay.
//
// Plain C++/POSIX (no Qt) so it builds with a bare g++ in the headless harness. The /proc/self/maps
// parse mirrors ReceiverUiBackend::findModuleDir() line-for-line; keep them in sync.
//
// Usage: bundled-bin-resolution.test <path-to-bundle-dir>   (dir holding ffplay + its $ORIGIN libs)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#define PLUGIN_LIB "receiver_ui_plugin.dylib"
#else
#define PLUGIN_LIB "receiver_ui_plugin.so"
#endif

// Mirror of ReceiverUiBackend::findModuleDir(): find the dir of the loaded plugin lib.
static std::string findModuleDir() {
#if defined(__APPLE__)
    // macOS: dyld image list (mirrors the backend's _dyld branch).
    for (uint32_t i = 0, n = _dyld_image_count(); i < n; ++i) {
        const char* name = _dyld_get_image_name(i);
        if (name && (std::strstr(name, "receiver_ui_plugin") || std::strstr(name, "receiver_ui_replica_factory"))) {
            std::string p = name; const size_t d = p.find_last_of('/');
            return d == std::string::npos ? std::string() : p.substr(0, d);
        }
    }
    return std::string();
#else
    // Linux: /proc/self/maps (mirrors the backend's Linux branch).
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("receiver_ui_plugin.so") == std::string::npos &&
            line.find("receiver_ui_replica_factory.so") == std::string::npos) continue;
        const size_t slash = line.find('/');
        if (slash == std::string::npos) continue;
        std::string path = line.substr(slash);
        while (!path.empty() && (path.back() == '\n' || path.back() == ' ')) path.pop_back();
        const size_t d = path.find_last_of('/');
        return d == std::string::npos ? std::string() : path.substr(0, d);   // dirname = module dir
    }
    return std::string();
#endif
}

// Mirror of ReceiverUiBackend::bundledBin(): <moduleDir>/bin/<name> if present + executable, else "".
static std::string bundledBin(const std::string& moduleDir, const std::string& name) {
    if (moduleDir.empty()) return std::string();
    const std::string p = moduleDir + "/bin/" + name;
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return std::string();
    if (!S_ISREG(st.st_mode) || access(p.c_str(), X_OK) != 0) return std::string();
    return p;
}

static int fail(const char* msg) { std::fprintf(stderr, "FAIL: %s\n", msg); return 1; }

int main(int argc, char** argv) {
    if (argc < 2) return fail("usage: bundled-bin-resolution.test <bundle-dir>");
    const std::string bundleDir = argv[1];   // holds ffplay + its $ORIGIN-rpath'd .so libs

    // arrange: a temp module dir holding a stub receiver_ui_plugin.so and bin/ffplay (the real bundle)
    char tmpl[] = "/tmp/rcv-bin-test-XXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) return fail("mkdtemp");
    // canonicalize: on macOS /tmp is a symlink to /private/tmp, and dyld reports the resolved path
    char realbuf[4096];
    const std::string moduleDir = realpath(dir, realbuf) ? std::string(realbuf) : std::string(dir);
    const std::string stubSrc = moduleDir + "/stub.c";
    const std::string stubSo  = moduleDir + "/" PLUGIN_LIB;   // named exactly like the real plugin
    { std::ofstream(stubSrc) << "int _rcv_stub(void){return 0;}\n"; }
#if defined(__APPLE__)
    const std::string shflags = "-dynamiclib";
#else
    const std::string shflags = "-shared -fPIC";
#endif
    if (std::system(("cc " + shflags + " -o '" + stubSo + "' '" + stubSrc + "'").c_str()) != 0)
        return fail("compile stub plugin lib");
    // stage the WHOLE bundle (ffplay + its $ORIGIN libs) under bin/, exactly as it ships
    if (std::system(("mkdir -p '" + moduleDir + "/bin' && cp -a '" + bundleDir + "'/. '" + moduleDir + "/bin/'").c_str()) != 0)
        return fail("stage bundle into bin/");

    // act 1: dlopen the stub so it lands in /proc/self/maps like the real plugin does
    void* h = dlopen(stubSo.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) return fail(dlerror());

    // assert 1: findModuleDir() recovers the plugin's own dir from maps (no dladdr)
    const std::string found = findModuleDir();
    if (found != moduleDir) {
        std::fprintf(stderr, "FAIL: findModuleDir()='%s' expected '%s'\n", found.c_str(), moduleDir.c_str());
        return 1;
    }
    std::printf("ok: findModuleDir -> %s\n", found.c_str());

    // assert 2: bundledBin resolves the bundled ffplay under that dir
    const std::string ff = bundledBin(found, "ffplay");
    if (ff.empty()) return fail("bundledBin(ffplay) empty — bin/ffplay not resolved");
    std::printf("ok: bundledBin(ffplay) -> %s\n", ff.c_str());

    // assert 3: the resolved (bundled, $ORIGIN-rpath'd) ffplay actually runs with a CLEAN env —
    // the same env cleanSpawnEnv() hands spawned children (no LD_LIBRARY_PATH)
    const std::string cmd = "env -i HOME=/tmp '" + ff + "' -version >/dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) return fail("bundled ffplay did not run under a clean env");
    std::printf("ok: bundled ffplay runs under clean env\n");

    // assert 4: a bogus name does NOT resolve (so resolveBin correctly falls through to PATH)
    if (!bundledBin(found, "definitely-not-bundled").empty()) return fail("bundledBin matched a missing binary");
    std::printf("ok: unknown binary falls through (empty)\n");

    dlclose(h);
    std::system(("rm -rf '" + moduleDir + "'").c_str());
    std::printf("\nPASS: zero-install binary resolution mechanism verified\n");
    return 0;
}
