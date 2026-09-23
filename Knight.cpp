#include "Knight.h"

#include <array>

namespace Knight
{
namespace
{
    using juce::Colour;
    using Layer = std::array<const char*, height>;   // rows of cells; '.' is transparent, missing rows are empty

    enum class Decor { none, plume, bigPlume, horns, crown };
    enum class Weapon { shortSword, longSword, greatSword };

    struct Look
    {
        const char* name;
        juce::uint32 armour, armourShade, visor;
        juce::uint32 tabard, tabardShade, tabardEmblem;
        juce::uint32 shield, shieldEmblem;
        Decor decor;
        juce::uint32 decorColour;
        Weapon weapon;
        juce::uint32 blade, bladeShade, guard, pommel;
        juce::uint32 glow;              // 0 = no glow
        juce::uint32 cape, capeShade;   // 0 = no cape
        bool flicker, rainbow, aura;
    };

    // One entry per rank. Each level up moves to the next one until the last.
    constexpr Look looks[] =
    {
        // name            armour      shade       visor       tabard      shade       emblem      shield      emblem      decor            decorColour  weapon               blade       shade       guard       pommel      glow        cape        capeShade   flicker rainbow aura
        { "SQUIRE",       0xffa0703f, 0xff6e4a26, 0xff1a1025, 0xff7d8a5a, 0xff5a6640, 0xff5a6640, 0xff9a6a3a, 0xff6e4a26, Decor::none,     0,          Weapon::shortSword,  0xffc49a62, 0xff8a6436, 0xff6e4a26, 0xff6e4a26, 0,          0,          0,          false,  false,  false },
        { "FOOTMAN",      0xffa9b1bf, 0xff6b7486, 0xff1a1025, 0xff8a3b3b, 0xff5e2626, 0xff5e2626, 0xff9a6a3a, 0xffa9b1bf, Decor::none,     0,          Weapon::shortSword,  0xffdfe5ee, 0xff96a0b0, 0xff6b7486, 0xff6b7486, 0,          0,          0,          false,  false,  false },
        { "KNIGHT",       0xffd5dcea, 0xff8490a8, 0xff1a1025, 0xff3f6fe0, 0xff2f4fb8, 0xffffc145, 0xffe8384f, 0xffffc145, Decor::plume,    0xffe8384f, Weapon::longSword,   0xfff4f7ff, 0xffa8b4c8, 0xffffc145, 0xffffc145, 0,          0,          0,          false,  false,  false },
        { "CAPTAIN",      0xffe3e8f2, 0xff8f9bb3, 0xff1a1025, 0xffc2263f, 0xff8a1a2d, 0xffffc145, 0xff3f6fe0, 0xffffc145, Decor::bigPlume, 0xff5bb8ff, Weapon::longSword,   0xfff4f7ff, 0xffa8b4c8, 0xffffc145, 0xffe8384f, 0,          0xff3f6fe0, 0xff2a4aa0, false,  false,  false },
        { "PALADIN",      0xffffd76a, 0xffc2912f, 0xff1a1025, 0xfff4f7ff, 0xffc4ccd9, 0xffffc145, 0xfff4f7ff, 0xffffc145, Decor::bigPlume, 0xfff4f7ff, Weapon::longSword,   0xffffffff, 0xffcfe8ff, 0xffffc145, 0xffffc145, 0x80fff3b0, 0xffc2263f, 0xff8a1a2d, false,  false,  false },
        { "DRAGONSLAYER", 0xff5a5a72, 0xff34344a, 0xffff4d2e, 0xff8a1a2d, 0xff5c1020, 0xffff9f40, 0xff34344a, 0xffe8384f, Decor::horns,    0xffe8e0c8, Weapon::greatSword,  0xffffd24a, 0xffff6b2e, 0xff34344a, 0xffe8384f, 0x99ff5a1e, 0xff3a1020, 0xff200810, true,   false,  false },
        { "MYTHIC",       0xffb8f0ff, 0xff5fb3d6, 0xff0a1a3a, 0xff8a4fff, 0xff5e2fc0, 0xff5ce1ff, 0xff8a4fff, 0xff5ce1ff, Decor::bigPlume, 0xffc38aff, Weapon::longSword,   0xffe0fbff, 0xff7fd8ff, 0xff8a4fff, 0xff5ce1ff, 0x8866ddff, 0xff5e2fc0, 0xff3d1c85, true,   false,  false },
        { "LEGEND",       0xffffe07a, 0xffd09a2a, 0xff1a1025, 0xffd62a4a, 0xff9a1530, 0xffffc145, 0xffffd76a, 0xffe8384f, Decor::crown,    0xffffc145, Weapon::greatSword,  0xffffffff, 0xffd0d0d0, 0xffffc145, 0xffe8384f, 0x80ffffff, 0xff7a2fd0, 0xff4e1a8e, false,  true,   true  },
    };

