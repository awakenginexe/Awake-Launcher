// SPDX-License-Identifier: GPL-3.0-only
#include "PackArchive.h"
#include <archive.h>
#include <archive_entry.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QtEndian>
#include <memory>

namespace {
constexpr quint64 GiB = 1024ULL * 1024 * 1024;
quint16 u16(const char *p) { return qFromLittleEndian<quint16>(p); }
quint32 u32(const char *p) { return qFromLittleEndian<quint32>(p); }
QJsonObject fail(const QString &s) { return {{"ok", false}, {"error", s}}; }
struct Entry { QString name; quint32 compressed, size; bool dir; };

bool safePath(const QString &path, bool directory = false)
{
    if (path.isEmpty() || path.startsWith('/') || path.contains('\\') || path.contains(':') || path.contains(QChar::Null)) return false;
    QString p = path;
    if (directory && p.endsWith('/')) p.chop(1);
    static const QRegularExpression device(QString::fromUtf8("^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\\..*)?$"), QRegularExpression::CaseInsensitiveOption);
    for (const auto &part : p.split('/')) {
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ') || device.match(part).hasMatch()) return false;
        for (const auto c : part) if (c.unicode() < 32 || QStringLiteral("<>\"|?*").contains(c)) return false;
    }
    return true;
}

bool centralDirectory(QFile &file, QList<Entry> &entries, QString &error)
{
    const auto length = file.size();
    if (length < 22 || quint64(length) > GiB) { error = "Archive size outside limits"; return false; }
    const qint64 tailSize = qMin<qint64>(length, 65557);
    file.seek(length - tailSize);
    const QByteArray tail = file.read(tailSize);
    qsizetype end = -1;
    for (qsizetype i = tail.size() - 22; i >= 0; --i) {
        if (u32(tail.constData()+i) == 0x06054b50 && i + 22 + u16(tail.constData()+i+20) == tail.size()) { end = i; break; }
    }
    if (end < 0) { error = "Missing ZIP end record"; return false; }
    const char *e = tail.constData()+end;
    const quint32 count = u16(e+10), cdSize = u32(e+12), cdOffset = u32(e+16);
    if (u16(e+4) || u16(e+6) || u16(e+8) != count || count == 0xffff || cdSize == 0xffffffff || cdOffset == 0xffffffff || count > 10000 || quint64(cdOffset)+cdSize != quint64(length-tailSize+end)) {
        error = "Unsupported ZIP layout"; return false;
    }
    if (!file.seek(cdOffset)) { error = "Invalid ZIP directory"; return false; }
    QSet<QString> names;
    QList<QPair<quint64,quint64>> ranges;
    quint64 total = 0;
    for (quint32 i=0; i<count; ++i) {
        QByteArray h = file.read(46);
        if (h.size()!=46 || u32(h.constData())!=0x02014b50) { error = "Invalid ZIP directory entry"; return false; }
        const char *p=h.constData();
        const quint16 flags=u16(p+8), method=u16(p+10), nl=u16(p+28), xl=u16(p+30), cl=u16(p+32), disk=u16(p+34);
        const quint32 compressed=u32(p+20), size=u32(p+24), attrs=u32(p+38), offset=u32(p+42);
        QByteArray name=file.read(nl), extra=file.read(xl), comment=file.read(cl);
        if (name.size()!=nl || extra.size()!=xl || comment.size()!=cl || disk || (flags & 0x2041) || (method!=0 && method!=8) || compressed==0xffffffff || size==0xffffffff || offset==0xffffffff || offset>=cdOffset || size>GiB || (size && (!compressed || quint64(size)>quint64(compressed)*200))) { error="Unsupported or oversized ZIP entry"; return false; }
        for (qsizetype x=0; x<extra.size();) {
            if (extra.size()-x<4) { error="Invalid ZIP extra field"; return false; }
            const auto tag=u16(extra.constData()+x), sizeExtra=u16(extra.constData()+x+2);
            if (tag==1 || sizeExtra>extra.size()-x-4) { error="Unsupported ZIP extra field"; return false; }
            x+=4+sizeExtra;
        }
        const auto next=file.pos();
        if (!file.seek(offset)) { error="Invalid ZIP local header"; return false; }
        const auto local=file.read(30);
        if (local.size()!=30 || u32(local.constData())!=0x04034b50 || u16(local.constData()+6)!=flags || u16(local.constData()+8)!=method || u16(local.constData()+26)!=nl || (!(flags & 8) && (u32(local.constData()+14)!=u32(p+16) || u32(local.constData()+18)!=compressed || u32(local.constData()+22)!=size)) || file.read(nl)!=name) { error="ZIP local header mismatch"; return false; }
        const quint64 dataStart=quint64(offset)+30+nl+u16(local.constData()+28), dataEnd=dataStart+compressed;
        if (dataEnd>cdOffset || dataStart>dataEnd) { error="Invalid ZIP data range"; return false; }
        for (const auto &range: ranges) if (quint64(offset)<range.second && dataEnd>range.first) { error="Overlapping ZIP entries"; return false; }
        ranges.append({offset,dataEnd});
        if (!file.seek(next)) { error="Invalid ZIP directory"; return false; }
        const QString path=QString::fromUtf8(name);
        if (path.toUtf8()!=name) { error="ZIP path is not UTF-8"; return false; }
        const bool dir=path.endsWith('/');
        const auto mode = (attrs >> 16) & 0170000;
        if (!safePath(path, dir) || (mode && mode != 0100000 && mode != 0040000) || (attrs & 0x400) || (dir && size) || (!dir && mode==0040000) || (dir && mode==0100000)) { error="Unsafe ZIP entry"; return false; }
        QString key=path; if (dir) key.chop(1); key=key.toCaseFolded();
        if (names.contains(key)) { error="Duplicate ZIP path"; return false; }
        names.insert(key);
        total+=size; if (total>2*GiB) { error="ZIP extraction limit exceeded"; return false; }
        entries.append({path, compressed, size, dir});
    }
    if (file.pos()!=qint64(cdOffset)+cdSize) { error="Invalid ZIP directory length"; return false; }
    return true;
}

bool jsonObject(const QByteArray &data, QJsonObject &out)
{
    QJsonParseError error; const auto doc=QJsonDocument::fromJson(data, &error);
    if (error.error!=QJsonParseError::NoError || !doc.isObject()) return false;
    out=doc.object(); return true;
}
bool digest(const QString &s, int length) { return s.size()==length && QRegularExpression("^[0-9a-fA-F]+$").match(s).hasMatch(); }
}

