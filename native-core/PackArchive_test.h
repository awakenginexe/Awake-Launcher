#pragma once
#include <QObject>
class PackArchiveTest : public QObject {
    Q_OBJECT
private slots:
    void validMrpack();
    void validCurseForge();
    void rejectUnsafe_data();
    void rejectUnsafe();
    void rejectMissingManifestAndHashes();
    void rejectCaseDuplicate();
    void rejectDangerousMetadata_data();
    void rejectDangerousMetadata();
    void strictManifest();
};