    constexpr int numLooks = (int) (sizeof (looks) / sizeof (looks[0]));

    //==============================================================================
    // K outline, S/s armour, V visor, B/b tabard, T tabard emblem, H shield, E shield emblem
    constexpr Layer body { {
        "",
        "",
        "....KKKKKKK",
        "...KSSSSSSsK",
        "...KSSSSSSsK",
        "...KSVVVVVVK",
        "...KSSSSSSsK",
        "...KsSSSSSsK",
        "....KKKKKKK",
        ".KHHHKbBBBBbKSSS",
        ".KHEHKbBTBBbKSSS",
        ".KEEEKbTTTBbK",
        ".KHEHKbBTBBbK",
        "..KHKKbBBBBbK",
        "...K.KKKKKKK",
        ".....KssKssK",
        ".....KssKssK",
        "....KKKK.KKKK",
    } };

    constexpr Layer cape { {
        "", "", "", "", "", "", "", "",
        "...CC",
        "C",
        "C",
        "C",
        "C",
        "CC",
        "cCC",
        ".cCCC",
        "..cCC",
    } };

    // D decoration colour, R gem
    constexpr Layer plume    { { "......DDD", ".....DDDD" } };
    constexpr Layer bigPlume { { "....DDDDD", "...DDDDDD", "..DD", "..D" } };
    constexpr Layer horns    { { ".D...........D", "..D.........D", "..DD.......DD", "..D.........D" } };
    constexpr Layer crown    { { "....D..D..D", "....DDDRDDD" } };

    // W/w blade, X guard, O pommel, K fist outline, x swing trail
    constexpr Layer shortIdle { {
        "", "", "",
        "..............W",
        "..............Ww",
        "..............Ww",
        "..............Ww",
        "..............Ww",
        ".............XXXX",
        "................K",
        "................K",
        "..............OO",
    } };

    constexpr Layer shortAttack { {
        "", "", "", "",
        ".................xx",
        "...................xx",
        ".....................x",
        "......................x",
        "................X",
        "................XWWWWW",
        "................Xwwww",
        "................X",
    } };

    constexpr Layer longIdle { {
        "..............W",
        "..............Ww",
        "..............Ww",
        "..............Ww",
        "..............Ww",
        "..............Ww",
        "..............Ww",
        "..............Ww",
        "............XXXXX",
        "................K",
        "................K",
        "..............OO",
    } };

    constexpr Layer longAttack { {
        "", "", "", "",
        ".................xx",
        "...................xx",
        ".....................x",
        "......................x",
        "................X",
        "................XWWWWWWW",
        "................Xwwwwww",
        "................X",
    } };

    constexpr Layer greatIdle { {
        "...............W",
        "..............WWw",
        "..............WWw",
        "..............WWw",
        "..............WWw",
        "..............WWw",
        "..............WWw",
        "..............WWw",
        "...........XXXXXXX",
        "................K",
        "................K",
        "..............OO",
        "..............OO",
    } };

    constexpr Layer greatAttack { {
        "", "", "", "",
        ".................xx",
        "...................xx",
        ".....................x",
        "................X.....x",
        "................XWWWWWW",
        "................XWWWWWWW",
        "................Xwwwwww",
        "................X",
        "................X",
    } };

    //==============================================================================
    const Look& lookFor (int level)
    {
        return looks[juce::jlimit (0, numLooks - 1, level - 1)];
    }

    template <typename Fn>
    void forEachCell (const Layer& layer, juce::Point<float> topLeft, float pixelSize, Fn&& fn)
    {
        for (int row = 0; row < height; ++row)
            if (const char* line = layer[(size_t) row])
                for (int column = 0; column < width && line[column] != 0; ++column)
                    if (line[column] != '.')
                        fn (line[column], column,
                            juce::Rectangle<float> (topLeft.x + (float) column * pixelSize,
                                                    topLeft.y + (float) row * pixelSize,
                                                    pixelSize, pixelSize));
    }

    Colour colourFor (const Look& look, char c, int column, double timeMs)
    {
        if (look.rainbow && (c == 'W' || c == 'w'))
        {
            const auto hue = (float) std::fmod (timeMs / 1200.0 + column * 0.06, 1.0);
            return c == 'W' ? Colour::fromHSV (hue, 0.45f, 1.0f, 1.0f)
                            : Colour::fromHSV (hue, 0.65f, 0.8f, 1.0f);
        }

        switch (c)
        {
            case 'K': return Colour (0xff1a1025);
            case 'S': return Colour (look.armour);
            case 's': return Colour (look.armourShade);
            case 'V': return Colour (look.visor);
            case 'B': return Colour (look.tabard);
            case 'b': return Colour (look.tabardShade);
            case 'T': return Colour (look.tabardEmblem);
            case 'H': return Colour (look.shield);
            case 'E': return Colour (look.shieldEmblem);
            case 'D': return Colour (look.decorColour);
            case 'R': return Colour (0xffe8384f);
            case 'W': return Colour (look.blade);
            case 'w': return Colour (look.bladeShade);
            case 'X': return Colour (look.guard);
            case 'O': return Colour (look.pommel);
            case 'C': return Colour (look.cape);
            case 'c': return Colour (look.capeShade);
            case 'x': return juce::Colours::white.withAlpha (0.5f);
            default:  return juce::Colours::transparentBlack;
        }
    }

