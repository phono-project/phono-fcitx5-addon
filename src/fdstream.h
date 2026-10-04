#pragma once
#include <fcitx-utils/fs.h>
#include <array>
#include <cerrno>
#include <streambuf>
#include <unistd.h>

namespace phono_fcitx {
// Borrow StandardPaths' fd without taking ownership, including on Fcitx
// versions predating its public FD stream buffers.
class InputFDStreamBuf : public std::streambuf {
public:
    explicit InputFDStreamBuf(int fd) : fd_(fd) {}
protected:
    int_type underflow() override {
        if (gptr() && gptr() < egptr()) return traits_type::to_int_type(*gptr());
        ssize_t count;
        do { count = ::read(fd_, data_.data(), data_.size()); } while (count < 0 && errno == EINTR);
        if (count <= 0) return traits_type::eof();
        setg(data_.data(), data_.data(), data_.data() + count);
        return traits_type::to_int_type(*gptr());
    }
private:
    int fd_;
    std::array<char, 8192> data_;
};
class OutputFDStreamBuf : public std::streambuf {
public:
    explicit OutputFDStreamBuf(int fd) : fd_(fd) { setp(data_.data(), data_.data() + data_.size()); }
    ~OutputFDStreamBuf() override { sync(); }
protected:
    int sync() override {
        const auto count = pptr() - pbase();
        if (count && fcitx::fs::safeWrite(fd_, pbase(), count) != count) return -1;
        setp(data_.data(), data_.data() + data_.size());
        return 0;
    }
    int_type overflow(int_type value) override {
        if (sync() < 0) return traits_type::eof();
        if (!traits_type::eq_int_type(value, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(value);
            pbump(1);
        }
        return traits_type::not_eof(value);
    }
private:
    int fd_;
    std::array<char, 8192> data_;
};
} // namespace phono_fcitx
