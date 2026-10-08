#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
std::string Trim(std::string text) {
  auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return {};
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
std::string NormalizeGuid(std::string text) {
  text.erase(
      std::remove_if(text.begin(), text.end(),
                     [](unsigned char value) { return std::isspace(value); }),
      text.end());
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char value) { return std::tolower(value); });
  return text;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 4 && std::string(argv[1]) == "--embed") {
      std::ifstream input(argv[2], std::ios::binary);
      if (!input)
        throw std::runtime_error("Cannot read embedded driver");
      char signature[2];
      input.read(signature, 2);
      if (!input || signature[0] != 'M' || signature[1] != 'Z')
        throw std::runtime_error("Embedded driver is not a PE image");
      input.seekg(0);
      std::ofstream output(argv[3]);
      std::string symbol =
          std::string(argv[2]).find("managed") == std::string::npos
              ? "CanoeMsd"
              : "CanoeManagedMsd";
      output << "#include <Uefi.h>\nSTATIC CONST UINT8 mImage[] = {";
      char byte;
      size_t size = 0;
      while (input.get(byte)) {
        output << unsigned(static_cast<unsigned char>(byte)) << ',';
        ++size;
      }
      output << "};\nCONST UINT8 *g" << symbol
             << "Variant = mImage;\nCONST UINTN g" << symbol
             << "VariantSize = " << size << ";\n";
      output.close();
      if (!input.eof() || !output)
        throw std::runtime_error("Embedded driver I/O failure");
      return 0;
    }
    if (argc < 4)
      throw std::runtime_error("Expected header, source and DEC inputs");
    std::map<std::string, std::string> guids, values, types;
    std::string version;
    std::string caller_guid, caller_name;
    for (int i = 3; i < argc; ++i) {
      std::ifstream input(argv[i]);
      if (!input)
        throw std::runtime_error("Cannot read metadata input");
      std::string line, section;
      while (std::getline(input, line)) {
        line = Trim(line.substr(0, line.find('#')));
        if (line.empty())
          continue;
        if (line.rfind("CANOE_VERSION =", 0) == 0) {
          version = Trim(line.substr(line.find('=') + 1));
          continue;
        }
        if (line[0] == '[') {
          section = line;
          continue;
        }
        if (section == "[Defines]" &&
            std::filesystem::path(argv[i]).extension() == ".inf") {
          auto equal = line.find('=');
          if (equal != std::string::npos) {
            auto key = Trim(line.substr(0, equal));
            if (key == "FILE_GUID")
              caller_guid = Trim(line.substr(equal + 1));
            if (key == "BASE_NAME")
              caller_name = Trim(line.substr(equal + 1));
          }
        }
        if (section.find(".common") == std::string::npos &&
            section.find(".AARCH64") == std::string::npos &&
            section.find('.') != std::string::npos)
          continue;
        if (section.find("Guids") != std::string::npos ||
            section.find("Protocols") != std::string::npos ||
            section.find("Ppis") != std::string::npos) {
          auto equal = line.find('=');
          if (equal != std::string::npos) {
            auto name = Trim(line.substr(0, equal));
            auto value = Trim(line.substr(equal + 1));
            auto found = guids.find(name);
            if (found != guids.end() &&
                NormalizeGuid(found->second) != NormalizeGuid(value))
              throw std::runtime_error("Conflicting GUID definition: " + name);
            guids[name] = value;
          }
        } else if (section.find("Pcds") != std::string::npos &&
                   std::filesystem::path(argv[i]).extension() == ".dec") {
          auto pipe = line.find('|'), next = line.find('|', pipe + 1),
               last = line.find('|', next + 1);
          if (pipe == std::string::npos || next == std::string::npos ||
              last == std::string::npos)
            continue;
          auto key = Trim(line.substr(0, pipe));
          key = key.substr(key.find('.') + 1);
          values[key] = Trim(line.substr(pipe + 1, next - pipe - 1));
          types[key] = Trim(line.substr(next + 1, last - next - 1));
        }
      }
    }
    for (int i = 3; i < argc; ++i) {
      if (std::filesystem::path(argv[i]).extension() != ".dsc")
        continue;
      std::ifstream input(argv[i]);
      std::string line, section;
      while (std::getline(input, line)) {
        line = Trim(line.substr(0, line.find('#')));
        if (line.empty())
          continue;
        if (line[0] == '[') {
          section = line;
          continue;
        }
        if (section.find("PcdsFixedAtBuild") == std::string::npos &&
            section.find("PcdsFeatureFlag") == std::string::npos)
          continue;
        auto pipe = line.find('|'), dot = line.find('.');
        if (pipe == std::string::npos || dot == std::string::npos || dot > pipe)
          throw std::runtime_error("Unsupported platform PCD record");
        values[Trim(line.substr(dot + 1, pipe - dot - 1))] =
            Trim(line.substr(pipe + 1));
      }
    }
    std::ofstream header(argv[1]), source(argv[2]);
    header << "#pragma once\n#include <Uefi.h>\n#include <Library/PcdLib.h>\n";
    if (version.empty())
      throw std::runtime_error("Missing canonical Canoe version");
    header << "#define SFB_BDS_VERSION \"" << version << "\"\n";
    source << "#include \"AutoGen.h\"\n";
    for (const auto &[name, value] : guids) {
      header << "extern EFI_GUID " << name << ";\n";
      source << "EFI_GUID " << name << " = " << value << ";\n";
    }
    for (const auto &[name, value] : values) {
      if (value.empty())
        throw std::runtime_error("Empty PCD value: " + name);
      if (value[0] == '{' || value.find("L\"") == 0)
        continue;
      header << "#define _PCD_VALUE_" << name << " " << value << "\n";
      const std::map<std::string, std::string> modes = {
          {"UINT8", "8"},   {"UINT16", "16"},    {"UINT32", "32"},
          {"UINT64", "64"}, {"BOOLEAN", "BOOL"}, {"VOID*", "PTR"}};
      header << "#define _PCD_GET_MODE_" << modes.at(types.at(name)) << "_"
             << name << " _PCD_VALUE_" << name << "\n";
      header << "#define _PCD_GET_MODE_SIZE_" << name << " sizeof(_PCD_VALUE_"
             << name << ")\n";
      header << "#define _PCD_SIZE_" << name << " sizeof(_PCD_VALUE_" << name
             << ")\n";
    }
    header
        << "extern GUID gEfiCallerIdGuid;\nextern CHAR8 *gEfiCallerBaseName;\n";
    if (caller_guid.size() != 36 || caller_name.empty())
      throw std::runtime_error("Missing application identity");
    if (caller_guid[8] != '-' || caller_guid[13] != '-' ||
        caller_guid[18] != '-' || caller_guid[23] != '-' ||
        caller_guid.find_first_not_of("0123456789abcdefABCDEF-") != std::string::npos ||
        caller_name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
      throw std::runtime_error("Invalid application identity");
    source << "GUID gEfiCallerIdGuid = {0x" << caller_guid.substr(0, 8) << ",0x"
           << caller_guid.substr(9, 4) << ",0x" << caller_guid.substr(14, 4)
           << ",{0x" << caller_guid.substr(19, 2) << ",0x"
           << caller_guid.substr(21, 2);
    for (size_t offset = 24; offset < 36; offset += 2)
      source << ",0x" << caller_guid.substr(offset, 2);
    source << "}};\nCHAR8 *gEfiCallerBaseName = \"" << caller_name << "\";\n";
    header.close();
    source.close();
    if (!header || !source)
      throw std::runtime_error("Cannot publish metadata");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
