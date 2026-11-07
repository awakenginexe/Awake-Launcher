// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QList>
#include <QString>
namespace Awake::Core {
// Loader metadata read from a verified JAR. Provider dependency records are claims; the loader
// metadata is what Forge/NeoForge/Fabric/Quilt actually enforce at runtime.
struct DeclaredDependency {
    QString modId;
    bool mandatory = false;
    QString range; // Maven range (Forge/NeoForge) or SemVer predicate set (Fabric/Quilt); empty = any
};
struct ModMetadata {
    bool readable = false;
    QString format; // "forge", "neoforge", "fabric" or "quilt"
    QList<QPair<QString, QString>> mods; // modId -> version (version may be empty when unresolvable)
    QList<DeclaredDependency> dependencies;
};
enum class RangeResult { Satisfied, Violated, Unknown };
// Reads only bounded, named metadata entries of a local ZIP/JAR; never executes or extracts files.
// Multi-loader JARs carry several descriptors: `preferred` selects the one the target loader reads.
ModMetadata inspectJar(const QString& path, const QString& preferred);
// Evaluates a declared range for the given metadata format against a concrete version.
RangeResult satisfies(const QString& format, const QString& range, const QString& version);
// Compares versions with Maven ComparableVersion ordering (sufficient for SemVer release/pre-release order).
int compareVersions(const QString& left, const QString& right);
}
