// Minimal WAV reader/writer (PCM 16/24/32 and IEEE float 32), host-only.
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace wav {

struct Audio {
    int sampleRate = 48000;
    int channels = 0;
    std::vector<float> data;  // interleaved
    size_t frames() const { return channels ? data.size() / channels : 0; }
};

inline bool read(const std::string& path, Audio& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char id[4]; uint32_t sz;
    if (std::fread(id, 1, 4, f) != 4 || std::memcmp(id, "RIFF", 4)) { std::fclose(f); return false; }
    std::fread(&sz, 4, 1, f);
    if (std::fread(id, 1, 4, f) != 4 || std::memcmp(id, "WAVE", 4)) { std::fclose(f); return false; }
    uint16_t fmt = 0, ch = 0, bits = 0; uint32_t sr = 0;
    bool haveFmt = false;
    while (std::fread(id, 1, 4, f) == 4 && std::fread(&sz, 4, 1, f) == 1) {
        if (!std::memcmp(id, "fmt ", 4)) {
            uint8_t buf[64] = {}; std::fread(buf, 1, sz > 64 ? 64 : sz, f); if (sz > 64) std::fseek(f, sz - 64, SEEK_CUR);
            std::memcpy(&fmt, buf, 2); std::memcpy(&ch, buf + 2, 2); std::memcpy(&sr, buf + 4, 4); std::memcpy(&bits, buf + 14, 2);
            if (fmt == 0xFFFE && sz >= 26) std::memcpy(&fmt, buf + 24, 2);   // WAVE_FORMAT_EXTENSIBLE: sub-format GUID first 2 bytes
            haveFmt = true;
        } else if (!std::memcmp(id, "data", 4)) {
            if (!haveFmt) { std::fclose(f); return false; }
            std::vector<uint8_t> raw(sz); const size_t got = std::fread(raw.data(), 1, sz, f); raw.resize(got);
            const int bps = bits / 8; const size_t n = got / bps;
            out.sampleRate = static_cast<int>(sr); out.channels = ch; out.data.resize(n);
            for (size_t i = 0; i < n; ++i) {
                const uint8_t* p = raw.data() + i * bps;
                if (fmt == 3 && bits == 32) { float v; std::memcpy(&v, p, 4); out.data[i] = v; }
                else if (bits == 16) { int16_t v; std::memcpy(&v, p, 2); out.data[i] = v / 32768.f; }
                else if (bits == 24) { int32_t v = (p[0] << 8) | (p[1] << 16) | (p[2] << 24); out.data[i] = (v >> 8) / 8388608.f; }
                else if (bits == 32) { int32_t v; std::memcpy(&v, p, 4); out.data[i] = v / 2147483648.f; }
                else { std::fclose(f); return false; }
            }
            std::fclose(f); return true;
        } else {
            std::fseek(f, sz + (sz & 1), SEEK_CUR);
        }
    }
    std::fclose(f); return false;
}

inline bool write(const std::string& path, const Audio& a, bool asFloat = true) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint16_t bits = asFloat ? 32 : 16, fmt = asFloat ? 3 : 1, ch = static_cast<uint16_t>(a.channels);
    const uint32_t dataBytes = static_cast<uint32_t>(a.data.size() * bits / 8);
    const uint32_t byteRate = a.sampleRate * ch * bits / 8; const uint16_t blockAlign = ch * bits / 8;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); w32(16); w16(fmt); w16(ch); w32(a.sampleRate); w32(byteRate); w16(blockAlign); w16(bits);
    std::fwrite("data", 1, 4, f); w32(dataBytes);
    if (asFloat) std::fwrite(a.data.data(), 4, a.data.size(), f);
    else for (float v : a.data) { const int16_t s = static_cast<int16_t>(v > 1.f ? 32767 : (v < -1.f ? -32768 : v * 32767.f)); std::fwrite(&s, 2, 1, f); }
    std::fclose(f); return true;
}

} // namespace wav