    //==============================================================================
    // Scenery helpers. Everything snaps to a small cell grid to keep the pixel-art look.
    void bandedSky (juce::Graphics& g, juce::Rectangle<float> area, Colour top, Colour bottom)
    {
        constexpr float band = 6.0f;
        const int numBands = (int) std::ceil (area.getHeight() / band);

        for (int i = 0; i < numBands; ++i)
        {
            g.setColour (top.interpolatedWith (bottom, (float) i / (float) juce::jmax (1, numBands - 1)));
            g.fillRect (area.getX(), area.getY() + (float) i * band, area.getWidth(), band);
        }
    }

    void pixelDisc (juce::Graphics& g, juce::Point<float> centre, int radiusCells, float cell)
    {
        for (int row = -radiusCells; row < radiusCells; ++row)
        {
            for (int column = -radiusCells; column < radiusCells; ++column)
            {
                const float dx = (float) column + 0.5f, dy = (float) row + 0.5f;

                if (dx * dx + dy * dy <= (float) (radiusCells * radiusCells))
                    g.fillRect (centre.x + (float) column * cell, centre.y + (float) row * cell, cell, cell);
            }
        }
    }

    void stars (juce::Graphics& g, juce::Rectangle<float> area, int count, juce::int64 seed, double timeMs, Colour colour)
    {
        juce::Random random (seed);

        for (int i = 0; i < count; ++i)
        {
            const float x = std::round (area.getX() + random.nextFloat() * area.getWidth());
            const float y = std::round (area.getY() + random.nextFloat() * area.getHeight());
            const float size = random.nextFloat() < 0.8f ? 2.0f : 3.0f;
            const float twinkle = 0.55f + 0.45f * (float) std::sin (timeMs * 0.003 + i * 1.7);

            g.setColour (colour.withAlpha ((0.3f + 0.5f * random.nextFloat()) * twinkle));
            g.fillRect (x, y, size, size);
        }
    }

    void hills (juce::Graphics& g, juce::Rectangle<float> area, float baseY, float amplitude, float frequency, float phase)
    {
        constexpr float cell = 4.0f;

        for (float x = area.getX(); x < area.getRight(); x += cell)
        {
            const float rise = amplitude * (0.5f + 0.5f * std::sin ((x - area.getX()) * frequency + phase));
            const float top = std::round ((baseY - rise) / cell) * cell;
            g.fillRect (x, top, cell, area.getBottom() - top);
        }
    }

    void cloud (juce::Graphics& g, float x, float y)
    {
        constexpr float cell = 4.0f;
        g.fillRect (x + 2.0f * cell, y, 4.0f * cell, cell);
        g.fillRect (x + cell, y + cell, 7.0f * cell, cell);
        g.fillRect (x, y + 2.0f * cell, 10.0f * cell, 2.0f * cell);
    }

    void brickWall (juce::Graphics& g, juce::Rectangle<float> wall, Colour stone, bool merlons)
    {
        g.setColour (stone);
        g.fillRect (wall);

        if (merlons)
            for (float x = wall.getX() + 2.0f; x < wall.getRight(); x += 28.0f)
                g.fillRect (x, wall.getY() - 12.0f, 14.0f, 12.0f);

        g.setColour (stone.darker (0.5f));

        for (int row = 0; wall.getY() + 14.0f * (float) row < wall.getBottom(); ++row)
        {
            const float y = wall.getY() + 14.0f * (float) row;
            g.fillRect (wall.getX(), y, wall.getWidth(), 2.0f);

            for (float x = wall.getX() + (row % 2 == 0 ? 10.0f : 24.0f); x < wall.getRight(); x += 28.0f)
                g.fillRect (x, y, 2.0f, 14.0f);
        }
    }

    void speckles (juce::Graphics& g, juce::Rectangle<float> area, int count, juce::int64 seed, Colour colour)
    {
        juce::Random random (seed);
        g.setColour (colour);

        for (int i = 0; i < count; ++i)
            g.fillRect (std::round (area.getX() + random.nextFloat() * area.getWidth()),
                        std::round (area.getY() + random.nextFloat() * area.getHeight()), 4.0f, 2.0f);
    }

