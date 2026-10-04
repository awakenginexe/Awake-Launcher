// SPDX-FileCopyrightText: 2022 Sefa Eyeoglu <contact@scrumplex.net>
// SPDX-FileCopyrightText: 2022 Rachel Powers <508861+Ryex@users.noreply.github.com>
// SPDX-FileCopyrightText: 2022 kumquat-ir <66188216+kumquat-ir@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2022 Rachel Powers <508861+Ryex@users.noreply.github.com>
 *  Copyright (C) 2022 kumquat-ir <66188216+kumquat-ir@users.noreply.github.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "BlockedModsDialog.h"

#include "Application.h"
#include "awake/AwakeTheme.h"
#include "modplatform/helpers/HashUtils.h"
#include "settings/SettingsObject.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDirListing>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <utility>

BlockedModsDialog::BlockedModsDialog(QWidget* parent, const QString& title, const QString& text, QList<BlockedMod>& mods, QString hashType)
    : AwakePopupDialog(parent), m_mods(mods), m_hashType(std::move(hashType))
{
    Q_UNUSED(text);
    setWindowTitle(title);
    setPanelSize(QSize(800, 660));
    m_hashingTask = shared_qobject_ptr<ConcurrentTask>(
        new ConcurrentTask("MakeHashesTask", APPLICATION->settings()->get("NumberOfConcurrentTasks").toInt()));
    connect(m_hashingTask.get(), &Task::finished, this, &BlockedModsDialog::hashTaskFinished);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &BlockedModsDialog::directoryChanged);

    auto* heading = new QLabel(tr("Download required files"), panel());
    heading->setProperty("role", "title");
    panelLayout()->addWidget(heading);
    auto* instructions = new QLabel(
        tr("Some creators require downloads from their website. Click Download for each missing file and save it to your Downloads folder. "
           "Keep this window open: installation continues automatically when all files are found."), panel());
    instructions->setWordWrap(true);
    panelLayout()->addWidget(instructions);

    auto* scroll = new QScrollArea(panel());
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* rows = new QWidget(scroll);
    rows->setObjectName("downloadRows");
    auto* rowList = new QVBoxLayout(rows);
    rowList->setContentsMargins(0, 0, 0, 0);
    rowList->setSpacing(10);
    for (const auto& mod : m_mods) {
        auto* row = new QFrame(rows);
        row->setObjectName("downloadRow");
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(16, 13, 16, 13);
        auto* details = new QVBoxLayout;
        auto* name = new QLabel(mod.name, row);
        name->setTextFormat(Qt::PlainText);
        name->setWordWrap(true);
        name->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 14px;"));
        details->addWidget(name);
        auto* status = new QLabel(row);
        status->setWordWrap(true);
        status->setTextFormat(Qt::PlainText);
        details->addWidget(status);
        m_statusLabels.append(status);
        auto* hash = new QLabel(mod.hash.isEmpty() ? tr("Checked by file name") : tr("%1: %2").arg(m_hashType.toUpper(), mod.hash), row);
        hash->setProperty("role", "muted");
        hash->setTextFormat(Qt::PlainText);
        hash->setWordWrap(true);
        hash->setTextInteractionFlags(Qt::TextSelectableByMouse);
        hash->setToolTip(tr("This code checks that the downloaded file is the correct version."));
        details->addWidget(hash);
        layout->addLayout(details, 1);
        auto* download = new QPushButton(tr("Download"), row);
        download->setProperty("primary", true);
        download->setAutoDefault(false);
        download->setAccessibleName(tr("Download %1").arg(mod.name));
        download->setToolTip(tr("Open the creator's download page in your browser"));
        const QUrl url(mod.websiteUrl);
        const bool hasWebsite = url.isValid() && (url.scheme() == "https" || url.scheme() == "http");
        download->setEnabled(hasWebsite);
        if (!hasWebsite)
            download->setToolTip(tr("No download page is available for this file."));
        connect(download, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(url); });
        layout->addWidget(download, 0, Qt::AlignVCenter);
        m_downloadButtons.append(download);
        rowList->addWidget(row);
    }
    rowList->addStretch();
    scroll->setWidget(rows);
    panelLayout()->addWidget(scroll, 1);

    auto* dropHint = new QLabel(tr("Saved somewhere else? Drop the files here, or choose the folder where you saved them."), panel());
    dropHint->setWordWrap(true);
    panelLayout()->addWidget(dropHint);
    auto* actions = new QHBoxLayout;
    auto* folder = new QPushButton(tr("Choose download folder"), panel());
    folder->setAutoDefault(false);
    connect(folder, &QPushButton::clicked, this, &BlockedModsDialog::addDownloadFolder);
    actions->addWidget(folder);
    m_openMissing = new QPushButton(tr("Download all missing"), panel());
    m_openMissing->setAutoDefault(false);
    connect(m_openMissing, &QPushButton::clicked, this, [this] { openAll(true); });
    actions->addWidget(m_openMissing);
    actions->addStretch();
    panelLayout()->addLayout(actions);
    m_folders = new QLabel(panel());
    m_folders->setProperty("role", "muted");
    m_folders->setWordWrap(true);
    m_folders->setTextFormat(Qt::PlainText);
    panelLayout()->addWidget(m_folders);
    auto* footer = new QHBoxLayout;
    m_summary = new QLabel(panel());
    footer->addWidget(m_summary, 1);
    auto* buttons = new QDialogButtonBox(panel());
    auto* proceed = buttons->addButton(tr("Continue without missing files"), QDialogButtonBox::AcceptRole);
    proceed->setToolTip(tr("Missing files will not be installed."));
    auto* cancel = buttons->addButton(tr("Cancel installation"), QDialogButtonBox::RejectRole);
    proceed->setAutoDefault(false);
    cancel->setAutoDefault(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    footer->addWidget(buttons);
    panelLayout()->addLayout(footer);
    setAcceptDrops(true);
    QTimer::singleShot(0, this, [this] {
        setupWatch();
        scanPaths();
        update();
    });
    update();
}

