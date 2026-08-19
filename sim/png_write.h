#pragma once

/* Minimal PNG writer, so the headless simulator can dump a frame without
 * pulling in libpng or zlib.  Deflate is emitted as stored (uncompressed)
 * blocks, which is legal zlib and costs us file size we do not care about --
 * these images exist to be looked at once and thrown away. */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace pngw {

inline uint32_t crc32(const uint8_t *data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        built = true;
    }
    for (size_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

inline uint32_t adler32(const uint8_t *data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

inline void put_be32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

inline void put_chunk(std::vector<uint8_t> &out, const char tag[4], const std::vector<uint8_t> &data) {
    put_be32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body;
    body.insert(body.end(), tag, tag + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put_be32(out, crc32(body.data(), body.size()) ^ 0xFFFFFFFFu);
}

/* `rgb` is width*height*3 bytes, 8-bit RGB. */
inline bool write_rgb(const char *path, const uint8_t *rgb, int width, int height) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 3));
    for (int y = 0; y < height; y++) {
        raw.push_back(0);  // filter type: none
        const uint8_t *row = rgb + static_cast<size_t>(y) * width * 3;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 3);
    }

    // zlib stream: 2-byte header, stored deflate blocks, adler32 trailer.
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    const size_t kMaxBlock = 65535;
    for (size_t off = 0; off < raw.size(); off += kMaxBlock) {
        const size_t n = std::min(kMaxBlock, raw.size() - off);
        const bool last = (off + n >= raw.size());
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
    }
    put_be32(z, adler32(raw.data(), raw.size()));

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    std::vector<uint8_t> ihdr;
    put_be32(ihdr, static_cast<uint32_t>(width));
    put_be32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // colour type: truecolour
    ihdr.push_back(0);  // deflate
    ihdr.push_back(0);  // adaptive filtering
    ihdr.push_back(0);  // no interlace
    put_chunk(png, "IHDR", ihdr);
    put_chunk(png, "IDAT", z);
    put_chunk(png, "IEND", {});

    std::FILE *f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const size_t written = std::fwrite(png.data(), 1, png.size(), f);
    std::fclose(f);
    return written == png.size();
}

}  // namespace pngw
