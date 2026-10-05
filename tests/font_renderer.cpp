// Run on GitHub Actions, against the actual renderer and shipped font assets.
#include "common.h"
#include "nitroFont2.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

struct Font
{
    std::vector<u8> bytes;
    std::vector<nft2_glyph_t> glyphs;
    std::vector<u16> charmap;
    std::map<char16_t, int> indices;
    nft2_header_t header{};

    u32 number(size_t offset, int length) const
    {
        u32 value = 0;
        for (int i = 0; i < length; ++i)
            value |= u32(bytes.at(offset + i)) << (8 * i);
        return value;
    }

    explicit Font(const std::string& path)
    {
        std::ifstream input(path, std::ios::binary);
        assert(input.good());
        bytes.assign(std::istreambuf_iterator<char>(input), {});
        assert(number(0, 4) == NFT2_SIGNATURE);
        header.signature = NFT2_SIGNATURE;
        header.ascend = bytes.at(16);
        header.descend = bytes.at(17);
        header.glyphCount = number(18, 2);
        for (int i = 0; i < header.glyphCount; ++i)
        {
            auto offset = number(4, 4) + i * 8;
            nft2_glyph_t glyph{};
            glyph.dataOffset = number(offset, 3);
            glyph.glyphWidth = bytes.at(offset + 3);
            glyph.spacingLeft = s8(bytes.at(offset + 4));
            glyph.spacingRight = s8(bytes.at(offset + 5));
            glyph.glyphHeight = bytes.at(offset + 6);
            glyph.spacingTop = s8(bytes.at(offset + 7));
            glyphs.push_back(glyph);
        }
        auto mapStart = number(8, 4), bitmap = number(12, 4);
        for (auto offset = mapStart; offset < bitmap; offset += 2)
            charmap.push_back(number(offset, 2));
        for (size_t p = 0; charmap.at(p);)
        {
            int count = charmap.at(p++), start = charmap.at(p++);
            for (int i = 0; i < count; ++i)
            {
                int index = charmap.at(p++);
                if (index) indices[char16_t(start + i)] = index;
            }
        }
        header.glyphInfoPtr = glyphs.data();
        header.charMapPtr = reinterpret_cast<const nft2_char_map_entry_t*>(charmap.data());
        header.glyphDataPtr = bytes.data() + bitmap;
    }

    int index(char16_t c) const
    {
        auto found = indices.find(c);
        return found == indices.end() ? 0 : found->second;
    }

    int pixel(int index, int x, int y) const
    {
        const auto& glyph = glyphs.at(index);
        bool packed = glyph.dataOffset & NFT2_GLYPH_2BPP;
        int perByte = packed ? 4 : 2;
        int rowBytes = (glyph.glyphWidth + perByte - 1) / perByte;
        int value = header.glyphDataPtr[(glyph.dataOffset & NFT2_GLYPH_OFFSET_MASK)
                                       + y * rowBytes + x / perByte];
        for (int i = 0; i < x % perByte; ++i) value /= packed ? 4 : 16;
        return (value % (packed ? 4 : 16)) * (packed ? 5 : 1);
    }
};

static const Font* fallback;
static const Font& select(const Font& base, char16_t c)
{
    return !base.index(c) && fallback && fallback->index(c) ? *fallback : base;
}

static std::vector<u8> reference(const Font& base, const std::u16string& text,
                               const nft2_string_render_params_t& params)
{
    std::vector<u8> result(256 * 32);
    int x = params.x, y = params.y;
    for (auto c : text)
    {
        if (c == u'\n')
        {
            x = params.x;
            y += base.header.ascend + base.header.descend + 1;
            continue;
        }
        const auto& font = select(base, c);
        int index = font.index(c);
        const auto& glyph = font.glyphs.at(index);
        x += glyph.spacingLeft;
        for (int gy = 0; gy < glyph.glyphHeight; ++gy)
        for (int gx = 0; gx < int(glyph.glyphWidth); ++gx)
        {
            int dx = x + gx;
            int dy = y + glyph.spacingTop + base.header.ascend - font.header.ascend + gy;
            if (dx < 0 || dx >= int(params.width) || dy < 0 || dy >= int(params.height)) continue;
            if (params.onlyRenderWholeGlyphs && x + int(glyph.glyphWidth) > int(params.width)) continue;
            int coverage = font.pixel(index, gx, gy);
            if (coverage)
            {
                auto& previous = result.at(dy * 256 + dx);
                previous = params.a5i3 ? coverage : std::max<int>(previous, coverage);
            }
        }
        x += glyph.glyphWidth + glyph.spacingRight;
    }
    return result;
}

