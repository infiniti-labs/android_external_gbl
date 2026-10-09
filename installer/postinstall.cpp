#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
bool Transfer(int fd, unsigned char *bytes, size_t size, bool write) {
  while (size) {
    ssize_t count = write ? ::write(fd, bytes, size) : ::read(fd, bytes, size);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return false;
    bytes += count;
    size -= count;
  }
  return true;
}

unsigned Read16(const std::vector<unsigned char> &image, size_t at) {
  return image[at] | unsigned(image[at + 1]) << 8;
}
unsigned Read32(const std::vector<unsigned char> &image, size_t at) {
  return Read16(image, at) | Read16(image, at + 2) << 16;
}

bool ValidImage(const std::vector<unsigned char> &image) {
  if (image.size() < 64 || image[0] != 'M' || image[1] != 'Z')
    return false;
  size_t pe = Read32(image, 60);
  if (pe > image.size() - 24 || Read32(image, pe) != 0x4550 ||
      Read16(image, pe + 4) != 0xaa64)
    return false;
  size_t optional = pe + 24;
  unsigned optional_size = Read16(image, pe + 20);
  if (optional_size < 160 || optional_size > image.size() - optional ||
      Read16(image, optional) != 0x20b || Read16(image, optional + 68) != 10 ||
      Read32(image, optional + 16) == 0 || Read32(image, optional + 152) == 0 ||
      Read32(image, optional + 156) == 0)
    return false;
  size_t sections = optional + optional_size;
  unsigned count = Read16(image, pe + 6);
  if (!count || count > (image.size() - sections) / 40)
    return false;
  for (unsigned i = 0; i < count; ++i) {
    size_t section = sections + i * 40;
    size_t size = Read32(image, section + 16),
           offset = Read32(image, section + 20);
    if (offset > image.size() || size > image.size() - offset)
      return false;
  }
  return true;
}

bool Install(const std::string &path) {
  int input = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  struct stat info{};
  if (input < 0)
    return false;
  bool valid = fstat(input, &info) == 0 && S_ISREG(info.st_mode) &&
               info.st_size > 0 && info.st_size <= 16 * 1024 * 1024;
  std::vector<unsigned char> image(valid ? info.st_size : 0);
  valid = valid && Transfer(input, image.data(), image.size(), false);
  close(input);
  if (!valid || !ValidImage(image))
    return false;
  int device = open("/dev/block/by-name/efisp", O_RDWR | O_CLOEXEC);
  if (device < 0)
    return false;
  uint64_t capacity = 0;
  valid = fstat(device, &info) == 0 && S_ISBLK(info.st_mode) &&
          ioctl(device, BLKGETSIZE64, &capacity) == 0 &&
          capacity >= image.size();
  std::vector<unsigned char> existing(image.size());
  if (valid)
    valid = Transfer(device, existing.data(), existing.size(), false);
  if (valid && existing != image) {
    valid = lseek(device, 0, SEEK_SET) == 0 &&
            Transfer(device, image.data(), image.size(), true) &&
            fsync(device) == 0;
    if (valid)
      valid = lseek(device, 0, SEEK_SET) == 0 &&
              Transfer(device, existing.data(), existing.size(), false) &&
              existing == image;
  }
  close(device);
  return valid;
}
} // namespace

int main(int argc, char **argv) {
  std::array<char, 4096> path{};
  ssize_t length = readlink("/proc/self/exe", path.data(), path.size() - 1);
  if (length <= 0 || size_t(length) >= path.size() - 1 || argc < 3)
    return 1;
  if (strcmp(argv[1], "0") != 0 && strcmp(argv[1], "1") != 0)
    return 1;
  std::string executable(path.data(), length);
  auto slash = executable.rfind('/');
  if (slash == std::string::npos)
    return 1;
  std::string bin = executable.substr(0, slash);
  std::string system = bin.substr(0, bin.rfind('/'));
  std::string preopt = bin + "/otapreopt_script";
  std::vector<char *> arguments{preopt.data()};
  for (int i = 1; i < argc; ++i)
    arguments.push_back(argv[i]);
  arguments.push_back(nullptr);
  pid_t child = fork();
  if (child < 0)
    return 1;
  if (child == 0) {
    execv(preopt.c_str(), arguments.data());
    _exit(127);
  }
  int status = 0;
  pid_t result;
  do {
    result = waitpid(child, &status, 0);
  } while (result < 0 && errno == EINTR);
  if (result < 0)
    return 1;
  if (!WIFEXITED(status) || WEXITSTATUS(status))
    fprintf(stderr, "GBL: optional otapreopt failed, status=%d\n", status);
  if (!Install(system + "/etc/gbl/efisp.img")) {
    fprintf(stderr, "GBL: shared efisp installation failed: %s\n",
            strerror(errno));
    return 1;
  }
  fprintf(stdout, "GBL: shared efisp verified\n");
  return 0;
}
