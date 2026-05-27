#include "topo/Check/CapabilityCatalog.h"
#include <unordered_map>

namespace topo::check {

namespace {

const std::unordered_map<std::string, CapabilityKind>& importCatalog() {
    static const std::unordered_map<std::string, CapabilityKind> catalog = {
        // ── C/C++ ──────────────────────────────────────────────
        // File I/O
        {"fstream", CapabilityKind::File},
        {"cstdio", CapabilityKind::File},
        {"filesystem", CapabilityKind::File},
        {"iostream", CapabilityKind::File},
        {"ostream", CapabilityKind::File},
        {"istream", CapabilityKind::File},
        {"sstream", CapabilityKind::File},
        {"print", CapabilityKind::File},
        {"fcntl.h", CapabilityKind::File},
        {"sys/stat.h", CapabilityKind::File},
        {"stdio.h", CapabilityKind::File},
        {"unistd.h", CapabilityKind::File},  // read/write/fork/exec overlap — File is primary
        {"io.h", CapabilityKind::File},       // Windows
        {"sys/mman.h", CapabilityKind::File},
        {"sys/sendfile.h", CapabilityKind::File},
        {"aio.h", CapabilityKind::File},
        // Network
        {"sys/socket.h", CapabilityKind::Network},
        {"netinet/in.h", CapabilityKind::Network},
        {"netinet/tcp.h", CapabilityKind::Network},
        {"arpa/inet.h", CapabilityKind::Network},
        {"netdb.h", CapabilityKind::Network},
        {"winsock2.h", CapabilityKind::Network},    // Windows
        {"ws2tcpip.h", CapabilityKind::Network},    // Windows
        {"sys/epoll.h", CapabilityKind::Network},
        {"poll.h", CapabilityKind::Network},
        {"net/if.h", CapabilityKind::Network},
        // Process / Thread
        {"cstdlib", CapabilityKind::Process},  // system()
        {"spawn.h", CapabilityKind::Process},
        {"sys/wait.h", CapabilityKind::Process},
        {"windows.h", CapabilityKind::Process},     // Windows
        {"process.h", CapabilityKind::Process},      // Windows
        {"sys/ptrace.h", CapabilityKind::Process},
        {"signal.h", CapabilityKind::Process},
        {"thread", CapabilityKind::Process},
        {"pthread.h", CapabilityKind::Process},
        {"csetjmp", CapabilityKind::Process},
        // Memory / C string operations
        {"cstring", CapabilityKind::Memory},
        {"string.h", CapabilityKind::Memory},
        {"memory.h", CapabilityKind::Memory},
        {"strings.h", CapabilityKind::Memory},
        // Dynamic loading
        {"dlfcn.h", CapabilityKind::DynamicLoad},
        {"ltdl.h", CapabilityKind::DynamicLoad},
        {"libloaderapi.h", CapabilityKind::DynamicLoad},  // Windows

        // ── Java (exact matches for specific classes) ──────────
        {"java.lang.ProcessBuilder", CapabilityKind::Process},
        {"java.lang.Runtime", CapabilityKind::Process},
        {"java.lang.ClassLoader", CapabilityKind::DynamicLoad},

        // ── Python (bare module names) ─────────────────────────
        // File I/O
        {"os", CapabilityKind::File},
        {"io", CapabilityKind::File},
        {"pathlib", CapabilityKind::File},
        {"shutil", CapabilityKind::File},
        {"pickle", CapabilityKind::File},
        {"tempfile", CapabilityKind::File},
        {"glob", CapabilityKind::File},
        {"fnmatch", CapabilityKind::File},
        // Network
        {"socket", CapabilityKind::Network},
        {"http", CapabilityKind::Network},
        {"urllib", CapabilityKind::Network},
        {"requests", CapabilityKind::Network},
        {"ftplib", CapabilityKind::Network},
        {"smtplib", CapabilityKind::Network},
        {"ssl", CapabilityKind::Network},
        {"xmlrpc", CapabilityKind::Network},
        // Process
        {"subprocess", CapabilityKind::Process},
        {"multiprocessing", CapabilityKind::Process},
        // Dynamic loading
        {"importlib", CapabilityKind::DynamicLoad},
        {"ctypes", CapabilityKind::DynamicLoad},

        // ── Rust (crate/module paths) ──────────────────────────
        // File I/O
        {"std::fs", CapabilityKind::File},
        {"std::io", CapabilityKind::File},
        // Network
        {"std::net", CapabilityKind::Network},
        {"hyper", CapabilityKind::Network},
        // Process
        {"std::process", CapabilityKind::Process},
        // Dynamic loading
        {"libloading", CapabilityKind::DynamicLoad},
        {"dlopen", CapabilityKind::DynamicLoad},
        // Async runtime crates (provide file/network/process capabilities)
        {"tokio", CapabilityKind::File},        // primary capability; sub-modules refined below
        {"tokio::fs", CapabilityKind::File},
        {"tokio::net", CapabilityKind::Network},
        {"tokio::process", CapabilityKind::Process},
        {"async-std", CapabilityKind::File},
        {"smol", CapabilityKind::File},
    };
    return catalog;
}

const std::unordered_map<std::string, CapabilityKind>& apiCatalog() {
    static const std::unordered_map<std::string, CapabilityKind> catalog = {
        // ── C/C++ ──────────────────────────────────────────────
        // File I/O
        {"fopen", CapabilityKind::File},
        {"fclose", CapabilityKind::File},
        {"fread", CapabilityKind::File},
        {"fwrite", CapabilityKind::File},
        {"open", CapabilityKind::File},
        {"close", CapabilityKind::File},
        {"read", CapabilityKind::File},
        {"write", CapabilityKind::File},
        {"mmap", CapabilityKind::File},
        {"munmap", CapabilityKind::File},
        {"freopen", CapabilityKind::File},
        {"tmpfile", CapabilityKind::File},
        {"mkstemp", CapabilityKind::File},
        {"sendfile", CapabilityKind::File},
        {"aio_read", CapabilityKind::File},
        {"aio_write", CapabilityKind::File},
        // Stdout / formatted output (write to file descriptors)
        {"printf", CapabilityKind::File},
        {"fprintf", CapabilityKind::File},
        {"puts", CapabilityKind::File},
        {"fputs", CapabilityKind::File},
        {"sprintf", CapabilityKind::File},
        {"snprintf", CapabilityKind::File},
        {"vprintf", CapabilityKind::File},
        {"vfprintf", CapabilityKind::File},
        {"vsnprintf", CapabilityKind::File},
        {"putchar", CapabilityKind::File},
        {"putc", CapabilityKind::File},
        {"fputc", CapabilityKind::File},
        {"perror", CapabilityKind::File},
        // Network
        {"socket", CapabilityKind::Network},
        {"connect", CapabilityKind::Network},
        {"bind", CapabilityKind::Network},
        {"listen", CapabilityKind::Network},
        {"accept", CapabilityKind::Network},
        {"send", CapabilityKind::Network},
        {"recv", CapabilityKind::Network},
        {"sendto", CapabilityKind::Network},
        {"recvfrom", CapabilityKind::Network},
        {"getaddrinfo", CapabilityKind::Network},
        {"gethostbyname", CapabilityKind::Network},
        {"epoll_create", CapabilityKind::Network},
        {"epoll_ctl", CapabilityKind::Network},
        {"epoll_wait", CapabilityKind::Network},
        {"poll", CapabilityKind::Network},
        {"select", CapabilityKind::Network},
        // Process
        {"system", CapabilityKind::Process},
        {"fork", CapabilityKind::Process},
        {"popen", CapabilityKind::Process},
        {"posix_spawn", CapabilityKind::Process},
        {"execve", CapabilityKind::Process},
        {"execvp", CapabilityKind::Process},
        {"waitpid", CapabilityKind::Process},
        {"kill", CapabilityKind::Process},
        {"signal", CapabilityKind::Process},
        {"ptrace", CapabilityKind::Process},
        {"CreateProcess", CapabilityKind::Process},   // Windows
        {"_spawnl", CapabilityKind::Process},          // Windows
        // Dynamic loading
        {"dlopen", CapabilityKind::DynamicLoad},
        {"dlsym", CapabilityKind::DynamicLoad},
        {"dlclose", CapabilityKind::DynamicLoad},
        {"LoadLibrary", CapabilityKind::DynamicLoad},    // Windows
        {"GetProcAddress", CapabilityKind::DynamicLoad}, // Windows
        // C/C++ File I/O (supplemental)
        {"lseek", CapabilityKind::File},
        {"pread", CapabilityKind::File},
        {"pwrite", CapabilityKind::File},
        {"dup", CapabilityKind::File},
        {"dup2", CapabilityKind::File},
        {"unlink", CapabilityKind::File},
        {"remove", CapabilityKind::File},
        {"rename", CapabilityKind::File},
        {"chmod", CapabilityKind::File},
        {"truncate", CapabilityKind::File},
        // C/C++ Network (supplemental)
        {"sendmsg", CapabilityKind::Network},
        {"recvmsg", CapabilityKind::Network},
        {"socketpair", CapabilityKind::Network},
        // C/C++ Process / Thread (supplemental)
        {"pipe", CapabilityKind::Process},
        {"mkfifo", CapabilityKind::Process},
        {"wait", CapabilityKind::Process},
        {"pthread_create", CapabilityKind::Process},
        {"thrd_create", CapabilityKind::Process},
        // C/C++ Memory — manual allocation
        {"malloc", CapabilityKind::Memory},
        {"calloc", CapabilityKind::Memory},
        {"realloc", CapabilityKind::Memory},
        {"reallocarray", CapabilityKind::Memory},
        {"free", CapabilityKind::Memory},
        {"aligned_alloc", CapabilityKind::Memory},
        {"posix_memalign", CapabilityKind::Memory},
        {"valloc", CapabilityKind::Memory},
        {"pvalloc", CapabilityKind::Memory},
        {"memalign", CapabilityKind::Memory},
        // C/C++ Memory — untyped buffer operations
        {"memcpy", CapabilityKind::Memory},
        {"memmove", CapabilityKind::Memory},
        {"memset", CapabilityKind::Memory},
        {"memchr", CapabilityKind::Memory},
        {"memcmp", CapabilityKind::Memory},
        {"bcopy", CapabilityKind::Memory},
        {"bzero", CapabilityKind::Memory},
        {"bcmp", CapabilityKind::Memory},
        // C/C++ Memory — unbounded string operations (overflow prone)
        {"strcpy", CapabilityKind::Memory},
        {"strncpy", CapabilityKind::Memory},
        {"strcat", CapabilityKind::Memory},
        {"strncat", CapabilityKind::Memory},
        {"strdup", CapabilityKind::Memory},
        {"strndup", CapabilityKind::Memory},
        // C++20 manual lifetime construction
        {"construct_at", CapabilityKind::Memory},
        {"destroy_at", CapabilityKind::Memory},
        {"start_lifetime_as", CapabilityKind::Memory},
        {"start_lifetime_as_array", CapabilityKind::Memory},

        // ── Java ───────────────────────────────────────────────
        // File I/O
        {"FileInputStream", CapabilityKind::File},
        {"FileOutputStream", CapabilityKind::File},
        {"FileReader", CapabilityKind::File},
        {"FileWriter", CapabilityKind::File},
        {"RandomAccessFile", CapabilityKind::File},
        {"Files.read", CapabilityKind::File},
        {"Files.write", CapabilityKind::File},
        {"Files.copy", CapabilityKind::File},
        {"Files.move", CapabilityKind::File},
        {"Files.delete", CapabilityKind::File},
        {"Files.createDirectory", CapabilityKind::File},
        {"Files.createDirectories", CapabilityKind::File},
        {"Files.newBufferedWriter", CapabilityKind::File},
        {"Files.newBufferedReader", CapabilityKind::File},
        {"Files.newInputStream", CapabilityKind::File},
        {"Files.newOutputStream", CapabilityKind::File},
        // Console I/O (stdout/stderr cross process boundary)
        {"System.out.println", CapabilityKind::File},
        {"System.out.print", CapabilityKind::File},
        {"System.out.printf", CapabilityKind::File},
        {"System.err.println", CapabilityKind::File},
        {"System.err.print", CapabilityKind::File},
        {"PrintStream.println", CapabilityKind::File},
        {"PrintStream.print", CapabilityKind::File},
        {"PrintStream.printf", CapabilityKind::File},
        {"PrintWriter.println", CapabilityKind::File},
        {"PrintWriter.print", CapabilityKind::File},
        {"PrintWriter.printf", CapabilityKind::File},
        // Network
        {"Socket", CapabilityKind::Network},
        {"ServerSocket", CapabilityKind::Network},
        {"DatagramSocket", CapabilityKind::Network},
        {"URLConnection", CapabilityKind::Network},
        {"HttpURLConnection", CapabilityKind::Network},
        // Process
        {"ProcessBuilder", CapabilityKind::Process},
        {"Runtime.exec", CapabilityKind::Process},
        // Dynamic loading
        {"Class.forName", CapabilityKind::DynamicLoad},
        {"ClassLoader.loadClass", CapabilityKind::DynamicLoad},

        // ── Python ─────────────────────────────────────────────
        // File I/O  ("open" already covered by C)
        {"os.open", CapabilityKind::File},
        {"os.read", CapabilityKind::File},
        {"os.write", CapabilityKind::File},
        {"pathlib.Path", CapabilityKind::File},
        // Network
        {"socket.socket", CapabilityKind::Network},
        {"urlopen", CapabilityKind::Network},
        {"requests.get", CapabilityKind::Network},
        {"requests.post", CapabilityKind::Network},
        // Process
        {"subprocess.run", CapabilityKind::Process},
        {"subprocess.Popen", CapabilityKind::Process},
        {"subprocess.call", CapabilityKind::Process},
        {"os.system", CapabilityKind::Process},
        {"os.popen", CapabilityKind::Process},
        {"os.fork", CapabilityKind::Process},
        {"os.exec", CapabilityKind::Process},
        {"os.execvp", CapabilityKind::Process},
        // Dynamic loading
        {"importlib.import_module", CapabilityKind::DynamicLoad},
        {"ctypes.cdll", CapabilityKind::DynamicLoad},
        {"ctypes.CDLL", CapabilityKind::DynamicLoad},

        // ── Rust ───────────────────────────────────────────────
        // File I/O
        {"File::open", CapabilityKind::File},
        {"File::create", CapabilityKind::File},
        {"fs::read", CapabilityKind::File},
        {"fs::write", CapabilityKind::File},
        {"fs::read_to_string", CapabilityKind::File},
        // stdout/stderr output macros (process boundary I/O)
        {"println", CapabilityKind::File},
        {"eprintln", CapabilityKind::File},
        {"print", CapabilityKind::File},
        {"eprint", CapabilityKind::File},
        {"write!", CapabilityKind::File},
        {"writeln!", CapabilityKind::File},
        // Buffered I/O wrappers
        {"BufWriter::new", CapabilityKind::File},
        {"BufReader::new", CapabilityKind::File},
        // tokio async filesystem operations
        {"tokio::fs::read", CapabilityKind::File},
        {"tokio::fs::write", CapabilityKind::File},
        {"tokio::fs::remove_file", CapabilityKind::File},
        {"tokio::fs::create_dir", CapabilityKind::File},
        {"tokio::fs::copy", CapabilityKind::File},
        {"tokio::fs::rename", CapabilityKind::File},
        {"tokio::fs::read_to_string", CapabilityKind::File},
        // Network
        {"TcpListener::bind", CapabilityKind::Network},
        {"TcpStream::connect", CapabilityKind::Network},
        {"UdpSocket::bind", CapabilityKind::Network},
        // tokio async network operations
        {"tokio::net::TcpListener::bind", CapabilityKind::Network},
        {"tokio::net::TcpStream::connect", CapabilityKind::Network},
        {"tokio::net::UdpSocket::bind", CapabilityKind::Network},
        // Process
        {"Command::new", CapabilityKind::Process},
        {"Command::spawn", CapabilityKind::Process},
        // Dynamic loading
        {"Library::new", CapabilityKind::DynamicLoad},
    };
    return catalog;
}

} // anonymous namespace

std::optional<CapabilityKind> classifyImport(const std::string& path) {
    // Direct match
    auto it = importCatalog().find(path);
    if (it != importCatalog().end()) return it->second;

    // ── C/C++ prefix matches ───────────────────────────────
    if (path.find("netinet/") == 0 || path.find("arpa/") == 0)
        return CapabilityKind::Network;
    if (path.find("curl/") == 0 || path.find("boost/asio") == 0)
        return CapabilityKind::Network;

    // ── Java dot-separated package prefixes ────────────────
    if (path.find("java.io.") == 0 || path.find("java.nio.") == 0 ||
        path == "java.io" || path == "java.nio")
        return CapabilityKind::File;
    if (path.find("java.net.") == 0 || path.find("javax.net.") == 0 ||
        path == "java.net" || path == "javax.net")
        return CapabilityKind::Network;
    if (path.find("java.lang.reflect.") == 0 || path == "java.lang.reflect")
        return CapabilityKind::DynamicLoad;

    // ── Rust colon-separated module prefixes ───────────────
    if (path.find("tokio::fs") == 0)
        return CapabilityKind::File;
    if (path.find("tokio::net") == 0)
        return CapabilityKind::Network;
    if (path.find("tokio::process") == 0)
        return CapabilityKind::Process;
    if (path.find("async_std::fs") == 0 || path.find("async_std::io") == 0)
        return CapabilityKind::File;
    if (path.find("async_std::net") == 0)
        return CapabilityKind::Network;
    if (path.find("async_std::process") == 0)
        return CapabilityKind::Process;

    return std::nullopt;
}

std::optional<CapabilityKind> classifyApiCall(const std::string& funcName) {
    // Direct match
    auto it = apiCatalog().find(funcName);
    if (it != apiCatalog().end()) return it->second;

    // exec* family: execl, execv, execvp, execle, etc.
    if (funcName.size() >= 4 && funcName.substr(0, 4) == "exec")
        return CapabilityKind::Process;

    // Stream class constructors
    if (funcName == "std::ifstream" || funcName == "std::ofstream" || funcName == "std::fstream")
        return CapabilityKind::File;

    return std::nullopt;
}

} // namespace topo::check
