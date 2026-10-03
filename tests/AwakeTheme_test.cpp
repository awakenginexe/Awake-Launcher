// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>
#include <cmath>
#include "awake/DesignTokens.h"

static double luminance(const QColor& c)
{
    const auto linear = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
}
class AwakeThemeTest : public QObject {
    Q_OBJECT
   private slots:
    void visibleControls()
    {
        const auto border = luminance(Awake::color(Awake::Color::Border));
        const auto surface = luminance(Awake::color(Awake::Color::Secondary));
        QVERIFY2((border + 0.05) / (surface + 0.05) >= 3.0, "Control boundary contrast below 3:1");
    }
    void readablePalette()
    {
        const auto palette = Awake::palette();
        for (auto pair :
             { std::pair{ QPalette::WindowText, QPalette::Window }, std::pair{ QPalette::Text, QPalette::Base },
               std::pair{ QPalette::HighlightedText, QPalette::Highlight }, std::pair{ QPalette::ButtonText, QPalette::Button } }) {
            auto a = luminance(palette.color(pair.first));
            auto b = luminance(palette.color(pair.second));
            QVERIFY2((std::max(a, b) + 0.05) / (std::min(a, b) + 0.05) >= 4.5, "Text contrast below 4.5:1");
        }
    }
};
QTEST_GUILESS_MAIN(AwakeThemeTest)
#include "AwakeTheme_test.moc"
