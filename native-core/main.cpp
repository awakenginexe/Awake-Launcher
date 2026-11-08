// SPDX-License-Identifier: GPL-3.0-only
#include "CoreBackend.h"
#include "awake/integration/GameProcessGroup.h"
#include "awake/integration/ProfileAccess.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
int main(int argc, char** argv)
{
    const auto keeper = Awake::Integration::GameProcessGroup::runKeeper(argc, argv); if (keeper >= 0) return keeper;
    QCoreApplication app(argc, argv); app.setApplicationName("Awake Native Core"); app.setApplicationVersion("0.3.0");
    QCoreApplication::setLibraryPaths({QCoreApplication::applicationDirPath()});
    QCommandLineParser parser; parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"profile", "Private absolute backend data directory.", "directory"}); parser.process(app);
    auto profile = parser.value("profile");
    if (profile.isEmpty()) {
        const auto base = Awake::Integration::privateCoordinationBase();
        if (base.isEmpty() || !Awake::Integration::createPrivateStorageDirectory(base + "/profiles")) return 70;
        profile = base + "/profiles/default";
    }
    Awake::Core::CoreBackend backend; QString error;
    if (!backend.start(profile, &error)) { QTextStream(stderr) << error << Qt::endl; return 71; }
    return app.exec();
}