QJsonObject PackArchive::inspect(const QString &archivePath, const QString &format, const QString &privateStagingDir)
{
    const bool mr=format=="mrpack", cf=format=="curseforge";
    if (!mr && !cf) return fail("Unsupported package format");
    QFile input(archivePath); if (!input.open(QIODevice::ReadOnly)) return fail("Cannot open archive");
    QList<Entry> entries; QString error;
    if (!centralDirectory(input, entries, error)) return fail(error);
    const QFileInfo stagingInfo(privateStagingDir);
    QDir staging(privateStagingDir);
    if (!stagingInfo.exists() || !stagingInfo.isDir() || stagingInfo.isSymLink() || !staging.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty()) return fail("Staging directory must be private and empty");
    using Archive = std::unique_ptr<archive, decltype(&archive_read_free)>;
    Archive ar(archive_read_new(), archive_read_free);
    archive_read_support_format_zip(ar.get());
    const auto wide=archivePath.toStdWString();
    if (archive_read_open_filename_w(ar.get(), wide.c_str(), 65536)!=ARCHIVE_OK) return fail("Cannot read ZIP");
    QJsonObject manifest; bool found=false; QJsonArray files; QStringList created;
    auto abort=[&](const QString &reason) { for (const auto &name: created) QFile::remove(staging.filePath(name)); return fail(reason); };
    archive_entry *entry=nullptr;
    for (const auto &expected: entries) {
        if (archive_read_next_header(ar.get(), &entry)!=ARCHIVE_OK) return abort("ZIP entries do not match directory");
        const char *raw=archive_entry_pathname_utf8(entry);
        if (!raw || QString::fromUtf8(raw)!=expected.name || archive_entry_size(entry)!=expected.size || archive_entry_is_encrypted(entry)>0 || archive_entry_symlink(entry) || archive_entry_hardlink(entry) || archive_entry_filetype(entry)!=(expected.dir ? AE_IFDIR : AE_IFREG)) return abort("ZIP entry metadata mismatch");
        const QString manifestName=mr ? "modrinth.index.json" : "manifest.json";
        const bool isManifest=expected.name==manifestName;
        if (isManifest && (expected.dir || expected.size>8*1024*1024)) return abort("Invalid package manifest size");
        const bool overrideFile=!expected.dir && (expected.name.startsWith("overrides/") || (mr && expected.name.startsWith("client-overrides/")));
        QByteArray data;
        std::unique_ptr<QFile> out;
        QString stagedName;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (overrideFile) {
            stagedName=QString::number(created.size())+".bin";
            out=std::make_unique<QFile>(staging.filePath(stagedName));
            if (!out->open(QIODevice::WriteOnly | QIODevice::NewOnly)) return abort("Cannot create staging file");
            created.append(stagedName);
        }
        quint64 actual=0; char buffer[65536]; la_ssize_t n;
        while ((n=archive_read_data(ar.get(), buffer, sizeof buffer))>0) {
            actual+=quint64(n); if (actual>expected.size) { if (out) out->close(); return abort("ZIP entry exceeded declared size"); }
            if (isManifest) data.append(buffer, n);
            if (out && (out->write(buffer,n)!=n)) { out->close(); return abort("Cannot write staging file"); }
            if (out) hash.addData(QByteArrayView(buffer,n));
        }
        if (n<0 || actual!=expected.size) { if (out) out->close(); return abort("Corrupt ZIP entry"); }
        if (isManifest) { if (!jsonObject(data,manifest)) return abort("Invalid package manifest"); found=true; }
        if (out) {
            out->close();
            const bool client=expected.name.startsWith("client-overrides/");
            const QString path=expected.name.mid(client ? 17 : 10);
            if (!safePath(path)) return abort("Unsafe override path");
            files.append(QJsonObject{{"path",path},{"stagedName",stagedName},{"sha256",QString::fromLatin1(hash.result().toHex())},{"size",double(actual)},{"clientOverride",client}});
        }
    }
    if (archive_read_next_header(ar.get(), &entry)!=ARCHIVE_EOF || !found) return abort("Missing manifest or unexpected ZIP entry");
    QJsonObject loader{{"id","vanilla"},{"version",""}}; QString minecraft;
    QJsonArray downloads, projects;
    if (mr) {
        if (manifest.value("formatVersion")!=QJsonValue(1) || manifest.value("game").toString()!="minecraft" || !manifest.value("files").isArray() || manifest.value("files").toArray().size()>10000 || !manifest.value("dependencies").isObject()) return abort("Unsupported Modrinth manifest");
        const auto deps=manifest.value("dependencies").toObject(); minecraft=deps.value("minecraft").toString();
        for (const QString &id: {QStringLiteral("fabric-loader"),QStringLiteral("forge"),QStringLiteral("neoforge"),QStringLiteral("quilt-loader")}) if (deps.contains(id)) { if (loader.value("id").toString()!="vanilla" || !deps.value(id).isString() || deps.value(id).toString().isEmpty() || deps.value(id).toString().size()>96) return abort("Invalid loader dependencies"); loader={{"id",id},{"version",deps.value(id).toString()}}; }
        QSet<QString> downloadPaths;
        for (const auto &v: manifest.value("files").toArray()) {
            if (!v.isObject()) return abort("Invalid Modrinth file"); auto f=v.toObject();
            auto hashes=f.value("hashes").toObject(); const auto urls=f.value("downloads").toArray();
            const QString destination=f.value("path").toString(); const double size=f.value("fileSize").toDouble();
            if (!safePath(destination) || downloadPaths.contains(destination.toCaseFolded()) || !digest(hashes.value("sha512").toString(),128) || !digest(hashes.value("sha1").toString(),40) || urls.isEmpty() || !f.value("fileSize").isDouble() || size<0 || size>double(GiB) || size!=qint64(size)) return abort("Invalid Modrinth file metadata");
            hashes["sha512"]=hashes.value("sha512").toString().toLower(); hashes["sha1"]=hashes.value("sha1").toString().toLower(); f["hashes"]=hashes;
            downloadPaths.insert(destination.toCaseFolded());
            for (const auto &url: urls) { const QUrl u(url.toString()); if (!url.isString() || !u.isValid() || u.scheme()!="https" || u.host().isEmpty() || !u.userInfo().isEmpty()) return abort("Invalid download URL"); }
            if (f.contains("env") && !f.value("env").isObject()) return abort("Invalid file environment");
            const auto env=f.value("env").toObject(); const QString client=env.value("client").toString("required"), server=env.value("server").toString("required");
            if ((env.contains("client") && !env.value("client").isString()) || (env.contains("server") && !env.value("server").isString())) return abort("Invalid file environment");
            if ((client!="required" && client!="optional" && client!="unsupported") || (server!="required" && server!="optional" && server!="unsupported")) return abort("Invalid file environment");
            if (client!="unsupported") downloads.append(f);
        }
    } else {
        const auto mc=manifest.value("minecraft").toObject(); minecraft=mc.value("version").toString();
        if (manifest.value("manifestType").toString()!="minecraftModpack" || manifest.value("manifestVersion")!=QJsonValue(1) || !manifest.value("minecraft").isObject() || !manifest.value("files").isArray() || manifest.value("files").toArray().size()>1024 || !mc.value("modLoaders").isArray() || manifest.value("overrides").toString("overrides")!="overrides") return abort("Unsupported CurseForge manifest");
        for (const auto &v: mc.value("modLoaders").toArray()) { const auto l=v.toObject(); const QString value=l.value("id").toString(); const int split=value.indexOf('-'); if (!v.isObject() || !l.value("id").isString() || !l.value("primary").isBool() || split<=0 || split==value.size()-1 || split>96 || value.size()-split-1>96) return abort("Invalid CurseForge loader"); if (!l.value("primary").toBool()) continue; if (loader.value("id").toString()!="vanilla") return abort("Invalid CurseForge loader"); loader={{"id",value.left(split)},{"version",value.mid(split+1)}}; }
        for (const auto &v: manifest.value("files").toArray()) { const auto f=v.toObject(); const double project=f.value("projectID").toDouble(), version=f.value("fileID").toDouble(); if (!v.isObject() || project<1 || version<1 || project>9007199254740991.0 || version>9007199254740991.0 || project!=qint64(project) || version!=qint64(version) || !f.value("required").isBool()) return abort("Invalid CurseForge file"); projects.append(QJsonObject{{"provider","curseforge"},{"projectId",QString::number(qint64(project))},{"versionId",QString::number(qint64(version))},{"required",f.value("required")}}); }
    }
    if (minecraft.isEmpty() || minecraft.size()>96) return abort("Invalid Minecraft version");
    // Client overrides replace common overrides at the same destination.
    QJsonArray selected; QSet<QString> paths;
    for (int pass=0; pass<2; ++pass) for (const auto &v: files) { auto f=v.toObject(); if (f.value("clientOverride").toBool()!=(pass==0)) continue; const QString key=f.value("path").toString().toCaseFolded(); if (paths.contains(key)) { QFile::remove(staging.filePath(f.value("stagedName").toString())); continue; } paths.insert(key); f.remove("clientOverride"); selected.append(f); }
    return {{"ok",true},{"minecraftVersion",minecraft},{"loader",loader},{"files",selected},{"downloads",downloads},{"projects",projects},{"warnings",QJsonArray{}},{"manualFiles",QJsonArray{}}};
}
