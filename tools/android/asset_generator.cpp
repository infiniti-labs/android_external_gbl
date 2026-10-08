#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/sha.h>

extern "C" {
#include "LzmaDec.h"
#include "patchs/core.h"
}

namespace {
using Bytes = std::vector<uint8_t>;
constexpr size_t kLimit = 32 * 1024 * 1024;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

uint32_t Read(const Bytes& data, size_t offset, size_t width) {
    Require(offset <= data.size() && width <= data.size() - offset, "Truncated image");
    uint32_t result = 0;
    for (size_t i = 0; i < width; ++i) result |= uint32_t(data[offset + i]) << (8 * i);
    return result;
}

void Put(Bytes& data, size_t offset, uint32_t value, size_t width) {
    Require(offset <= data.size() && width <= data.size() - offset, "Invalid output range");
    for (size_t i = 0; i < width; ++i) data[offset + i] = value >> (8 * i);
}

Bytes Load(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    Require(bool(stream), "Cannot read " + path.string());
    auto size = stream.tellg();
    Require(size >= 0 && size <= static_cast<std::streamoff>(kLimit), "Input exceeds 32 MiB");
    Bytes result(static_cast<size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(result.data()), result.size());
    Require(bool(stream), "Incomplete read: " + path.string());
    return result;
}

void Save(const std::filesystem::path& path, const Bytes& data) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), data.size());
    stream.close();
    Require(bool(stream), "Cannot write " + path.string());
}

size_t PeSize(const Bytes& data) {
    if (data.size() < 64 || data[0] != 'M' || data[1] != 'Z') return 0;
    size_t pe = Read(data, 60, 4);
    if (pe > data.size() || data.size() - pe < 24) return 0;
    if (Read(data, pe, 4) != 0x4550 || Read(data, pe + 4, 2) != 0xaa64) return 0;
    size_t count = Read(data, pe + 6, 2), optional = Read(data, pe + 20, 2);
    if (!count || count > 96 || optional < 112 || optional > data.size() - pe - 24) return 0;
    if (Read(data, pe + 24, 2) != 0x20b || Read(data, pe + 92, 2) != 10) return 0;
    size_t table = pe + 24 + optional;
    if (count * 40 > data.size() - table) return 0;
    size_t headers = table + count * 40, size = Read(data, pe + 84, 4);
    if (size < headers || size > data.size()) return 0;
    std::vector<std::pair<uint64_t, uint64_t>> raw, mapped;
    for (size_t i = 0; i < count; ++i) {
        size_t at = table + i * 40;
        uint64_t length = Read(data, at + 16, 4), offset = Read(data, at + 20, 4);
        uint64_t va = Read(data, at + 12, 4);
        uint64_t extent = std::max<uint64_t>(Read(data, at + 8, 4), length);
        if (offset + length > data.size() || (length && offset < headers) || va + extent > 0x100000000ULL) return 0;
        for (const auto& range : raw) if (length && offset < range.second && range.first < offset + length) return 0;
        for (const auto& range : mapped) if (extent && va < range.second && range.first < va + extent) return 0;
        if (length) raw.emplace_back(offset, offset + length);
        if (extent) mapped.emplace_back(va, va + extent);
        size = std::max<size_t>(size, offset + length);
    }
    return size <= kLimit ? size : 0;
}

void* Allocate(ISzAllocPtr, size_t size) { return size <= kLimit ? malloc(size) : nullptr; }
void Release(ISzAllocPtr, void* address) { free(address); }
const ISzAlloc kAllocator = {Allocate, Release};

class Extractor {
  public:
    Bytes Extract(const Bytes& input) {
        Require(input.size() >= 64, "ABL input must be at least 64 bytes");
        Scan(input, 0);
        Require(!best_.empty(), "No complete ARM64 EFI application in ABL");
        return best_;
    }