BlockedModsDialog::~BlockedModsDialog() = default;

void BlockedModsDialog::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
    }
}

void BlockedModsDialog::dropEvent(QDropEvent* e)
{
    for (QUrl& url : e->mimeData()->urls()) {
        if (url.scheme().isEmpty()) {  // ensure isLocalFile() works correctly
            url.setScheme("file");
        }

        if (!url.isLocalFile()) {  // can't drop external files here.
            continue;
        }

        QString filePath = url.toLocalFile();
        qDebug() << "[Blocked Mods Dialog] Dropped file:" << filePath;
        addHashTask(filePath);

        // watch for changes
        QFileInfo file = QFileInfo(filePath);
        QString path = file.dir().absolutePath();
        qDebug() << "[Blocked Mods Dialog] Adding watch path:" << path;
        m_watcher.addPath(path);
    }
    scanPaths();
    update();
}

void BlockedModsDialog::done(int r)
{
    QDialog::done(r);
    disconnect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &BlockedModsDialog::directoryChanged);
}

void BlockedModsDialog::openAll(bool missingOnly)
{
    for (auto& mod : m_mods) {
        if (!missingOnly || !mod.matched) {
            const QUrl url(mod.websiteUrl);
            if (url.isValid() && (url.scheme() == "https" || url.scheme() == "http"))
                QDesktopServices::openUrl(url);
        }
    }
}

void BlockedModsDialog::addDownloadFolder()
{
    QString dir =
        QFileDialog::getExistingDirectory(this, tr("Choose the folder containing your downloaded files"),
                                          QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), QFileDialog::ShowDirsOnly);
    if (dir.isEmpty())
        return;
    qDebug() << "[Blocked Mods Dialog] Adding watch path:" << dir;
    m_watcher.addPath(dir);
    scanPath(dir, true);
    update();
}

/// @brief update UI with current status of the blocked mod detection
void BlockedModsDialog::update()
{
    int found = 0;
    bool downloadableMissing = false;
    for (int index = 0; index < m_mods.size(); ++index) {
        const auto& mod = m_mods.at(index);
        auto* status = m_statusLabels.at(index);
        status->setText(mod.matched ? tr("Ready to install") : tr("Waiting for download"));
        status->setStyleSheet(mod.matched ? QStringLiteral("color: #9ee4bc;") : QStringLiteral("color: #f0cb8c;"));
        status->setToolTip(mod.matched ? mod.localPath : tr("Download this file into one of the folders being checked."));
        auto* button = m_downloadButtons.at(index);
        const QUrl url(mod.websiteUrl);
        const bool hasWebsite = url.isValid() && (url.scheme() == "https" || url.scheme() == "http");
        button->setEnabled(!mod.matched && hasWebsite);
        button->setText(mod.matched ? tr("Downloaded") : tr("Download"));
        downloadableMissing |= !mod.matched && hasWebsite;
        found += mod.matched ? 1 : 0;
    }
    m_summary->setText(tr("%1 of %2 files ready").arg(found).arg(m_mods.size()));
    m_openMissing->setEnabled(downloadableMissing);
    const QStringList folders = m_watcher.directories();
    m_folders->setText(folders.isEmpty() ? tr("Checking your download folders…") :
                      tr("Checking %1 folder(s) automatically").arg(folders.size()));
    m_folders->setToolTip(folders.join(QStringLiteral("\n")));
    if (found == m_mods.size())
        accept();
}

