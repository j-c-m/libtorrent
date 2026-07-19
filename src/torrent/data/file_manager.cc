#include "config.h"

#include "file_manager.h"

#include <algorithm>
#include <cassert>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

#include "manager.h"
#include "data/socket_file.h"
#include "data/thread_disk.h"
#include "torrent/exceptions.h"
#include "torrent/data/file.h"

namespace torrent {

namespace {

void
close_file_descriptor(int fd) {
  if (fd < 0)
    return;

  // Prefer the disk thread so main/UI is not blocked on slow FS close.
  if (ThreadDisk* disk = thread_disk(); disk != nullptr && disk->is_active())
    disk->queue_close_fd(fd);
  else
    ::close(fd);
}

} // namespace

FileManager::~FileManager() {
  assert(empty() && "FileManager::~FileManager() called but empty() != true.");
}

void
FileManager::set_max_open_files(size_type s) {
  if (s < 4 || s > (1 << 16))
    throw input_error("Max open files must be between 4 and 2^16.");

  m_max_open_files = s;

  while (size() > m_max_open_files)
    close_least_active();
}

bool
FileManager::open(value_type file, [[maybe_unused]] bool hashing, int prot, int flags) {
  if (file->is_padding())
    return true;

  if (file->is_open())
    close(file);

  if (size() > m_max_open_files)
    throw internal_error("FileManager::open_file(...) m_openSize > m_max_open_files.");

  if (size() == m_max_open_files)
    close_least_active();

  SocketFile fd;

  if (!fd.open(file->frozen_path(), prot, flags)) {
    m_files_failed_counter++;
    return false;
  }

  file->set_protection(prot);
  file->set_file_descriptor(fd.fd());

#ifdef USE_POSIX_FADVISE
  if (hashing) {
    if (m_advise_random_hashing)
      posix_fadvise(fd.fd(), 0, 0, POSIX_FADV_RANDOM);
  } else {
    if (m_advise_random)
      posix_fadvise(fd.fd(), 0, 0, POSIX_FADV_RANDOM);
  }
#endif

  base_type::push_back(file);

  // Consider storing the position of the file here.

  m_files_opened_counter++;
  return true;
}

int
FileManager::release(value_type file) {
  if (file == nullptr || !file->is_open() || file->is_padding())
    return -1;

  int fd = file->file_descriptor();

  file->set_protection(0);
  file->set_file_descriptor(-1);

  auto itr = std::find(begin(), end(), file);

  if (itr == end())
    throw internal_error("FileManager::release(...) itr == end().");

  *itr = back();
  base_type::pop_back();

  m_files_closed_counter++;
  return fd;
}

void
FileManager::close(value_type file) {
  close_file_descriptor(release(file));
}

void
FileManager::close_files(const std::vector<value_type>& files) {
  if (files.empty())
    return;

  std::vector<int> fds;
  fds.reserve(files.size());

  for (value_type file : files) {
    int fd = release(file);

    if (fd >= 0)
      fds.push_back(fd);
  }

  if (fds.empty())
    return;

  if (ThreadDisk* disk = thread_disk(); disk != nullptr && disk->is_active())
    disk->queue_close_fds(fds);
  else
    for (int fd : fds)
      ::close(fd);
}

void
FileManager::close_least_active() {
  File* least = nullptr;
  uint64_t last = std::numeric_limits<int64_t>::max();

  for (auto f : *this) {
    if (f->is_open() && f->last_touched() <= last) {
      last = f->last_touched();
      least = f;
    }
  }

  if (least)
    close(least);
}

void
FileManager::periodic_close_idle() {
  if (m_close_idle == 0 || empty())
    return;

  const uint64_t now = this_thread::cached_time().count();
  const uint64_t limit = static_cast<uint64_t>(m_close_idle) * 1'000'000ull;

  size_type i = 0;
  while (i < size()) {
    File* f = base_type::operator[](i);

    if (!f->is_open() || f->last_touched() > now) {
      ++i;
      continue;
    }

    if (now - f->last_touched() >= limit)
      close(f);
    else
      ++i;
  }
}

} // namespace torrent