    //==============================================================================
    void villageAtDawn (juce::Graphics& g, juce::Rectangle<float> area, float groundY)
    {
        bandedSky (g, area, Colour (0xff4a6fb5), Colour (0xffffc58a));

        g.setColour (Colour (0xffffe7a0));
        pixelDisc (g, { area.getRight() - 48.0f, groundY - 34.0f }, 7, 3.0f);

        g.setColour (Colour (0xff7fae9a));
        hills (g, area, groundY - 26.0f, 22.0f, 0.045f, 1.0f);
        g.setColour (Colour (0xff4f9a4a));
        hills (g, area, groundY - 6.0f, 12.0f, 0.07f, 3.0f);

        // Cottage
        const float hx = area.getX() + 14.0f, hy = groundY - 30.0f;
        g.setColour (Colour (0xffc9a06a));
        g.fillRect (hx, hy, 36.0f, 30.0f);
        g.setColour (Colour (0xffb04a3a));

        for (int i = 0; i < 5; ++i)
            g.fillRect (hx - 4.0f + (float) i * 4.0f, hy - 4.0f - (float) i * 4.0f, 44.0f - (float) i * 8.0f, 4.0f);

        g.setColour (Colour (0xffffd86a));
        g.fillRect (hx + 6.0f, hy + 8.0f, 8.0f, 8.0f);
        g.setColour (Colour (0xff6e4a26));
        g.fillRect (hx + 22.0f, hy + 12.0f, 8.0f, 18.0f);

        // Fence
        g.setColour (Colour (0xff9a6a3a));

        for (float x = area.getX() + 4.0f; x < area.getRight(); x += 24.0f)
            g.fillRect (x, groundY - 16.0f, 4.0f, 16.0f);

        g.fillRect (area.getX(), groundY - 13.0f, area.getWidth(), 3.0f);
        g.fillRect (area.getX(), groundY - 7.0f, area.getWidth(), 3.0f);

        // Grass and dirt path
        g.setColour (Colour (0xff5cae4a));
        g.fillRect (area.withTop (groundY).withHeight (6.0f));
        g.setColour (Colour (0xff8a6a3a));
        g.fillRect (area.withTop (groundY + 6.0f));
        speckles (g, area.withTop (groundY + 8.0f), 30, 11, Colour (0xff6e5230));
    }

    void trainingYard (juce::Graphics& g, juce::Rectangle<float> area, float groundY, double timeMs)
    {
        bandedSky (g, area, Colour (0xff4f9dff), Colour (0xffc4e8ff));

        g.setColour (Colour (0xfffff2a8));
        pixelDisc (g, { area.getX() + 36.0f, area.getY() + 72.0f }, 5, 3.0f);

        g.setColour (juce::Colours::white.withAlpha (0.9f));

        for (int i = 0; i < 3; ++i)
        {
            const double span = area.getWidth() + 50.0;
            const double x = area.getX() - 44.0 + std::fmod (i * 83.0 + timeMs * 0.004 * (1.0 + i * 0.3), span);
            cloud (g, std::round ((float) x), area.getY() + 56.0f + (float) i * 22.0f);
        }

        // Wooden palisade
        const float wallTop = groundY - 64.0f;

        for (float x = area.getX(); x < area.getRight(); x += 10.0f)
        {
            const bool alternate = ((int) ((x - area.getX()) / 10.0f)) % 2 == 0;
            g.setColour (alternate ? Colour (0xff9a6a3a) : Colour (0xff85592f));
            g.fillRect (x, wallTop + 6.0f, 10.0f, groundY - wallTop - 6.0f);
            g.fillRect (x + 2.0f, wallTop + 2.0f, 6.0f, 4.0f);
            g.fillRect (x + 4.0f, wallTop - 2.0f, 2.0f, 4.0f);
            g.setColour (Colour (0xff5a3a1c));
            g.fillRect (x + 9.0f, wallTop + 6.0f, 1.0f, groundY - wallTop - 6.0f);
        }

        g.setColour (Colour (0xff5a3a1c));
        g.fillRect (area.getX(), wallTop + 16.0f, area.getWidth(), 3.0f);
        g.fillRect (area.getX(), groundY - 16.0f, area.getWidth(), 3.0f);

        // Practice target
        const juce::Point<float> target (area.getRight() - 30.0f, groundY - 38.0f);
        g.setColour (Colour (0xff6e4a26));
        g.fillRect (target.x - 2.0f, target.y, 4.0f, groundY - target.y);

        for (int ring = 4; ring >= 1; --ring)
        {
            g.setColour (ring % 2 == 0 ? juce::Colours::white : Colour (0xffe8384f));
            pixelDisc (g, target, ring, 3.0f);
        }

        g.setColour (Colour (0xffb08850));
        g.fillRect (area.withTop (groundY));
        speckles (g, area.withTop (groundY + 4.0f), 30, 12, Colour (0xff8a6a3a));
    }