/// @brief Signal fired when a watched directory has changed
/// @param path the path to the changed directory
void BlockedModsDialog::directoryChanged(const QString& path)
{
    qDebug() << "[Blocked Mods Dialog] Directory changed:" << path;
    validateMatchedMods();
    scanPath(path, true);
}

/// @brief add the user downloads folder and the global mods folder to the filesystem watcher
void BlockedModsDialog::setupWatch()
{
    const QString downloadsFolder = APPLICATION->settings()->get("DownloadsDir").toString();
    const QString modsFolder = APPLICATION->settings()->get("CentralModsDir").toString();
    const bool downloadsFolderWatchRecursive = APPLICATION->settings()->get("DownloadsDirWatchRecursive").toBool();
    watchPath(downloadsFolder, downloadsFolderWatchRecursive);
    watchPath(modsFolder, true);
}

void BlockedModsDialog::watchPath(const QString& path, bool watchRecursive)
{
    auto toWatch = QFileInfo(path);
    if (!toWatch.isReadable()) {
        qWarning() << "[Blocked Mods Dialog] Failed to add Watch Path (unable to read):" << path;
        return;
    }
    auto toWatchPath = toWatch.canonicalFilePath();
    if (m_watcher.directories().contains(toWatchPath)) {
        return;  // don't watch the same path twice (no loops!)
    }

    qDebug() << "[Blocked Mods Dialog] Adding Watch Path:" << path;
    m_watcher.addPath(toWatchPath);

    if (!toWatch.isDir() || !watchRecursive) {
        return;
    }

    for (const auto& entry : QDirListing(toWatchPath, QDirListing::IteratorFlag::DirsOnly | QDirListing::IteratorFlag::ResolveSymlinks)) {
        QString watchDir = entry.canonicalFilePath();  // resolve symlinks and relative paths
        watchPath(watchDir, watchRecursive);
    }
}

/// @brief scan all watched folder
void BlockedModsDialog::scanPaths()
{
    for (auto& dir : m_watcher.directories()) {
        scanPath(dir, false);
    }
    runHashTask();
}

/// @brief Scan the directory at path, skip paths that do not contain a file name
///        of a blocked mod we are looking for
/// @param path the directory to scan
void BlockedModsDialog::scanPath(const QString& path, bool startTask)
{
    for (const auto& entry : QDirListing(path, QDirListing::IteratorFlag::FilesOnly | QDirListing::IteratorFlag::ResolveSymlinks |
                                                   QDirListing::IteratorFlag::IncludeHidden)) {
        QString file = entry.absoluteFilePath();

        if (!checkValidPath(file)) {
            continue;
        }

        addHashTask(file);
    }

    if (startTask) {
        runHashTask();
    }
}

/// @brief add a hashing task for the file located at path, add the path to the pending set if the hashing task is already running
/// @param path the path to the local file being hashed
void BlockedModsDialog::addHashTask(const QString& path)
{
    qDebug() << "[Blocked Mods Dialog] adding a Hash task for" << path << "to the pending set.";
    m_pendingHashPaths.insert(path);
}

/// @brief add a hashing task for the file located at path and connect it to check that hash against
///        our blocked mods list
/// @param path the path to the local file being hashed
void BlockedModsDialog::buildHashTask(const QString& path)
{
    auto hashTask = Hashing::createHasher(path, m_hashType);

    qDebug() << "[Blocked Mods Dialog] Creating Hash task for path:" << path;

    connect(hashTask.get(), &Task::succeeded, this, [this, hashTask, path] { checkMatchHash(hashTask->getResult(), path); });
    connect(hashTask.get(), &Task::failed, this, [path] { qDebug() << "Failed to hash path:" << path; });

    m_hashingTask->addTask(hashTask);
}

/// @brief check if the computed hash for the provided path matches a blocked
///        mod we are looking for
/// @param hash the computed hash for the provided path
/// @param path the path to the local file being compared
void BlockedModsDialog::checkMatchHash(const QString& hash, const QString& path)
{
    bool match = false;

    qDebug() << "[Blocked Mods Dialog] Checking for match on hash:" << hash << "| From path:" << path;

    auto downloadDir = QFileInfo(APPLICATION->settings()->get("DownloadsDir").toString()).absoluteFilePath();
    auto moveFiles = APPLICATION->settings()->get("MoveModsFromDownloadsDir").toBool();
    for (auto& mod : m_mods) {
        if (mod.matched) {
            continue;
        }
        if (mod.hash.compare(hash, Qt::CaseInsensitive) == 0) {
            mod.matched = true;
            mod.localPath = path;
            if (moveFiles) {
                mod.move = QFileInfo(path).absoluteFilePath().startsWith(downloadDir);
            }
            match = true;

            qDebug() << "[Blocked Mods Dialog] Hash match found:" << mod.name << hash << "| From path:" << path;

            break;
        }
    }

    if (match) {
        update();
    }
}

