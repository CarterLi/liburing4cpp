#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <vector>

#include "io_service.hpp"

#define BS (1024)

static off_t get_file_size(int fd) {
    struct stat st;

    fstat(fd, &st) | panic_on_err("fstat", true);

    if (__builtin_expect(S_ISREG(st.st_mode), true)) {
        return st.st_size;
    }

    if (S_ISBLK(st.st_mode)) {
        unsigned long long bytes;
        ioctl(fd, BLKGETSIZE64, &bytes) | panic_on_err("ioctl", true);
        return bytes;
    }

    throw std::runtime_error("Unsupported file type");
}

task<> copy_file(io_service& service, off_t insize) {
    std::vector<char> buf(BS, '\0');
    service.register_buffers({ to_iov(buf.data(), buf.size()) });
    on_scope_exit unreg_bufs([&]() { service.unregister_buffers(); });

    off_t offset = 0;
    for (; offset < insize - BS; offset += BS) {
        service.read_fixed(0, buf.data(), buf.size(), offset, 0, IOSQE_FIXED_FILE | IOSQE_IO_LINK) | panic_on_err("read_fixed(1)", false);
        service.write_fixed(1, buf.data(), buf.size(), offset, 0, IOSQE_FIXED_FILE | IOSQE_IO_LINK) | panic_on_err("write_fixed(1)", false);
    }

    int left = insize - offset;
    service.read_fixed(0, buf.data(), left, offset, 0, IOSQE_FIXED_FILE | IOSQE_IO_LINK) | panic_on_err("read_fixed(2)", false);
    service.write_fixed(1, buf.data(), left, offset, 0, IOSQE_FIXED_FILE) | panic_on_err("write_fixed(2)", false);
    co_await service.fsync(1, 0, IOSQE_FIXED_FILE);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("%s: infile outfile\n", argv[0]);
        return 1;
    }

    int infd = open(argv[1], O_RDONLY) | panic_on_err("open infile", true);
    on_scope_exit close_infd([=]() { close(infd); });

    int outfd = creat(argv[2], 0644) | panic_on_err("creat outfile", true);
    on_scope_exit close_outfd([=]() { close(outfd); });

    off_t insize = get_file_size(infd);
    io_service service;
    service.register_files({ infd, outfd });
    on_scope_exit unreg_file([&]() { service.unregister_files(); });

    service.run(copy_file(service, insize));
}