    void castleAtNight (juce::Graphics& g, juce::Rectangle<float> area, float groundY, double timeMs)
    {
        bandedSky (g, area, Colour (0xff0a1236), Colour (0xff2a4585));
        stars (g, area.withTrimmedTop (44.0f).withHeight (area.getHeight() * 0.45f), 26, 5, timeMs, Colour (0xffdce8ff));

        const juce::Point<float> moon (area.getRight() - 34.0f, area.getY() + 70.0f);
        g.setColour (Colour (0xfff5ecc2));
        pixelDisc (g, moon, 6, 3.0f);
        g.setColour (Colour (0xffcfc08a));

        for (const auto& crater : { juce::Point<int> (-2, -1), { 1, 2 }, { 2, -3 } })
            g.fillRect (moon.x + (float) crater.x * 3.0f, moon.y + (float) crater.y * 3.0f, 3.0f, 3.0f);

        brickWall (g, area.withTop (groundY), Colour (0xff2a3a66), true);
    }

    void battlementsAtSunset (juce::Graphics& g, juce::Rectangle<float> area, float groundY, double timeMs)
    {
        bandedSky (g, area, Colour (0xff3a2a6a), Colour (0xffff8a4a));

        // Striped retro sun
        const juce::Point<float> sun (area.getCentreX() + 30.0f, groundY - 40.0f);
        g.setColour (Colour (0xffffc85a));
        pixelDisc (g, sun, 9, 3.0f);
        g.setColour (Colour (0xffef7d48));

        for (int i = 0; i < 4; ++i)
            g.fillRect (sun.x - 27.0f, sun.y + 3.0f + (float) i * 6.0f, 54.0f, i < 2 ? 2.0f : 3.0f);

        // Towers with waving flags and banners
        auto tower = [&] (float x, float w, float h)
        {
            g.setColour (Colour (0xff2a1a3a));
            g.fillRect (x, groundY - h, w, h);

            for (float mx = x; mx < x + w; mx += 8.0f)
                g.fillRect (mx, groundY - h - 6.0f, 4.0f, 6.0f);

            const float poleX = x + w * 0.5f - 1.0f, poleTop = groundY - h - 26.0f;
            g.setColour (Colour (0xff1a1025));
            g.fillRect (poleX, poleTop, 2.0f, 20.0f);

            g.setColour (Colour (0xff3f6fe0));

            for (int c = 0; c < 5; ++c)
                g.fillRect (poleX + 2.0f + (float) c * 3.0f,
                            poleTop + std::round ((float) std::sin (timeMs * 0.006 + c * 0.9 + x) * 1.5f), 3.0f, 9.0f);

            g.fillRect (x + w * 0.5f - 6.0f, groundY - h + 10.0f, 12.0f, 26.0f);
            g.fillRect (x + w * 0.5f - 6.0f, groundY - h + 36.0f, 4.0f, 4.0f);
            g.fillRect (x + w * 0.5f + 2.0f, groundY - h + 36.0f, 4.0f, 4.0f);
            g.setColour (Colour (0xffffc145));
            g.fillRect (x + w * 0.5f - 2.0f, groundY - h + 18.0f, 4.0f, 8.0f);
        };

        tower (area.getX() + 6.0f, 28.0f, 110.0f);
        tower (area.getRight() - 38.0f, 32.0f, 86.0f);

        brickWall (g, area.withTop (groundY), Colour (0xff5a4a6a), true);
    }

