#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <vector>

#include "io_service.hpp"
#include "when.hpp"

#define BS	(8*1024)

static off_t get_file_size(int fd) {
	struct stat st;

	if (fstat(fd, &st) < 0) panic("fstat");

	if (__builtin_expect(S_ISREG(st.st_mode), true)) {
		return st.st_size;
	}

    if (S_ISBLK(st.st_mode)) {
		if (unsigned long long bytes; ioctl(fd, BLKGETSIZE64, &bytes) < 0) {
            panic("ioctl");
        } else {
			return bytes;
        }
	}

    throw std::runtime_error("Unsupported file type");
}

task<> copy_file(io_service& service, off_t insize) {
    std::vector<char> buf(BS, '\0');
    std::vector<task<int>> tmp;
    tmp.reserve(insize / BS);
    service.register_buffers({ to_iov(buf.data(), buf.size()) });
    on_scope_exit unreg_bufs([&]() { service.unregister_buffers(); });

    off_t offset = 0;
    for (; offset < insize - BS; offset += BS) {
        // We MUST push these tasks into a vector to preventing them from early destruction.
        tmp.push_back(service.read_fixed(0, buf.data(), buf.size(), offset, 0, IOSQE_FIXED_FILE | IOSQE_IO_LINK));
        tmp.push_back(service.write_fixed(1, buf.data(), buf.size(), offset, 0, IOSQE_FIXED_FILE | IOSQE_IO_LINK));
    }

    int left = co_await service.read_fixed(0, buf.data(), buf.size(), offset, 0, IOSQE_FIXED_FILE);
    if (left) {
        co_await service.write_fixed(1, buf.data(), left, offset, 0, IOSQE_FIXED_FILE);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("%s: infile outfile\n", argv[0]);
        return 1;
    }

    int infd = open(argv[1], O_RDONLY);
    if (infd < 0) panic("open infile");
    on_scope_exit close_infd([=]() { close(infd); });

    int outfd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outfd < 0) panic("open outfile");
    on_scope_exit close_outfd([=]() { close(outfd); });

    off_t insize = get_file_size(infd);
    io_service service;
    service.register_files({ infd, outfd });
    on_scope_exit unreg_file([&]() { service.unregister_files(); });

    auto work = copy_file(service, insize);

    // Event loop
    while (!work.done()) {
        auto [promise, res] = service.wait_event();

        // Found a finished event, go back to its coroutine.
        promise->resolve(res);
    }

    work.get_result();
}
