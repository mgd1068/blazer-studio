#include "barcode.h"
#include <array>

namespace {
// Pattern data checked against ZXing (copyright 2008 ZXing authors, Apache-2.0).
// See THIRD_PARTY_NOTICES.md and LICENSES/Apache-2.0.txt.
// Code 128 symbol run widths (bar, space, ...), in integral modules.
// Standard symbol data; checked against ZXing Code128Reader, 2026-11-21:
// https://github.com/zxing/zxing/blob/master/core/src/main/java/com/google/zxing/oned/Code128Reader.java
constexpr std::array<const char *, 107> patterns{
    "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312",
    "132212", "221213", "221312", "231212", "112232", "122132", "122231", "113222",
    "123122", "123221", "223211", "221132", "221231", "213212", "223112", "312131",
    "311222", "321122", "321221", "312212", "322112", "322211", "212123", "212321",
    "232121", "111323", "131123", "131321", "112313", "132113", "132311", "211313",
    "231113", "231311", "112133", "112331", "132131", "113123", "113321", "133121",
    "313121", "211331", "231131", "213113", "213311", "213131", "311123", "311321",
    "331121", "312113", "312311", "332111", "314111", "221411", "431111", "111224",
    "111422", "121124", "121421", "141122", "141221", "112214", "112412", "122114",
    "122411", "142112", "142211", "241211", "221114", "413111", "241112", "134111",
    "111242", "121142", "121241", "114212", "124112", "124211", "411212", "421112",
    "421211", "212141", "214121", "412121", "111143", "111341", "131141", "114113",
    "114311", "411113", "411311", "113141", "114131", "311141", "411131", "211412",
    "211214", "211232", "2331112"};
}

Barcode::Result Barcode::code128B(const QString &text) {
    Result result;
    if (text.isEmpty() || text.size() > maximumCharacters) {
        result.error = "Code 128-B benötigt 1 bis 128 Zeichen.";
        return result;
    }
    for (auto character : text)
        if (character.unicode() < 32 || character.unicode() > 126) {
            result.error = "Code 128-B unterstützt hier nur druckbares ASCII (32–126).";
            return result;
        }
    // Start and checksum: 11 modules each; Stop: 13; every data character: 11.
    result.modules = quietModules * 2 + 35 + 11 * int(text.size());
    result.path.setFillRule(Qt::WindingFill);
    int position = quietModules;
    auto append = [&](int symbol) {
        bool bar = true;
        for (const char *run = patterns[symbol]; *run; ++run) {
            const int width = *run - '0';
            if (bar)
                result.path.addRect(QRectF(double(position) / result.modules, 0,
                                           double(width) / result.modules, 1));
            position += width;
            bar = !bar;
        }
    };
    append(104); // Start B.
    int checksum = 104;
    for (int i = 0; i < text.size(); ++i) {
        const int symbol = text[i].unicode() - 32;
        checksum += (i + 1) * symbol;
        append(symbol);
    }
    append(checksum % 103);
    append(106);
    return result;
}