    void holySanctum (juce::Graphics& g, juce::Rectangle<float> area, float groundY, double timeMs)
    {
        bandedSky (g, area, Colour (0xffffe3a0), Colour (0xfffff8e6));

        // Light shafts
        for (int i = 0; i < 5; ++i)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f + 0.08f * (float) std::sin (timeMs * 0.002 + i)));
            g.fillRect (area.getX() + 14.0f + (float) i * 40.0f, area.getY(), 14.0f, groundY - area.getY());
        }

        // Halo behind the knight's head
        const juce::Point<float> head (area.getCentreX() - 9.0f, groundY - 74.0f);
        g.setGradientFill (juce::ColourGradient (Colour (0xccffffff), head.x, head.y,
                                                 Colour (0x00ffffff), head.x + 56.0f, head.y, true));
        g.fillEllipse (juce::Rectangle<float> (112.0f, 112.0f).withCentre (head));

        // Clouds drifting behind the floor
        g.setColour (juce::Colours::white);

        for (int i = 0; i < 4; ++i)
            cloud (g, area.getX() - 10.0f + (float) i * 52.0f
                          + std::round ((float) std::sin (timeMs * 0.001 + i) * 4.0f),
                   groundY - 14.0f);

        // Marble pillars
        auto pillar = [&] (float x)
        {
            g.setColour (Colour (0xffeae6f5));
            g.fillRect (x, groundY - 150.0f, 20.0f, 150.0f);
            g.setColour (Colour (0xffc9c2dc));
            g.fillRect (x + 14.0f, groundY - 150.0f, 6.0f, 150.0f);
            g.fillRect (x + 6.0f, groundY - 146.0f, 2.0f, 140.0f);
            g.setColour (Colour (0xffffd76a));
            g.fillRect (x - 4.0f, groundY - 156.0f, 28.0f, 6.0f);
            g.fillRect (x - 4.0f, groundY - 6.0f, 28.0f, 6.0f);
        };

        pillar (area.getX() + 8.0f);
        pillar (area.getRight() - 28.0f);

        // Checkered marble floor with gold trim
        const auto floor = area.withTop (groundY);

        for (int row = 0; floor.getY() + (float) row * 14.0f < floor.getBottom(); ++row)
        {
            for (int column = 0; floor.getX() + (float) column * 14.0f < floor.getRight(); ++column)
            {
                g.setColour ((row + column) % 2 == 0 ? Colour (0xfff4f0fa) : Colour (0xffd9d2ea));
                g.fillRect (floor.getX() + (float) column * 14.0f, floor.getY() + (float) row * 14.0f, 14.0f, 14.0f);
            }
        }

        g.setColour (Colour (0xffffc145));
        g.fillRect (floor.withHeight (3.0f));
    }

    void volcanoLair (juce::Graphics& g, juce::Rectangle<float> area, float groundY, double timeMs)
    {
        bandedSky (g, area, Colour (0xff12040a), Colour (0xff7a1e12));

        const float pulse = 0.75f + 0.25f * (float) std::sin (timeMs * 0.004);
        const float peakY = groundY - 120.0f, cx = area.getCentreX() + 20.0f;

        // Glow over the crater
        g.setGradientFill (juce::ColourGradient (Colour (0xffff6b2e).withAlpha (0.5f * pulse), cx, peakY,
                                                 Colour (0x00ff6b2e), cx, peakY - 60.0f, true));
        g.fillEllipse (juce::Rectangle<float> (140.0f, 120.0f).withCentre ({ cx, peakY }));

        // Volcano
        g.setColour (Colour (0xff2a0d12));

        for (float y = peakY; y < groundY; y += 4.0f)
        {
            const float t = (y - peakY) / (groundY - peakY);
            const float half = std::round ((18.0f + 92.0f * t) / 4.0f) * 4.0f;
            g.fillRect (cx - half, y, half * 2.0f, 4.0f);
        }

        g.setColour (Colour (0xffff6b2e).withAlpha (pulse));
        g.fillRect (cx - 14.0f, peakY, 28.0f, 4.0f);

        for (int i = 0; i < 10; ++i)
            g.fillRect (cx - 6.0f + (float) (i % 2) * 4.0f, peakY + 4.0f + (float) i * 8.0f, 4.0f, 8.0f);

        // Rising embers
        const float span = groundY - area.getY();

        for (int i = 0; i < 14; ++i)
        {
            const double speed = 0.03 + (i % 5) * 0.008;
            const float y = groundY - (float) std::fmod (timeMs * speed + i * 37.0, (double) span);
            const float x = area.getX() + (float) std::fmod (i * 53.0 + std::sin (timeMs * 0.002 + i) * 6.0 + 400.0, (double) area.getWidth());

            g.setColour ((i % 3 == 0 ? Colour (0xffffd24a) : Colour (0xffff6b2e)).withAlpha (0.8f));
            g.fillRect (std::round (x), std::round (y), 3.0f, 3.0f);
        }

        // Basalt with glowing cracks
        const auto ground = area.withTop (groundY);
        g.setColour (Colour (0xff2a2025));
        g.fillRect (ground);
        g.setColour (Colour (0xff3e3038));
        g.fillRect (ground.withHeight (4.0f));

        g.setColour (Colour (0xffff6b2e).withAlpha (pulse));

        for (const auto& crack : { juce::Point<float> (14.0f, 12.0f), { 70.0f, 26.0f }, { 128.0f, 10.0f }, { 160.0f, 34.0f } })
        {
            g.fillRect (ground.getX() + crack.x, ground.getY() + crack.y, 12.0f, 2.0f);
            g.fillRect (ground.getX() + crack.x + 10.0f, ground.getY() + crack.y + 2.0f, 10.0f, 2.0f);
            g.fillRect (ground.getX() + crack.x + 18.0f, ground.getY() + crack.y + 4.0f, 6.0f, 2.0f);
        }
    }

    void auroraPeaks (juce::Graphics& g, juce::Rectangle<float> area, float groundY, double timeMs)
    {
        bandedSky (g, area, Colour (0xff031420), Colour (0xff0d3a4c));
        stars (g, area.withTrimmedTop (44.0f).withHeight (area.getHeight() * 0.5f), 20, 17, timeMs, juce::Colours::white);

        const Colour ribbons[] = { Colour (0xff5ce1a6), Colour (0xff5ce1ff), Colour (0xffb18cff) };

        for (int band = 0; band < 3; ++band)
        {
            for (float x = area.getX(); x < area.getRight(); x += 4.0f)
            {
                const float y = area.getY() + 58.0f + (float) band * 14.0f
                              + 10.0f * (float) std::sin (x * 0.05f + timeMs * 0.0015 + (double) band);
                const float h = 18.0f + 8.0f * (float) std::sin (x * 0.08f + timeMs * 0.002 + (double) band * 2.0);

                g.setColour (ribbons[band].withAlpha (0.2f));
                g.fillRect (x, std::round (y / 4.0f) * 4.0f, 4.0f, std::round (h / 4.0f) * 4.0f);
            }
        }

        auto crystal = [&] (float cx, float h, float w, Colour c)
        {
            for (float y = 0.0f; y < h; y += 4.0f)
            {
                const float half = juce::jmax (2.0f, std::round (w * 0.5f * (y + 4.0f) / h / 2.0f) * 2.0f);
                const float top = groundY - h + y;

                g.setColour (c);
                g.fillRect (cx - half, top, half, 4.0f);
                g.setColour (c.darker (0.45f));
                g.fillRect (cx, top, half, 4.0f);
            }

            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (cx - 2.0f, groundY - h, 2.0f, 4.0f);
        };

        crystal (area.getX() + 20.0f, 90.0f, 26.0f, Colour (0xff7fd8ff));
        crystal (area.getX() + 44.0f, 56.0f, 18.0f, Colour (0xffb18cff));
        crystal (area.getRight() - 24.0f, 110.0f, 30.0f, Colour (0xffb18cff));
        crystal (area.getRight() - 50.0f, 60.0f, 18.0f, Colour (0xff7fd8ff));

        const auto ice = area.withTop (groundY);
        g.setColour (Colour (0xff9fdcf5));
        g.fillRect (ice);
        g.setColour (Colour (0xffdff6ff));
        g.fillRect (ice.withHeight (4.0f));
        speckles (g, ice.withTrimmedTop (6.0f), 16, 23, Colour (0xff6fb8dc));
    }

    void celestialThrone (juce::Graphics& g, juce::Rectangle<float> area, float groundY, double timeMs)
    {
        bandedSky (g, area, Colour (0xff0c0320), Colour (0xff3a0f6a));
        stars (g, area.withBottom (groundY), 60, 21, timeMs, juce::Colours::white);

        // Ringed planet: back half of the ring, planet, front half
        const juce::Point<float> planet (area.getRight() - 46.0f, area.getY() + 84.0f);

        auto ring = [&] (bool front)
        {
            g.setColour (Colour (0xffffe07a));

            for (int i = 0; i < 48; ++i)
            {
                const float angle = juce::MathConstants<float>::twoPi * (float) i / 48.0f;
                const float y = std::sin (angle) * 6.0f;

                if ((y >= 0.0f) == front)
                    g.fillRect (std::round ((planet.x + std::cos (angle) * 30.0f) / 3.0f) * 3.0f,
                                std::round ((planet.y + y) / 3.0f) * 3.0f, 3.0f, 3.0f);
            }
        };

        ring (false);
        g.setColour (Colour (0xffff9fd0));
        pixelDisc (g, planet, 6, 3.0f);
        g.setColour (Colour (0xffd66fb0));
        g.fillRect (planet.x - 15.0f, planet.y + 6.0f, 30.0f, 6.0f);
        ring (true);

        // A shooting star every few seconds
        const double phase = std::fmod (timeMs, 4500.0) / 4500.0;

        if (phase < 0.2)
        {
            const float t = (float) (phase / 0.2);
            const juce::Point<float> head (area.getX() + 20.0f + 120.0f * t, area.getY() + 50.0f + 50.0f * t);

            for (int i = 0; i < 6; ++i)
            {
                g.setColour (juce::Colours::white.withAlpha (1.0f - (float) i / 6.0f));
                g.fillRect (std::round (head.x - (float) i * 4.0f), std::round (head.y - (float) i * 2.0f), 3.0f, 3.0f);
            }
        }

        // Floating golden platform with a rainbow edge
        const auto floor = area.withTop (groundY);
        g.setColour (Colour (0xffd09a2a));
        g.fillRect (floor);
        g.setColour (Colour (0xffffd76a));
        g.fillRect (floor.withHeight (6.0f));
        g.setColour (Colour (0xffb07f1e));

        for (float x = floor.getX() + 16.0f; x < floor.getRight(); x += 16.0f)
            g.fillRect (x, floor.getY() + 8.0f, 2.0f, floor.getHeight() - 8.0f);

        for (float x = floor.getX(); x < floor.getRight(); x += 4.0f)
        {
            const auto hue = (float) std::fmod ((x - floor.getX()) * 0.01 + timeMs * 0.0005, 1.0);
            g.setColour (Colour::fromHSV (hue, 0.5f, 1.0f, 0.9f));
            g.fillRect (x, floor.getY() + 6.0f, 4.0f, 2.0f);
        }
    }
}

