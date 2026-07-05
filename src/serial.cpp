#include "echoir/serial.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>

#if defined(ECHOIR_HAS_POSIX_SERIAL)
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

#if defined(ECHOIR_HAS_WIN32_SERIAL)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cwctype>
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

struct SerialPort::Impl {
    int fd = -1;

    ~Impl() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
};

SerialPort::SerialPort(const std::string& path, int baudRate) {
    impl_ = std::make_unique<Impl>();
    impl_->fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (impl_->fd < 0) {
        throwErrno("open serial port");
    }

    termios tty {};
    if (::tcgetattr(impl_->fd, &tty) != 0) {
        const int saved = errno;
        impl_.reset();
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

    if (::tcsetattr(impl_->fd, TCSANOW, &tty) != 0) {
        const int saved = errno;
        impl_.reset();
        throw std::system_error(saved, std::generic_category(), "tcsetattr");
    }
    ::tcflush(impl_->fd, TCIOFLUSH);
}

SerialPort::~SerialPort() = default;

void SerialPort::writeAll(const Bytes& bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = ::write(impl_->fd, bytes.data() + written, bytes.size() - written);
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
    if (::tcdrain(impl_->fd) != 0) {
        throwErrno("tcdrain");
    }
}

Bytes SerialPort::readSome(std::size_t maxBytes, std::chrono::milliseconds timeout) {
    pollfd pfd {};
    pfd.fd = impl_->fd;
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
    const ssize_t n = ::read(impl_->fd, bytes.data(), bytes.size());
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            return {};
        }
        throwErrno("read serial port");
    }
    bytes.resize(static_cast<std::size_t>(n));
    return bytes;
}

#elif defined(ECHOIR_HAS_WIN32_SERIAL)
namespace {

DWORD baudToConstant(int baudRate) {
    switch (baudRate) {
        case 9600:
            return CBR_9600;
        case 19200:
            return CBR_19200;
        case 38400:
            return CBR_38400;
        case 57600:
            return CBR_57600;
        case 115200:
            return CBR_115200;
        default:
            throw std::invalid_argument("unsupported baud rate");
    }
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                               static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                "convert serial path to UTF-16");
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                          wide.data(), required);
    return wide;
}

bool startsWithComName(const std::wstring& path) {
    if (path.size() < 4) {
        return false;
    }
    return (std::towupper(path[0]) == L'C') &&
           (std::towupper(path[1]) == L'O') &&
           (std::towupper(path[2]) == L'M') &&
           std::iswdigit(path[3]) != 0;
}

std::wstring normalizeSerialPath(const std::string& path) {
    auto wide = utf8ToWide(path);
    if (wide.rfind(LR"(\\.\)", 0) == 0) {
        return wide;
    }
    if (startsWithComName(wide)) {
        return LR"(\\.\)" + wide;
    }
    return wide;
}

[[noreturn]] void throwWindowsError(const std::string& prefix) {
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), prefix);
}

void configureTimeouts(HANDLE handle, std::chrono::milliseconds readTimeout) {
    COMMTIMEOUTS timeouts {};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(std::max<long long>(0, readTimeout.count()));
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 5000;
    if (!::SetCommTimeouts(handle, &timeouts)) {
        throwWindowsError("SetCommTimeouts");
    }
}

}  // namespace

struct SerialPort::Impl {
    HANDLE handle = INVALID_HANDLE_VALUE;

    ~Impl() {
        if (handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
        }
    }
};

SerialPort::SerialPort(const std::string& path, int baudRate) {
    impl_ = std::make_unique<Impl>();
    const auto normalizedPath = normalizeSerialPath(path);
    impl_->handle = ::CreateFileW(normalizedPath.c_str(),
                                  GENERIC_READ | GENERIC_WRITE,
                                  0,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    if (impl_->handle == INVALID_HANDLE_VALUE) {
        throwWindowsError("open serial port");
    }

    ::SetupComm(impl_->handle, 4096, 4096);
    ::PurgeComm(impl_->handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    DCB dcb {};
    dcb.DCBlength = sizeof(DCB);
    if (!::GetCommState(impl_->handle, &dcb)) {
        const auto error = ::GetLastError();
        impl_.reset();
        throw std::system_error(static_cast<int>(error), std::system_category(), "GetCommState");
    }

    dcb.BaudRate = baudToConstant(baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fAbortOnError = FALSE;

    if (!::SetCommState(impl_->handle, &dcb)) {
        const auto error = ::GetLastError();
        impl_.reset();
        throw std::system_error(static_cast<int>(error), std::system_category(), "SetCommState");
    }
    configureTimeouts(impl_->handle, std::chrono::milliseconds(0));
}

SerialPort::~SerialPort() = default;

void SerialPort::writeAll(const Bytes& bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto remaining = bytes.size() - written;
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD n = 0;
        if (!::WriteFile(impl_->handle, bytes.data() + written, chunk, &n, nullptr)) {
            throwWindowsError("write serial port");
        }
        if (n == 0) {
            throw std::runtime_error("serial write returned zero bytes");
        }
        written += n;
    }
    if (!::FlushFileBuffers(impl_->handle)) {
        throwWindowsError("flush serial port");
    }
}

Bytes SerialPort::readSome(std::size_t maxBytes, std::chrono::milliseconds timeout) {
    if (maxBytes == 0) {
        return {};
    }
    configureTimeouts(impl_->handle, timeout);
    const DWORD chunk = static_cast<DWORD>(
        std::min<std::size_t>(maxBytes, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
    Bytes bytes(chunk);
    DWORD n = 0;
    if (!::ReadFile(impl_->handle, bytes.data(), chunk, &n, nullptr)) {
        throwWindowsError("read serial port");
    }
    bytes.resize(n);
    return bytes;
}

#else

SerialPort::SerialPort(const std::string&, int) {
    throw std::runtime_error("serial support is not available in this build");
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