  private:
    void Scan(const Bytes& data, unsigned depth) {
        total_ += data.size();
        Require(total_ <= 4 * kLimit, "ABL scan budget exceeded");
        for (size_t at = 0; at + 1 < data.size(); ++at) {
            if (data[at] != 'M' || data[at + 1] != 'Z' || data.size() - at <= remaining_) continue;
            Bytes candidate(data.begin() + at, data.end());
            size_t size = PeSize(candidate);
            if (size) {
                remaining_ = candidate.size();
                candidate.resize(size);
                best_ = std::move(candidate);
            }
        }
        if (depth == 5) return;
        for (size_t at = 0; at + 13 < data.size(); ++at) {
            if (data[at] != 0x5d || data[at + 1] || data[at + 2]) continue;
            Require(++attempts_ <= 512, "ABL decompression attempt budget exceeded");
            for (size_t skip : {size_t(5), size_t(13)}) {
                size_t capacity = std::min(kLimit, 4 * kLimit - total_);
                uint64_t declared = UINT64_MAX;
                if (skip == 13) {
                    declared = 0;
                    for (size_t i = 0; i < 8; ++i) declared |= uint64_t(data[at + 5 + i]) << (8 * i);
                    if (declared != UINT64_MAX && (declared <= 64 || declared > capacity)) continue;
                    if (declared != UINT64_MAX) capacity = declared;
                }
                Bytes output(capacity);
                SizeT length = output.size(), compressed = std::min<size_t>(0x200000, data.size() - at) - skip;
                ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
                SRes result = LzmaDecode(output.data(), &length, data.data() + at + skip, &compressed,
                                        data.data() + at, 5, LZMA_FINISH_END, &status, &kAllocator);
                bool complete = status == LZMA_STATUS_FINISHED_WITH_MARK ||
                    (declared != UINT64_MAX && length == declared && status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK);
                if (result == SZ_OK && complete && length > 64) {
                    output.resize(length);
                    Scan(output, depth + 1);
                    break;
                }
            }
        }
    }
    Bytes best_;
    size_t remaining_ = 0, total_ = 0, attempts_ = 0;
};

Bytes Digest(const Bytes& data) {
    Bytes digest(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), digest.data());
    return digest;
}

Bytes Hex(const std::string& text) {
    Require(text.size() == 64 && text.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos,
            "Digest must contain exactly 64 hexadecimal characters");
    Bytes result;
    for (size_t i = 0; i < text.size(); i += 2) result.push_back(std::stoul(text.substr(i, 2), nullptr, 16));
    return result;
}

uint32_t Number(const std::string& text, uint32_t maximum, int base = 10) {
    Require(!text.empty() && text[0] != '-', "Invalid unsigned number");
    size_t consumed = 0;
    auto result = std::stoull(text, &consumed, base);
    Require(consumed == text.size() && result <= maximum, "Number outside wire-format range");
    return result;
}

bool Contains(const Bytes& data, const Bytes& needle) {
    return std::search(data.begin(), data.end(), needle.begin(), needle.end()) != data.end();
}

struct Record { uint16_t size; uint8_t semantic; uint8_t occurrences; };
const std::map<uint16_t, uint8_t> kSemantics = {
    {0x200, 7}, {0x201, 1}, {0x202, 5}, {0x203, 6}, {0x204, 8},
    {0x207, 2}, {0x208, 3}, {0x211, 4}, {0x219, 9},
};

Bytes TzMap(const Bytes& loader, const std::filesystem::path& evidence) {
    auto digest = Digest(loader);
    uint32_t flags = 0;
    const std::array<std::pair<Bytes, uint32_t>, 6> needles = {{
        {{0xce,0x62,0x48,0xa7,0x0f,0x68,0xe1,0x4f,0xa3,0x11,0xdf,0x41,0xf4,0x03,0x03,0x91},16},
        {{0x2c,0xff,0x22,0xa3,0x1a,0x6d,0xde,0x44,0xa4,0x70,0xc0,0xa8,0x9e,0x48,0xc2,0xe6},1},
        {{0x6a,0xda,0x1d,0xe1,0x1b,0x65,0xb4,0x4a,0xb8,0xc5,0x30,0xb3,0x52,0xb4,0x72,0xe2},8},
        {{0x91,0xff,0x5e,0x8e,0xb6,0x21,0xd3,0x47,0xaf,0x2b,0xc1,0x5a,0x01,0xe0,0x20,0xec},32},
        {{'k','e','y','m','a','s','t','e','r',0},2},
        {{'k','e','y','m','a','s','t','e','r','6','4',0},4},
    }};
    for (const auto& [needle, flag] : needles) if (Contains(loader, needle)) flags |= flag;
    std::map<uint16_t, Record> records;
    for (uint16_t command : {0x200,0x201,0x202,0x203,0x207,0x208,0x211}) records[command] = {0,kSemantics.at(command),0};
    for (const auto& entry : std::filesystem::directory_iterator(evidence)) {
        if (entry.path().extension() != ".commands") continue;
        std::ifstream stream(entry.path());
        Require(bool(stream), "Cannot read evidence table");
        Bytes table_digest;
        std::map<uint16_t, Record> observed;
        std::string line;
        while (std::getline(stream, line)) {
            line = line.substr(0, line.find('#'));
            std::istringstream fields(line);
            std::string field;
            if (!(fields >> field)) continue;
            if (field.rfind("sha256=", 0) == 0) {
                Require(table_digest.empty(), "Duplicate evidence digest");
                table_digest = Hex(field.substr(7));
                Require(!(fields >> field), "Unexpected digest fields");
                continue;
            }
            std::map<std::string, std::string> values;
            do {
                auto equal = field.find('=');
                Require(equal != std::string::npos && values.emplace(field.substr(0,equal),field.substr(equal+1)).second,
                        "Invalid evidence field");
            } while (fields >> field);
            Require(values.size() == 4 && values.count("command") && values.count("size") &&
                    values.count("semantic") && values.count("occurrences"), "Invalid evidence record");
            uint16_t command = Number(values.at("command"), 65535, 16);
            Require(command != 0 && !observed.count(command), "Duplicate or zero evidence command");
            uint8_t semantic = kSemantics.count(command) ? kSemantics.at(command) : 0;
            const std::array<std::string,10> tokens = {"unknown","set_rot","set_version","set_bootstate","set_vbh",
                "read_device_state","write_device_state","get_version","milestone","generate_frs_uds"};
            Require(std::find(tokens.begin(),tokens.end(),values.at("semantic")) != tokens.end(), "Unknown evidence semantic");
            observed[command] = {static_cast<uint16_t>(Number(values.at("size"),65535)),semantic,
                                 static_cast<uint8_t>(Number(values.at("occurrences"),255))};
        }
        Require(stream.eof() && table_digest.size() == 32, "Incomplete evidence table");
        if (table_digest == digest) for (const auto& record : observed) records[record.first] = record.second;
    }
    while (records.size() > 16) {
        auto candidate = records.end();
        for (auto it = records.begin(); it != records.end(); ++it) {
            if (!it->second.semantic && (candidate == records.end() || it->second.occurrences <= candidate->second.occurrences)) candidate = it;
        }
        Require(candidate != records.end(), "TZ map would drop a known command");
        records.erase(candidate);
    }
    Bytes result(256);
    std::copy_n("GTZM", 4, result.begin());
    Put(result,4,1,2); Put(result,6,records.size(),2); Put(result,8,flags,4);
    std::copy(digest.begin(),digest.end(),result.begin()+16);
    size_t at = 48;
    for (const auto& [command, record] : records) {
        Put(result,at,command,2); Put(result,at+2,record.size,2);
        result[at+4]=record.semantic; result[at+5]=record.occurrences; at+=8;
    }
    return result;
}