//==============================================================================
int getNumRanks()
{
    return numLooks;
}

int getRankIndex (int level)
{
    return juce::jlimit (0, numLooks - 1, level - 1);
}

juce::String getRankName (int level)
{
    return lookFor (level).name;
}

void draw (juce::Graphics& g, juce::Point<float> topLeft, float pixelSize, int level, bool attacking, double timeMs)
{
    const auto& look = lookFor (level);

    const Layer* decor = nullptr;

    switch (look.decor)
    {
        case Decor::plume:    decor = &plume;    break;
        case Decor::bigPlume: decor = &bigPlume; break;
        case Decor::horns:    decor = &horns;    break;
        case Decor::crown:    decor = &crown;    break;
        case Decor::none:     break;
    }

    const Layer* weapon = nullptr;

    switch (look.weapon)
    {
        case Weapon::shortSword: weapon = attacking ? &shortAttack : &shortIdle; break;
        case Weapon::longSword:  weapon = attacking ? &longAttack  : &longIdle;  break;
        case Weapon::greatSword: weapon = attacking ? &greatAttack : &greatIdle; break;
    }

    std::vector<const Layer*> layers;

    if (look.cape != 0)
        layers.push_back (&cape);

    layers.push_back (&body);

    if (decor != nullptr)
        layers.push_back (decor);

    // Soft drop shadow under everything
    g.setColour (juce::Colours::black.withAlpha (0.3f));

    for (const auto* layer : layers)
        forEachCell (*layer, topLeft, pixelSize, [&] (char, int, juce::Rectangle<float> cell)
        {
            g.fillRect (cell.translated (pixelSize * 0.5f, pixelSize * 0.5f));
        });

    forEachCell (*weapon, topLeft, pixelSize, [&] (char c, int, juce::Rectangle<float> cell)
    {
        if (c != 'x')
            g.fillRect (cell.translated (pixelSize * 0.5f, pixelSize * 0.5f));
    });

    auto paintCell = [&] (char c, int column, juce::Rectangle<float> cell)
    {
        g.setColour (colourFor (look, c, column, timeMs));
        g.fillRect (cell);
    };

    for (const auto* layer : layers)
        forEachCell (*layer, topLeft, pixelSize, paintCell);

    if (look.glow != 0)
    {
        const float strength = look.flicker ? 0.6f + 0.4f * (float) std::sin (timeMs * 0.025) : 1.0f;
        g.setColour (Colour (look.glow).withMultipliedAlpha (strength));

        forEachCell (*weapon, topLeft, pixelSize, [&] (char c, int, juce::Rectangle<float> cell)
        {
            if (c == 'W' || c == 'w')
                g.fillRect (cell.expanded (pixelSize * 0.6f));
        });
    }

    forEachCell (*weapon, topLeft, pixelSize, paintCell);

    if (look.aura)
        drawSparkles (g, juce::Rectangle<float> (topLeft.x, topLeft.y, (float) width * pixelSize, (float) height * pixelSize)
                             .expanded (pixelSize * 2.0f),
                      pixelSize, 6, timeMs, 7);
}

