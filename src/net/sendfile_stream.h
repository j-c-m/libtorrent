#ifndef LIBTORRENT_NET_SENDFILE_STREAM_H
#define LIBTORRENT_NET_SENDFILE_STREAM_H

#include <cstdint>
#include <sys/types.h>

namespace torrent {

// Zero-copy file → socket. Optional userspace header is sent before file data
// (sf_hdtr on FreeBSD/Darwin; prefix write then sendfile on Linux).
//
// Returns total logical bytes written (header + file), or -1 with errno set.
// Soft failures: EAGAIN/EWOULDBLOCK/EINTR/EBUSY. On FreeBSD/Darwin, success with
// zero bytes transferred is treated as EIO (avoids past-EOF spin).
//
// On short header write, the file is not advanced (return value < header_len).
int sendfile_stream(int socket_fd, int file_fd, uint64_t offset, uint32_t length,
                    const void* header, uint32_t header_len);

} // namespace torrent

#endif
