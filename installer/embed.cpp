#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 3)
    return 1;
  std::ifstream input(argv[1], std::ios::binary);
  if (!input)
    return 1;
  std::vector<unsigned char> image((std::istreambuf_iterator<char>(input)), {});
  if (input.bad() || image.empty() || image.size() > 16 * 1024 * 1024)
    return 1;
  FILE *output = fopen(argv[2], "w");
  if (!output)
    return 1;
  fprintf(output, "#pragma once\nstatic const unsigned char kFirmwareImage[] = {\n");
  for (size_t i = 0; i < image.size(); ++i)
    fprintf(output, "0x%02x,%s", image[i], (i + 1) % 16 ? " " : "\n");
  fprintf(output, "\n};\n");
  bool failed = ferror(output);
  return fclose(output) != 0 || failed ? 1 : 0;
}
