#include "durability.h"

#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

void fsync_fd(int fd, const fs::path &path) {
	if (::fsync(fd) != 0) {
		::close(fd);
		throw std::runtime_error("fsync failed for " + path.string());
	}
}

}

void fsync_file(const fs::path &path) {

	const int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0)
		throw std::runtime_error("fsync: cannot open " + path.string());

	fsync_fd(fd, path);
	::close(fd);
}

void fsync_dir(const fs::path &path) {

	/* A directory must be opened O_RDONLY|O_DIRECTORY; fsync on it flushes the
	   directory entries, which is what makes a rename survive a power cut. */
	const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		throw std::runtime_error("fsync: cannot open directory " + path.string());

	fsync_fd(fd, path);
	::close(fd);
}

void atomic_replace(const fs::path &tmp, const fs::path &final_path) {

	fsync_file(tmp);
	fs::rename(tmp, final_path);
	fsync_dir(final_path.parent_path());
}