static std::vector<u8> render(const Font& font, const std::u16string& text,
                             nft2_string_render_params_t params, bool ellipsis = false)
{
    // Padding canaries detect out-of-bounds writes; sanitizers also check reads.
    std::vector<u8> bytes((params.a5i3 ? 256 * 32 : 256 * 16 / 2) + 128, 0xAB);
    std::fill(bytes.begin() + 64, bytes.end() - 64, 0);
    if (ellipsis)
        nft2_renderStringEllipsis(&font.header, text.c_str(), bytes.data() + 64, 256, &params, u" ... ");
    else
        nft2_renderString(&font.header, text.c_str(), bytes.data() + 64, 256, &params);
    assert(std::all_of(bytes.begin(), bytes.begin() + 64, [](u8 v) { return v == 0xAB; }));
    assert(std::all_of(bytes.end() - 64, bytes.end(), [](u8 v) { return v == 0xAB; }));
    std::vector<u8> result(256 * 32);
    for (unsigned y = 0; y < params.height; ++y)
    for (unsigned x = 0; x < params.width; ++x)
    {
        if (params.a5i3)
        {
            u8 value = bytes.at(64 + y * 256 + x);
            assert(!value || (value & 15) == 8);
            result[y * 256 + x] = value >> 4;
        }
        else
        {
            unsigned tile = (y / 16) * 256 + (x / 32) * 8 + (y / 8 % 2) * 4 + (x / 8 % 4);
            unsigned pixel = tile * 64 + (y % 8) * 8 + x % 8;
            result[y * 256 + x] = (bytes.at(64 + pixel / 2) >> (4 * (pixel % 2))) & 15;
        }
    }
    return result;
}

static int advance(const Font& base, char16_t c)
{
    const auto& font = select(base, c);
    const auto& glyph = font.glyphs.at(font.index(c));
    return glyph.spacingLeft + glyph.glyphWidth + glyph.spacingRight;
}

int main()
{
    const std::string root = "arm9/data/";
    Font small(root + "PicoSerifCJK-9.nft2"), regular(root + "PicoSerifCJK-12.nft2");
    int cases = 0;
    for (const auto& name : {"Regular-10", "Medium-10", "Medium-11", "Medium-7_5"})
    {
        Font base(root + "NotoSansJP-" + name + ".nft2");
        fallback = base.header.ascend <= 8 ? &small : &regular;
        nft2_string_render_params_t params{0, 0, 256, 16, 0, true};
        nft2_setFallbackFonts(nullptr, nullptr);
        auto legacy = render(base, u"File ABC 0123.nds", params);
        nft2_setFallbackFonts(&small.header, &regular.header);
        assert(legacy == render(base, u"File ABC 0123.nds", params));
        assert(legacy == reference(base, u"File ABC 0123.nds", params));
        assert(render(base, u"\u0378", params) == reference(base, u"\u0378", params));
        for (auto c : u"马里奥赛车汉化版寶可夢薩爾達傳說")
        {
            if (!c) continue;
            assert(!base.index(c) && fallback->index(c));
            u32 width, height;
            nft2_measureString(&base.header, std::u16string(2, c).c_str(), width, height);
            const auto& glyph = fallback->glyphs.at(fallback->index(c));
            assert(int(width) == 2 * advance(base, c) - glyph.spacingRight);
            assert(height == u32(base.header.ascend + base.header.descend));
        }
        for (bool a5i3 : {false, true})
        for (bool whole : {false, true})
        for (int x : {-300, -7, 0, 3})
        for (int y : {-20, -2, 0, 8, 30})
        {
            params = {x, y, 123, 16, 0, a5i3, whole};
            std::u16string text = u"马里奥赛车 DS 寶可夢 汉化版.nds";
            assert(render(base, text, params) == reference(base, text, params));
            ++cases;
        }
        // In a 96-pixel label this long title must preserve a short suffix.
        params = {0, 0, 96, 16, 0, true};
        std::u16string longText = u"精灵宝可梦白金中文版馬里奧赛车.nds";
        int dots = 0;
        for (auto c : std::u16string(u" ... ")) dots += advance(base, c);
        int leftBudget = 96 * 3 / 4 - dots / 2;
        int rightBudget = 96 - 96 * 3 / 4 - (dots + 1) / 2;
        std::u16string prefix, suffix;
        for (auto c : longText)
        {
            leftBudget -= advance(base, c);
            if (leftBudget < 0) break;
            prefix += c;
        }
        for (auto i = longText.size(); i > prefix.size();)
        {
            char16_t c = longText[--i];
            int width = advance(base, c);
            if (suffix.empty()) width -= select(base, c).glyphs.at(select(base, c).index(c)).spacingRight;
            if (width > rightBudget) break;
            rightBudget -= width;
            suffix.insert(suffix.begin(), c);
        }
        assert(!prefix.empty() && !suffix.empty() && prefix.size() + suffix.size() < longText.size());
        for (bool a5i3 : {false, true})
        {
            params.a5i3 = a5i3;
            assert(render(base, longText, params, true) == reference(base, prefix + u" ... " + suffix, params));
            assert(render(base, u"中文.nds", params, true) == render(base, u"中文.nds", params));
            ++cases;
        }
        params = {0, 0, 256, 32, 0, true};
        assert(render(base, u"中文\n繁體", params) == reference(base, u"中文\n繁體", params));
    }
    std::cout << cases << " mixed-font render cases passed; legacy, missing glyph, width, baseline,\n"
                 "2bpp/4bpp, tiled/A5I3, clipping, marquee and ellipsis checks passed.\n";
}
