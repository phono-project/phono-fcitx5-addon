#include "fdstream.h"
#include "check.h"
#include <fcntl.h>
#include <iostream>
#include <iterator>

int main() {
    char path[] = "/tmp/phono-fdstream-XXXXXX";
    const int fd = mkstemp(path);
    CHECK(fd >= 0);
    unlink(path);
    try {
        std::string expected(65537, '\0');
        for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<char>(i % 256);
        {
            phono_fcitx::OutputFDStreamBuf buffer(fd);
            std::ostream out(&buffer);
            out.write(expected.data(), expected.size());
            out.flush();
            CHECK(out.good());
        }
        CHECK(fcntl(fd, F_GETFD) >= 0);
        CHECK(lseek(fd, 0, SEEK_SET) == 0);
        {
            phono_fcitx::InputFDStreamBuf buffer(fd);
            std::istream in(&buffer);
            const std::string actual{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            CHECK(actual == expected);
        }
        CHECK(fcntl(fd, F_GETFD) >= 0);
        close(fd);
        phono_fcitx::OutputFDStreamBuf rejected(-1);
        std::ostream out(&rejected);
        out << "must not report a successful dictionary save";
        out.flush();
        CHECK(!out.good());
    } catch (const std::exception &e) { close(fd); std::cerr << e.what() << '\n'; return 1; }
}
