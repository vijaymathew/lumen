// Unit test for raw::RawDecodeOptions serialization: the default-constructed value
// reproduces Lumen's historical decode behaviour (so old projects are unchanged),
// JSON round-trips faithfully, and fromJson on an empty object yields the defaults.

#include "core/RawLoader.h"

#include <QJsonObject>

#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main()
{
    using raw::RawDecodeOptions;

    // 1. Defaults == historical behaviour (auto-bright on @1%, clip, camera WB, AHD).
    {
        RawDecodeOptions d;
        CHECK(d.autoBright == true);
        CHECK(d.autoBrightThreshold > 0.0099f && d.autoBrightThreshold < 0.0101f);
        CHECK(d.highlight == 0);
        CHECK(d.wb == RawDecodeOptions::Camera);
        CHECK(d.demosaic == 3);
    }

    // 2. fromJson({}) yields the defaults (backward compat: old .lumen sans field).
    {
        CHECK(RawDecodeOptions::fromJson(QJsonObject{}) == RawDecodeOptions{});
    }

    // 3. Round-trip preserves every field.
    {
        RawDecodeOptions o;
        o.autoBright = false;
        o.autoBrightThreshold = 0.05f;
        o.highlight = 3;
        o.wb = RawDecodeOptions::Auto;
        o.demosaic = 4;
        RawDecodeOptions back = RawDecodeOptions::fromJson(o.toJson());
        CHECK(back == o);
        CHECK(!(back == RawDecodeOptions{})); // genuinely different from defaults
    }

    std::printf("rawoptions_test OK\n");
    return 0;
}
