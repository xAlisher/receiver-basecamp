// #84/#51/#10/#2 Headless regression for reapOrphans() — the startup cleanup of leaked tor/ffplay.
//
// Mirrors the backend's Linux branch: ORPHAN = parent is not a live `ui-host` (covers PPid==1 AND the
// systemd --user subreaper case), then a marker + argv0 guard. It stands up REAL processes:
//   - an orphaned `tor` and `ffplay` (double-fork → parent dies → reparented to init/systemd) with our markers,
//   - a "ui-host" parent stub that forks a matching `ffplay` child → the LIVE-session control (must be SPARED),
//   - a bogus proc whose argv0 is NOT tor/ffplay but carries a marker (must be SPARED — the pkill -f footgun).
// Asserts: orphans matched, control spared (proves the multi-instance guard), bogus spared.
//
// Keep in sync with the #if defined(__linux__) branch of ReceiverUiBackend::reapOrphans().
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>

static const char* TOR_MARKER = "receiver_ui/torlisten-";
static const char* FF_MARKER  = "cookieCheck=1";

static int ppidOf(int pid) {
    std::ifstream st("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    while (std::getline(st, line))
        if (line.rfind("PPid:", 0) == 0) return std::atoi(line.c_str() + 5);
    return -1;
}
static std::string commOf(int pid) {
    std::ifstream c("/proc/" + std::to_string(pid) + "/comm");
    std::string s((std::istreambuf_iterator<char>(c)), {}); return s;
}
static std::string cmdlineOf(int pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), {});
}
static bool isOrphan(int ppid) {                       // mirror: parent is not a live ui-host
    if (ppid <= 1) return true;
    const std::string c = commOf(ppid);
    if (c.empty()) return true;
    return c.find("ui-host") == std::string::npos;
}
static bool isOrphanTarget(int pid) {
    if (!isOrphan(ppidOf(pid))) return false;
    const std::string cmd = cmdlineOf(pid);
    if (cmd.empty()) return false;
    const std::string argv0 = cmd.substr(0, cmd.find('\0'));
    auto ends = [&](const char* s){ std::string suf = std::string("/") + s;
        return argv0 == s || (argv0.size() >= suf.size() && argv0.compare(argv0.size()-suf.size(), suf.size(), suf) == 0); };
    const bool tor = cmd.find(TOR_MARKER) != std::string::npos && ends("tor");
    const bool ff  = cmd.find(FF_MARKER)  != std::string::npos && ends("ffplay");
    return tor || ff;
}
// double-fork → grandchild reparents away from us; exec `path argv…`. returns grandchild pid.
static int spawnOrphan(const std::string& path, std::vector<std::string> args) {
    int pfd[2]; if (pipe(pfd)) return -1;
    pid_t mid = fork();
    if (mid == 0) {
        close(pfd[0]);
        pid_t gc = fork();
        if (gc == 0) {
            std::vector<char*> a; a.push_back(const_cast<char*>(path.c_str()));
            for (auto& s : args) a.push_back(const_cast<char*>(s.c_str()));
            a.push_back(nullptr); execv(path.c_str(), a.data()); _exit(127);
        }
        write(pfd[1], &gc, sizeof(gc)); close(pfd[1]); _exit(0);
    }
    close(pfd[1]); int gc = -1; read(pfd[0], &gc, sizeof(gc)); close(pfd[0]); waitpid(mid, nullptr, 0);
    return gc;
}
static int fail(const char* m){ std::fprintf(stderr, "FAIL: %s\n", m); return 1; }

int main() {
    char tmpl[] = "/tmp/reap-test-XXXXXX";
    const char* dir = mkdtemp(tmpl); if (!dir) return fail("mkdtemp");
    const std::string D = dir;
    // sleeper stub (ignores argv) → named tor / ffplay / sh; and a "ui-host" stub that forks a marked ffplay child.
    { std::ofstream(D+"/sleeper.c") << "#include <unistd.h>\nint main(int c,char**v){(void)c;(void)v;sleep(120);return 0;}\n"; }
    { std::ofstream(D+"/uihost.c")  << "#include <unistd.h>\nint main(int c,char**v){(void)c;if(fork()==0){execl(v[1],\"ffplay\",\"-cookies\",\"cookieCheck=1\",\"120\",(char*)0);_exit(127);}sleep(120);return 0;}\n"; }
    for (const char* n : {"tor","ffplay","sh"})
        if (std::system(("cc -O0 -o '"+D+"/"+n+"' '"+D+"/sleeper.c'").c_str())) return fail("compile sleeper");
    if (std::system(("cc -O0 -o '"+D+"/ui-host' '"+D+"/uihost.c'").c_str())) return fail("compile uihost");

    int torPid = spawnOrphan(D+"/tor",    {"-f", D+"/receiver_ui/torlisten-abc/torrc", "120"});
    int ffPid  = spawnOrphan(D+"/ffplay", {"-cookies", "cookieCheck=1; path=/", "120"});
    // bogus: argv0 is `sh` (not tor/ffplay) but the marker is in its args → must be spared
    int bogus  = spawnOrphan(D+"/sh",     {"-c", "receiver_ui/torlisten-x cookieCheck=1", "120"});
    // control: a live "ui-host" parent forks a marked ffplay child → child's parent comm == ui-host → spared
    pid_t uihost = fork();
    if (uihost == 0) { execl((D+"/ui-host").c_str(), "ui-host", (D+"/ffplay").c_str(), (char*)nullptr); _exit(127); }

    // wait for orphans to reparent + the control child to appear
    for (int i = 0; i < 60; ++i) { if (ppidOf(torPid)!=-1 && isOrphan(ppidOf(torPid)) && isOrphan(ppidOf(ffPid))) break; usleep(100000); }
    usleep(300000);
    // find the control ffplay child (parent == uihost) by scanning real /proc entries (pids can be millions)
    int control = -1;
    if (DIR* pd = opendir("/proc")) {
        for (dirent* e; (e = readdir(pd)) && control < 0; ) {
            const int p = std::atoi(e->d_name);
            if (p > 1 && ppidOf(p) == uihost && cmdlineOf(p).find(FF_MARKER) != std::string::npos) control = p;
        }
        closedir(pd);
    }

    int rc = 0;
    if (!isOrphanTarget(torPid)) rc = fail("orphaned tor not matched"); else std::printf("ok: orphaned tor matched (%d)\n", torPid);
    if (!isOrphanTarget(ffPid))  rc = fail("orphaned ffplay not matched"); else std::printf("ok: orphaned ffplay matched (%d)\n", ffPid);
    if (isOrphanTarget(bogus))   rc = fail("bogus (argv0=sh) matched — argv0 guard broken"); else std::printf("ok: bogus argv0 spared (%d)\n", bogus);
    if (control < 0)             rc = fail("control child not found");
    else if (isOrphanTarget(control)) rc = fail("LIVE-session control matched — ui-host parent guard broken");
    else std::printf("ok: live-session control spared (%d, parent comm='%s')\n", control, commOf(uihost).c_str());

    for (int p : {torPid, ffPid, bogus, (int)uihost, control}) if (p > 0) kill(p, SIGKILL);
    std::system(("rm -rf '"+D+"'").c_str());
    if (rc == 0) std::printf("\nPASS: orphan reap matches leaked tor+ffplay, spares live-session + non-tor/ffplay procs\n");
    return rc;
}
