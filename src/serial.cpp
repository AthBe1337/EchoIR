#include "echoir/serial.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

#if defined(ECHOIR_HAS_POSIX_SERIAL)
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace echoir {

#if defined(ECHOIR_HAS_POSIX_SERIAL)
namespace {

speed_t baudToConstant(int baudRate) {
    switch (baudRate) {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        default:
            throw std::invalid_argument("unsupported baud rate");
    }
}

[[noreturn]] void throwErrno(const std::string& prefix) {
    throw std::system_error(errno, std::generic_category(), prefix);
}

}  // namespace

SerialPort::SerialPort(const std::string& path, int baudRate) {
    fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd_ < 0) {
        throwErrno("open serial port");
    }

    termios tty {};
    if (::tcgetattr(fd_, &tty) != 0) {
        const int saved = errno;
        ::close(fd_);
        fd_ = -1;
        throw std::system_error(saved, std::generic_category(), "tcgetattr");
    }

    ::cfmakeraw(&tty);
    const speed_t speed = baudToConstant(baudRate);
    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    tty.c_cflag |= static_cast<unsigned int>(CLOCAL | CREAD);
    tty.c_cflag &= static_cast<unsigned int>(~PARENB);
    tty.c_cflag &= static_cast<unsigned int>(~CSTOPB);
    tty.c_cflag &= static_cast<unsigned int>(~CSIZE);
    tty.c_cflag |= CS8;
#if defined(CRTSCTS)
    tty.c_cflag &= static_cast<unsigned int>(~CRTSCTS);
#endif
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        const int saved = errno;
        ::close(fd_);
        fd_ = -1;
        throw std::system_error(saved, std::generic_category(), "tcsetattr");
    }
    ::tcflush(fd_, TCIOFLUSH);
}

SerialPort::~SerialPort() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void SerialPort::writeAll(const Bytes& bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = ::write(fd_, bytes.data() + written, bytes.size() - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            throwErrno("write serial port");
        }
        if (n == 0) {
            throw std::runtime_error("serial write returned zero bytes");
        }
        written += static_cast<std::size_t>(n);
    }
    if (::tcdrain(fd_) != 0) {
        throwErrno("tcdrain");
    }
}

Bytes SerialPort::readSome(std::size_t maxBytes, std::chrono::milliseconds timeout) {
    pollfd pfd {};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    const int rc = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (rc < 0) {
        if (errno == EINTR) {
            return {};
        }
        throwErrno("poll serial port");
    }
    if (rc == 0 || (pfd.revents & POLLIN) == 0) {
        return {};
    }

    Bytes bytes(maxBytes);
    const ssize_t n = ::read(fd_, bytes.data(), bytes.size());
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            return {};
        }
        throwErrno("read serial port");
    }
    bytes.resize(static_cast<std::size_t>(n));
    return bytes;
}

#else

SerialPort::SerialPort(const std::string&, int) {
    throw std::runtime_error("POSIX serial support is not available in this build");
}

SerialPort::~SerialPort() = default;

void SerialPort::writeAll(const Bytes&) {
    throw std::runtime_error("serial support is not available in this build");
}

Bytes SerialPort::readSome(std::size_t, std::chrono::milliseconds) {
    throw std::runtime_error("serial support is not available in this build");
}

#endif

std::unique_ptr<ITransport> openSerialTransport(const std::string& path, int baudRate) {
    return std::make_unique<SerialPort>(path, baudRate);
}

}  // namespace echoir