void drawSparkles (juce::Graphics& g, juce::Rectangle<float> area, float pixelSize, int count, double timeMs, juce::int64 seed)
{
    for (int i = 0; i < count; ++i)
    {
        // Each sparkle lives for one cycle at a random spot, growing and then shrinking
        const double t = timeMs / 450.0 + (double) i / (double) count;
        const auto cycle = (juce::int64) std::floor (t);
        const auto bump = 1.0f - std::abs (2.0f * (float) (t - (double) cycle) - 1.0f);

        juce::Random random (seed * 1000003 + cycle * 7919 + i * 104729);
        const float x = area.getX() + random.nextFloat() * area.getWidth();
        const float y = area.getY() + random.nextFloat() * area.getHeight();

        const float thickness = pixelSize * 0.5f;
        const float arm = std::round (pixelSize * (0.5f + 1.2f * bump));

        g.setColour ((i % 2 == 0 ? Colour (0xffffe07a) : juce::Colours::white).withAlpha (bump));
        g.fillRect (juce::Rectangle<float> (arm * 2.0f, thickness).withCentre ({ x, y }));
        g.fillRect (juce::Rectangle<float> (thickness, arm * 2.0f).withCentre ({ x, y }));
    }
}

void drawScenery (juce::Graphics& g, juce::Rectangle<float> area, float groundY, int level, double timeMs)
{
    switch (getRankIndex (level))
    {
        case 0:  villageAtDawn (g, area, groundY);                break;
        case 1:  trainingYard (g, area, groundY, timeMs);         break;
        case 2:  castleAtNight (g, area, groundY, timeMs);        break;
        case 3:  battlementsAtSunset (g, area, groundY, timeMs);  break;
        case 4:  holySanctum (g, area, groundY, timeMs);          break;
        case 5:  volcanoLair (g, area, groundY, timeMs);          break;
        case 6:  auroraPeaks (g, area, groundY, timeMs);          break;
        default: celestialThrone (g, area, groundY, timeMs);      break;
    }
}
}