void Header(const std::filesystem::path& path, const Bytes& loader, const Bytes& profile, const Bytes& map) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    for (const auto& [name, bytes] : std::array<std::pair<const char*,const Bytes*>,3>{{
             {"GblLoader",&loader},{"GblProfile",&profile},{"GblTzMap",&map}}}) {
        stream << "unsigned char " << name << "[] = {\n";
        for (size_t i=0;i<bytes->size();++i) {
            stream << "0x" << std::hex << std::setw(2) << std::setfill('0') << unsigned((*bytes)[i]) << ',';
            if (i%16==15) stream << '\n';
        }
        stream << "\n};\nunsigned int " << name << "_len = " << std::dec << bytes->size() << ";\n";
    }
    stream.close();
    Require(bool(stream), "Cannot write embedded header");
}
}

int main(int argc, char** argv) {
    try {
        Require(argc == 19, "Expected nine explicit --name value arguments");
        std::map<std::string,std::string> args;
        for (int i=1;i<argc;i+=2) Require(args.emplace(argv[i],argv[i+1]).second,"Duplicate argument");
        Bytes loader = Extractor().Extract(Load(args.at("--abl")));
        Bytes map = TzMap(loader,args.at("--evidence"));
        Bytes profile(120);
        std::copy_n("GM2P",4,profile.begin()); Put(profile,4,1,2);
        Put(profile,16,Number(args.at("--system-version"),UINT32_MAX),4);
        Put(profile,20,Number(args.at("--system-spl"),UINT32_MAX),4);
        size_t at=24;
        for (const auto* key : {"--rot-digest","--pubkey-digest","--verified-boot-hash"}) {
            auto digest=Hex(args.at(key)); std::copy(digest.begin(),digest.end(),profile.begin()+at); at+=32;
        }
        uint32_t flags=PatchBufferFlags(reinterpret_cast<char*>(loader.data()),loader.size());
        Require((flags & (PATCH_REQUIRED_AVB | PATCH_REQUIRED_DICE)) == (PATCH_REQUIRED_AVB | PATCH_REQUIRED_DICE),
                "Required ABL AVB or DICE patch could not be resolved uniquely");
        Require(PeSize(loader)==loader.size(),"Patched loader is not a complete ARM64 EFI application");
        std::filesystem::path staged=args.at("--staged");
        Require(!std::filesystem::exists(staged) || std::filesystem::is_empty(staged),"Staging directory must be empty");
        std::filesystem::create_directories(staged);
        Save(staged/"boot.efi",loader); Save(staged/"boot.efi.gm2p",profile); Save(staged/"boot.efi.tzmap",map);
        Header(args.at("--header"),loader,profile,map);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "gbl_asset_generator: " << error.what() << '\n';
        return 1;
    }
}