/// @brief Check if the name of the file at path matches the name of a blocked mod we are searching for
/// @param path the path to check
/// @return boolean: did the path match the name of a blocked mod?
bool BlockedModsDialog::checkValidPath(const QString& path)
{
    const QFileInfo file = QFileInfo(path);
    const QString filename = file.fileName();

    auto compare = [](const QString& fsFilename, const QString& metadataFilename) {
        return metadataFilename.compare(fsFilename, Qt::CaseInsensitive) == 0;
    };

    // super lax compare (but not fuzzy)
    // convert to lowercase
    // convert all speratores to whitespace
    // simplify sequence of internal whitespace to a single space
    // efectivly compare two strings ignoring all separators and case
    auto laxCompare = [](const QString& fsfilename, const QString& metadataFilename) {
        // allowed character seperators
        QList<QChar> allowedSeperators = { '-', '+', '.', '_' };

        // copy in lowercase
        auto fsName = fsfilename.toLower();
        auto metaName = metadataFilename.toLower();

        // replace all potential allowed seperatores with whitespace
        for (auto sep : allowedSeperators) {
            fsName = fsName.replace(sep, ' ');
            metaName = metaName.replace(sep, ' ');
        }

        // remove extraneous whitespace
        fsName = fsName.simplified();
        metaName = metaName.simplified();

        return fsName.compare(metaName) == 0;
    };

    auto downloadDir = QFileInfo(APPLICATION->settings()->get("DownloadsDir").toString()).absoluteFilePath();
    auto moveFiles = APPLICATION->settings()->get("MoveModsFromDownloadsDir").toBool();
    for (auto& mod : m_mods) {
        if (compare(filename, mod.name)) {
            // if the mod is not yet matched and doesn't have a hash then
            // just match it with the file that has the exact same name
            if (!mod.matched && mod.hash.isEmpty()) {
                mod.matched = true;
                mod.localPath = path;
                if (moveFiles) {
                    mod.move = QFileInfo(path).absoluteFilePath().startsWith(downloadDir);
                }
                return false;
            }
            qDebug() << "[Blocked Mods Dialog] Name match found:" << mod.name << "| From path:" << path;
            return true;
        }
        if (laxCompare(filename, mod.name)) {
            qDebug() << "[Blocked Mods Dialog] Lax name match found:" << mod.name << "| From path:" << path;
            return true;
        }
    }

    return false;
}

/// @brief ensure matched file paths still exist
void BlockedModsDialog::validateMatchedMods()
{
    bool changed = false;
    for (auto& mod : m_mods) {
        if (mod.matched) {
            QFileInfo file = QFileInfo(mod.localPath);
            if (!file.exists() || !file.isFile()) {
                qDebug() << "[Blocked Mods Dialog] File" << mod.localPath << "for mod" << mod.name
                         << "has vanshed! marking as not matched.";
                mod.localPath = "";
                mod.matched = false;
                changed = true;
            }
        }
    }
    if (changed) {
        update();
    }
}

/// @brief run hash task or mark a pending run if it is already running
void BlockedModsDialog::runHashTask()
{
    if (!m_hashingTask->isRunning()) {
        m_rehashPending = false;

        if (!m_pendingHashPaths.isEmpty()) {
            qDebug() << "[Blocked Mods Dialog] there are pending hash tasks, building and running tasks";

            auto path = m_pendingHashPaths.begin();
            while (path != m_pendingHashPaths.end()) {
                buildHashTask(*path);
                path = m_pendingHashPaths.erase(path);
            }

            m_hashingTask->start();
        }
    } else {
        qDebug() << "[Blocked Mods Dialog] queueing another run of the hashing task";
        qDebug() << "[Blocked Mods Dialog] pending hash tasks:" << m_pendingHashPaths;
        m_rehashPending = true;
    }
}

void BlockedModsDialog::hashTaskFinished()
{
    qDebug() << "[Blocked Mods Dialog] All hash tasks finished";
    if (m_rehashPending) {
        qDebug() << "[Blocked Mods Dialog] task finished with a rehash pending, rerunning";
        runHashTask();
    }
}

/// qDebug print support for the BlockedMod struct
QDebug operator<<(QDebug debug, const BlockedMod& m)
{
    QDebugStateSaver saver(debug);

    debug.nospace() << "{ name: " << m.name << ", websiteUrl: " << m.websiteUrl << ", hash: " << m.hash << ", matched: " << m.matched
                    << ", localPath: " << m.localPath << "}";

    return debug;
}
