#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/file-descriptor.hh"

#include <unistd.h>

#include <filesystem>

namespace nix {

/**
 * Create a Unix domain socket.
 */
AutoCloseFD createUnixDomainSocket();

/**
 * Create a Unix domain socket in listen mode.
 */
AutoCloseFD createUnixDomainSocket(const Path & path, mode_t mode);

/**
 * Often we want to use `Descriptor`, but Windows makes a slightly
 * stronger file descriptor vs socket distinction, at least at the level
 * of C types.
 */
using Socket = int;

/**
 * Convert a `Socket` to a `Descriptor`
 *
 * This is a no-op except on Windows.
 */
static inline Socket toSocket(Descriptor fd)
{
    return fd;
}

/**
 * Convert a `Socket` to a `Descriptor`
 *
 * This is a no-op except on Windows.
 */
static inline Descriptor fromSocket(Socket fd)
{
    return fd;
}

/**
 * Bind a Unix domain socket to a path.
 */
void bind(Socket fd, const std::string & path);

/**
 * Connect to a Unix domain socket.
 */
void connect(Socket fd, const std::filesystem::path & path);

/**
 * Connect to a Unix domain socket.
 */
AutoCloseFD connect(const std::filesystem::path & path);

}
