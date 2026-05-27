#include "topo/Check/CapabilityCatalog.h"
#include <gtest/gtest.h>

using namespace topo::check;

TEST(CapabilityCatalog, FileImports) {
    EXPECT_EQ(classifyImport("fstream"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("cstdio"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("filesystem"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("fcntl.h"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("sys/stat.h"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("stdio.h"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("unistd.h"), CapabilityKind::File);
    // Windows + extended POSIX
    EXPECT_EQ(classifyImport("io.h"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("sys/mman.h"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("sys/sendfile.h"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("aio.h"), CapabilityKind::File);
}

TEST(CapabilityCatalog, NetworkImports) {
    EXPECT_EQ(classifyImport("sys/socket.h"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("netinet/in.h"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("netinet/tcp.h"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("arpa/inet.h"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("netdb.h"), CapabilityKind::Network);
    // Windows + extended POSIX
    EXPECT_EQ(classifyImport("winsock2.h"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("ws2tcpip.h"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("sys/epoll.h"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("poll.h"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("net/if.h"), CapabilityKind::Network);
    // Prefix match
    EXPECT_EQ(classifyImport("curl/curl.h"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("boost/asio.hpp"), CapabilityKind::Network);
}

TEST(CapabilityCatalog, ProcessImports) {
    EXPECT_EQ(classifyImport("cstdlib"), CapabilityKind::Process);
    EXPECT_EQ(classifyImport("spawn.h"), CapabilityKind::Process);
    EXPECT_EQ(classifyImport("sys/wait.h"), CapabilityKind::Process);
    // Windows + extended POSIX
    EXPECT_EQ(classifyImport("windows.h"), CapabilityKind::Process);
    EXPECT_EQ(classifyImport("process.h"), CapabilityKind::Process);
    EXPECT_EQ(classifyImport("sys/ptrace.h"), CapabilityKind::Process);
    EXPECT_EQ(classifyImport("signal.h"), CapabilityKind::Process);
}

TEST(CapabilityCatalog, DynamicLoadImports) {
    EXPECT_EQ(classifyImport("dlfcn.h"), CapabilityKind::DynamicLoad);
    EXPECT_EQ(classifyImport("ltdl.h"), CapabilityKind::DynamicLoad);
    // Windows
    EXPECT_EQ(classifyImport("libloaderapi.h"), CapabilityKind::DynamicLoad);
}

TEST(CapabilityCatalog, SafeStdlibImports) {
    EXPECT_EQ(classifyImport("vector"), std::nullopt);
    EXPECT_EQ(classifyImport("string"), std::nullopt);
    EXPECT_EQ(classifyImport("algorithm"), std::nullopt);
    EXPECT_EQ(classifyImport("iostream"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("memory"), std::nullopt);
    EXPECT_EQ(classifyImport("map"), std::nullopt);
    EXPECT_EQ(classifyImport("unordered_map"), std::nullopt);
}

TEST(CapabilityCatalog, FileApis) {
    EXPECT_EQ(classifyApiCall("fopen"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("fclose"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("fread"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("fwrite"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("mmap"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("munmap"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("freopen"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("tmpfile"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("mkstemp"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("sendfile"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("aio_read"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("aio_write"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("std::ifstream"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("std::ofstream"), CapabilityKind::File);
}

TEST(CapabilityCatalog, NetworkApis) {
    EXPECT_EQ(classifyApiCall("socket"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("connect"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("bind"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("listen"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("accept"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("send"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("recv"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("sendto"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("recvfrom"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("getaddrinfo"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("gethostbyname"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("epoll_create"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("epoll_ctl"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("epoll_wait"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("poll"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("select"), CapabilityKind::Network);
}

TEST(CapabilityCatalog, ProcessApis) {
    EXPECT_EQ(classifyApiCall("system"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("fork"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("popen"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("posix_spawn"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("execve"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("execvp"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("waitpid"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("kill"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("signal"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("ptrace"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("CreateProcess"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("_spawnl"), CapabilityKind::Process);
    // exec* family (prefix match)
    EXPECT_EQ(classifyApiCall("execl"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("execv"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("execle"), CapabilityKind::Process);
}

TEST(CapabilityCatalog, DynamicLoadApis) {
    EXPECT_EQ(classifyApiCall("dlopen"), CapabilityKind::DynamicLoad);
    EXPECT_EQ(classifyApiCall("dlsym"), CapabilityKind::DynamicLoad);
    EXPECT_EQ(classifyApiCall("dlclose"), CapabilityKind::DynamicLoad);
    // Windows
    EXPECT_EQ(classifyApiCall("LoadLibrary"), CapabilityKind::DynamicLoad);
    EXPECT_EQ(classifyApiCall("GetProcAddress"), CapabilityKind::DynamicLoad);
}

TEST(CapabilityCatalog, SafeApis) {
    EXPECT_EQ(classifyApiCall("printf"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("strlen"), std::nullopt);
    EXPECT_EQ(classifyApiCall("std::sort"), std::nullopt);
}

TEST(CapabilityCatalog, MemoryApis) {
    // Allocation family
    EXPECT_EQ(classifyApiCall("malloc"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("calloc"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("realloc"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("free"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("aligned_alloc"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("posix_memalign"), CapabilityKind::Memory);
    // Raw block operations
    EXPECT_EQ(classifyApiCall("memcpy"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("memmove"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("memset"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("memcmp"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("bzero"), CapabilityKind::Memory);
    // Unbounded string copy family (classic buffer-overflow sources)
    EXPECT_EQ(classifyApiCall("strcpy"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("strncpy"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("strcat"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("strdup"), CapabilityKind::Memory);
    // C++ placement new / destroy
    EXPECT_EQ(classifyApiCall("construct_at"), CapabilityKind::Memory);
    EXPECT_EQ(classifyApiCall("destroy_at"), CapabilityKind::Memory);
}

// ── Java ───────────────────────────────────────────────────────

TEST(CapabilityCatalog, JavaImportClassification) {
    // File — exact and prefix
    EXPECT_EQ(classifyImport("java.io"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("java.io.File"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("java.nio"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("java.nio.file.Path"), CapabilityKind::File);
    // Network
    EXPECT_EQ(classifyImport("java.net"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("java.net.Socket"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("javax.net"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("javax.net.ssl.SSLSocket"), CapabilityKind::Network);
    // Process — exact match
    EXPECT_EQ(classifyImport("java.lang.ProcessBuilder"), CapabilityKind::Process);
    EXPECT_EQ(classifyImport("java.lang.Runtime"), CapabilityKind::Process);
    // DynamicLoad
    EXPECT_EQ(classifyImport("java.lang.ClassLoader"), CapabilityKind::DynamicLoad);
    EXPECT_EQ(classifyImport("java.lang.reflect"), CapabilityKind::DynamicLoad);
    EXPECT_EQ(classifyImport("java.lang.reflect.Method"), CapabilityKind::DynamicLoad);
    // Safe Java imports
    EXPECT_EQ(classifyImport("java.util.List"), std::nullopt);
    EXPECT_EQ(classifyImport("java.lang.String"), std::nullopt);
}

TEST(CapabilityCatalog, JavaApiCallClassification) {
    // File
    EXPECT_EQ(classifyApiCall("FileInputStream"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("FileOutputStream"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("FileReader"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("FileWriter"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("RandomAccessFile"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("Files.read"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("Files.write"), CapabilityKind::File);
    // Network
    EXPECT_EQ(classifyApiCall("Socket"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("ServerSocket"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("DatagramSocket"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("URLConnection"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("HttpURLConnection"), CapabilityKind::Network);
    // Process
    EXPECT_EQ(classifyApiCall("ProcessBuilder"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("Runtime.exec"), CapabilityKind::Process);
    // DynamicLoad
    EXPECT_EQ(classifyApiCall("Class.forName"), CapabilityKind::DynamicLoad);
    EXPECT_EQ(classifyApiCall("ClassLoader.loadClass"), CapabilityKind::DynamicLoad);
}

// ── Python ─────────────────────────────────────────────────────

TEST(CapabilityCatalog, PythonImportClassification) {
    // File
    EXPECT_EQ(classifyImport("os"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("io"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("pathlib"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("shutil"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("pickle"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("tempfile"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("glob"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("fnmatch"), CapabilityKind::File);
    // Network
    EXPECT_EQ(classifyImport("socket"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("http"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("urllib"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("requests"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("ftplib"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("smtplib"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("ssl"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("xmlrpc"), CapabilityKind::Network);
    // Process
    EXPECT_EQ(classifyImport("subprocess"), CapabilityKind::Process);
    EXPECT_EQ(classifyImport("multiprocessing"), CapabilityKind::Process);
    // DynamicLoad
    EXPECT_EQ(classifyImport("importlib"), CapabilityKind::DynamicLoad);
    EXPECT_EQ(classifyImport("ctypes"), CapabilityKind::DynamicLoad);
    // Safe Python imports
    EXPECT_EQ(classifyImport("json"), std::nullopt);
    EXPECT_EQ(classifyImport("collections"), std::nullopt);
}

TEST(CapabilityCatalog, PythonApiCallClassification) {
    // File ("open" already tested under C API)
    EXPECT_EQ(classifyApiCall("os.open"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("os.read"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("os.write"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("pathlib.Path"), CapabilityKind::File);
    // Network
    EXPECT_EQ(classifyApiCall("socket.socket"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("urlopen"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("requests.get"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("requests.post"), CapabilityKind::Network);
    // Process
    EXPECT_EQ(classifyApiCall("subprocess.run"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("subprocess.Popen"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("subprocess.call"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("os.system"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("os.popen"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("os.fork"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("os.exec"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("os.execvp"), CapabilityKind::Process);
    // DynamicLoad
    EXPECT_EQ(classifyApiCall("importlib.import_module"), CapabilityKind::DynamicLoad);
    EXPECT_EQ(classifyApiCall("ctypes.cdll"), CapabilityKind::DynamicLoad);
    EXPECT_EQ(classifyApiCall("ctypes.CDLL"), CapabilityKind::DynamicLoad);
}

// ── Rust ───────────────────────────────────────────────────────

TEST(CapabilityCatalog, RustImportClassification) {
    // File
    EXPECT_EQ(classifyImport("std::fs"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("std::io"), CapabilityKind::File);
    // Network — exact and prefix
    EXPECT_EQ(classifyImport("std::net"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("tokio::net"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("tokio::net::TcpListener"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("hyper"), CapabilityKind::Network);
    // Process
    EXPECT_EQ(classifyImport("std::process"), CapabilityKind::Process);
    EXPECT_EQ(classifyImport("tokio::process"), CapabilityKind::Process);
    EXPECT_EQ(classifyImport("tokio::process::Command"), CapabilityKind::Process);
    // DynamicLoad
    EXPECT_EQ(classifyImport("libloading"), CapabilityKind::DynamicLoad);
    EXPECT_EQ(classifyImport("dlopen"), CapabilityKind::DynamicLoad);
    // Async runtime crates — direct match
    EXPECT_EQ(classifyImport("tokio"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("tokio::fs"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("tokio::fs::File"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("async-std"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("smol"), CapabilityKind::File);
    // async-std sub-module prefix matches
    EXPECT_EQ(classifyImport("async_std::fs"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("async_std::io"), CapabilityKind::File);
    EXPECT_EQ(classifyImport("async_std::net"), CapabilityKind::Network);
    EXPECT_EQ(classifyImport("async_std::process"), CapabilityKind::Process);
    // Safe Rust imports
    EXPECT_EQ(classifyImport("std::collections"), std::nullopt);
    EXPECT_EQ(classifyImport("std::fmt"), std::nullopt);
}

TEST(CapabilityCatalog, RustApiCallClassification) {
    // File
    EXPECT_EQ(classifyApiCall("File::open"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("File::create"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("fs::read"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("fs::write"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("fs::read_to_string"), CapabilityKind::File);
    // stdout/stderr output macros
    EXPECT_EQ(classifyApiCall("println"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("eprintln"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("print"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("eprint"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("write!"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("writeln!"), CapabilityKind::File);
    // Buffered I/O wrappers
    EXPECT_EQ(classifyApiCall("BufWriter::new"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("BufReader::new"), CapabilityKind::File);
    // tokio async filesystem
    EXPECT_EQ(classifyApiCall("tokio::fs::read"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("tokio::fs::write"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("tokio::fs::remove_file"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("tokio::fs::create_dir"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("tokio::fs::copy"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("tokio::fs::rename"), CapabilityKind::File);
    EXPECT_EQ(classifyApiCall("tokio::fs::read_to_string"), CapabilityKind::File);
    // Network
    EXPECT_EQ(classifyApiCall("TcpListener::bind"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("TcpStream::connect"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("UdpSocket::bind"), CapabilityKind::Network);
    // tokio async network
    EXPECT_EQ(classifyApiCall("tokio::net::TcpListener::bind"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("tokio::net::TcpStream::connect"), CapabilityKind::Network);
    EXPECT_EQ(classifyApiCall("tokio::net::UdpSocket::bind"), CapabilityKind::Network);
    // Process
    EXPECT_EQ(classifyApiCall("Command::new"), CapabilityKind::Process);
    EXPECT_EQ(classifyApiCall("Command::spawn"), CapabilityKind::Process);
    // DynamicLoad
    EXPECT_EQ(classifyApiCall("Library::new"), CapabilityKind::DynamicLoad);
}
