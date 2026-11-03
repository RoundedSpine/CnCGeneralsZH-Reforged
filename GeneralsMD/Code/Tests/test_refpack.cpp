// Exercise the actual RefPack decoder, including exact-sized allocations for
// source truncation and destination limits so ASan can check both boundaries.
#include "test_harness.h"
#include "EAC/codex.h"
#include "EAC/refcodex.h"
#include <vector>
#include <cstring>

static std::vector<unsigned char> packet(unsigned type, unsigned size,
    std::vector<unsigned char> body)
{
    std::vector<unsigned char> result{(unsigned char)(type >> 8), (unsigned char)type};
    const int width = type & 0x8000 ? 4 : 3;
    if (type & 0x100) result.insert(result.end(), width, 0);
    for (int i = width - 1; i >= 0; --i) result.push_back((unsigned char)(size >> (8 * i)));
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

static int decode(const std::vector<unsigned char> &source, std::vector<unsigned char> &dest,
    int *consumed = nullptr)
{
    return REF_decode(dest.data(), (int)dest.size(), source.data(), (int)source.size(), consumed);
}

TEST(refpack_all_header_variants_and_terminal_literals)
{
    for (unsigned type : {0x10fbu, 0x11fbu, 0x90fbu, 0x91fbu}) {
        auto source = packet(type, 4, {0xe0, 't', 'e', 's', 't', 0xfc});
        std::vector<unsigned char> dest(4);
        int consumed = -1;
        CHECK_EQ(decode(source, dest, &consumed), 4);
        CHECK_EQ(consumed, (int)source.size());
        CHECK(std::memcmp(dest.data(), "test", 4) == 0);
        for (unsigned n = 1; n <= 3; ++n) {
            std::vector<unsigned char> body{(unsigned char)(0xfc | n)};
            body.insert(body.end(), n, 'A');
            source = packet(type, n, body);
            dest.resize(n);
            CHECK_EQ(decode(source, dest), (int)n);
            for (auto byte : dest) CHECK_EQ(byte, 'A');
        }
    }
}

TEST(refpack_all_match_forms_and_overlap)
{
    const std::vector<unsigned char> commands[] = {{0, 3}, {0x80, 0, 3}, {0xc0, 0, 3, 0}};
    for (int form = 0; form != 3; ++form) {
        std::vector<unsigned char> body{0xe0, 'a', 'b', 'c', 'd'};
        body.insert(body.end(), commands[form].begin(), commands[form].end());
        body.push_back(0xfc);
        auto source = packet(0x10fb, 7 + form, body);
        std::vector<unsigned char> dest(7 + form);
        CHECK_EQ(decode(source, dest), 7 + form);
        for (int i = 0; i < 7 + form; ++i) CHECK_EQ(dest[i], "abcd"[i % 4]);
    }
    // One literal followed by an overlapping distance-one match.
    auto source = packet(0x10fb, 261, {0xc1, 0, 0, 255, 'A', 0xfc});
    std::vector<unsigned char> dest(261);
    CHECK_EQ(decode(source, dest), 261);
    for (auto byte : dest) CHECK_EQ(byte, 'A');
}

TEST(refpack_rejects_truncated_input_at_every_byte)
{
    const std::vector<unsigned char> bodies[] = {
        {0xe0, 'a', 'b', 'c', 'd', 0xfc},
        {0x01, 0, 'A', 0xfc},
        {0x80, 0x40, 0, 'A', 0xfc},
        {0xc1, 0, 0, 0, 'A', 0xfc},
        {0xff, 'a', 'b', 'c'}
    };
    const unsigned sizes[] = {4, 4, 5, 6, 3};
    for (unsigned type : {0x10fbu, 0x11fbu, 0x90fbu, 0x91fbu}) {
        for (unsigned command = 0; command < 5; ++command) {
            const auto source = packet(type, sizes[command], bodies[command]);
            for (unsigned n = 0; n < source.size(); ++n) {
                // Separate allocations ensure an overread cannot hide in spare capacity.
                std::vector<unsigned char> truncated(source.begin(), source.begin() + n);
                std::vector<unsigned char> dest(sizes[command]);
                int consumed = -1;
                CHECK_EQ(decode(truncated, dest, &consumed), 0);
                CHECK_EQ(consumed, 0);
            }
            std::vector<unsigned char> dest(sizes[command]);
            CHECK_EQ(decode(source, dest), (int)sizes[command]);
        }
    }
}

TEST(refpack_rejects_sizes_and_invalid_references)
{
    std::vector<unsigned char> dest(4, 0xa5);
    auto source = packet(0x10fb, 4, {0xe0, 't', 'e', 's', 't', 0xfc});
    std::vector<unsigned char> small(1, 0xa5);
    CHECK_EQ(decode(source, small), 0);
    CHECK_EQ(small[0], 0xa5);
    const std::vector<unsigned char> invalid[] = {
        packet(0x12fb, 4, {0xfc}), // Unknown header.
        packet(0x90fb, 0x80000000u, {0xfc}), // Size cannot be returned as int.
        packet(0x10fb, 1, {0xe0, 't', 'e', 's', 't', 0xfc}), // Literal exceeds size.
        packet(0x10fb, 4, {0xfc}), // Premature EOF.
        packet(0x10fb, 4, {0x01, 1, 'A', 0xfc}), // Reference precedes output.
        packet(0x10fb, 4, {0x80, 0, 0, 0xfc}),
        packet(0x10fb, 4, {0xc0, 0, 0, 0, 0xfc}),
        packet(0x10fb, 1, {0x01, 0, 'A', 0xfc}) // Match exceeds size.
    };
    for (const auto &bad : invalid) CHECK_EQ(decode(bad, dest), 0);
    CHECK_EQ(REF_decode(dest.data(), -1, source.data(), (int)source.size()), 0);
    CHECK_EQ(REF_decode(dest.data(), 4, source.data(), -1), 0);
    CHECK_EQ(REF_decode(nullptr, 4, source.data(), (int)source.size()), 0);
    CHECK_EQ(REF_decode(dest.data(), 4, nullptr, 10), 0);
}

TEST(refpack_encoder_round_trips)
{
    for (int size : {1, 2, 3, 4, 113, 1024, 65536}) {
        for (int pattern = 0; pattern < 3; ++pattern) {
            std::vector<unsigned char> original(size), packed(size + size / 8 + 512), dest(size);
            unsigned random = 12345;
            for (int i = 0; i < size; ++i) {
                random = 1664525u * random + 1013904223u;
                original[i] = pattern == 0 ? 'A' : pattern == 1 ? i % 251 : random >> 24;
            }
            const int length = REF_encode(packed.data(), original.data(), size);
            CHECK(length > 0 && length <= (int)packed.size());
            if (length <= 0 || length > (int)packed.size()) continue;
            packed.resize(length);
            CHECK_EQ(decode(packed, dest), size);
            CHECK(dest == original);
        }
    }
}

TEST(refpack_maximum_distance_and_match_length)
{
    const int distance = 131072, match = 1028;
    std::vector<unsigned char> body;
    for (int i = 0; i < distance;) {
        const int run = distance - i >= 112 ? 112 : distance - i;
        body.push_back((unsigned char)(0xe0 | ((run - 4) >> 2)));
        for (int j = 0; j < run; ++j, ++i) body.push_back((unsigned char)(i % 251));
    }
    body.insert(body.end(), {0xdc, 0xff, 0xff, 0xff, 0xfc});
    auto source = packet(0x90fb, distance + match, body);
    std::vector<unsigned char> dest(distance + match);
    CHECK_EQ(decode(source, dest), distance + match);
    for (int i = 0; i < distance; ++i) CHECK_EQ(dest[i], i % 251);
    for (int i = 0; i < match; ++i) CHECK_EQ(dest[distance + i], i % 251);
}
