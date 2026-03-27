#include "DigiKamLibrary.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QSettings>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QImageReader>
#include <QDebug>
#include <functional>
#include <algorithm>

// Recognised image extensions for folder mode.
static const QStringList kImageFilters = {
    "*.jpg", "*.jpeg", "*.png", "*.tif", "*.tiff", "*.webp", "*.bmp"
};

// ── Construction / destruction ─────────────────────────────────────────────

DigiKamLibrary::DigiKamLibrary(QObject *parent)
    : QObject(parent)
{
    if (openDatabase()) {
        m_databaseAvailable = true;
        emit databaseAvailableChanged();
        loadDbTags();
    }
}

DigiKamLibrary::~DigiKamLibrary()
{
    const QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    if (!connName.isEmpty())
        QSqlDatabase::removeDatabase(connName);
}

// ── DB mode: init ──────────────────────────────────────────────────────────

bool DigiKamLibrary::openDatabase()
{
    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString iniPath = configDir + "/digikam/digikamrc";

    if (!QFile::exists(iniPath)) {
        m_statusMessage = QStringLiteral("No digikam database found. Select a folder to browse images.");
        emit statusMessageChanged();
        return false;
    }

    QSettings ini(iniPath, QSettings::IniFormat);
    ini.beginGroup("Database Settings");
    QString dbPath = ini.value("Database Name").toString();
    ini.endGroup();

    if (dbPath.isEmpty()) {
        m_statusMessage = QStringLiteral("'Database Name' not set in digikamrc");
        emit statusMessageChanged();
        return false;
    }

    // KDE on Windows prefixes drive letters with '/': "/C:/..." → "C:/..."
    if (dbPath.startsWith('/') && dbPath.size() > 2 && dbPath.at(2) == ':')
        dbPath = dbPath.mid(1);

    if (!QFile::exists(dbPath)) {
        m_statusMessage = QStringLiteral("digikam4.db not found at %1").arg(dbPath);
        emit statusMessageChanged();
        return false;
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE", "fotoframe_digikam");
    m_db.setDatabaseName(dbPath);
    m_db.setConnectOptions("QSQLITE_OPEN_READONLY");

    if (!m_db.open()) {
        m_statusMessage = QStringLiteral("Cannot open database: %1")
                              .arg(m_db.lastError().text());
        emit statusMessageChanged();
        qWarning() << m_statusMessage;
        return false;
    }

    return true;
}

// ── DB mode: tag loading ───────────────────────────────────────────────────

void DigiKamLibrary::loadDbTags()
{
    // 1. Load all tags as id → {pid, name}
    QMap<int, QPair<int, QString>> raw;
    {
        QSqlQuery q(m_db);
        q.exec("SELECT id, pid, name FROM Tags");
        while (q.next())
            raw[q.value(0).toInt()] = { q.value(1).toInt(), q.value(2).toString() };
    }

    // 2. Build full paths with memoised recursion (handles any id ordering)
    QMap<int, QString> tagPaths;
    std::function<QString(int)> getPath = [&](int id) -> QString {
        if (tagPaths.contains(id))
            return tagPaths[id];
        if (!raw.contains(id))
            return {};
        const auto &[pid, name] = raw[id];
        tagPaths[id] = (pid <= 0) ? name : (getPath(pid) + "/" + name);
        return tagPaths[id];
    };
    for (auto it = raw.keyBegin(); it != raw.keyEnd(); ++it)
        getPath(*it);

    // 3. Find tags that have at least one visible image
    QSet<int> activeIds;
    {
        QSqlQuery q(m_db);
        q.exec(
            "SELECT DISTINCT it.tagid "
            "FROM ImageTags it "
            "JOIN Images i ON i.id = it.imageid "
            "WHERE i.status = 1 AND i.category = 1"
        );
        while (q.next())
            activeIds.insert(q.value(0).toInt());
    }

    // 4. Build path → IDs map for active tags only
    m_tagNameToIds.clear();
    for (auto it = tagPaths.cbegin(); it != tagPaths.cend(); ++it) {
        if (activeIds.contains(it.key()))
            m_tagNameToIds[it.value()] << it.key();
    }

    m_allTags = m_tagNameToIds.keys();
    std::sort(m_allTags.begin(), m_allTags.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });

    emit allTagsChanged();
}

// ── DB mode: photo query ───────────────────────────────────────────────────

void DigiKamLibrary::refreshPhotosFromDb()
{
    if (m_selectedTags.isEmpty() || !m_db.isOpen())
        return;

    QList<int> ids;
    for (const QString &name : std::as_const(m_selectedTags)) {
        const auto it = m_tagNameToIds.constFind(name);
        if (it != m_tagNameToIds.constEnd())
            ids.append(it.value());
    }
    if (ids.isEmpty())
        return;

    QStringList ph;
    ph.reserve(ids.size());
    for (int i = 0; i < ids.size(); ++i) ph << "?";

    QSqlQuery q(m_db);
    q.prepare(QString(
        "SELECT DISTINCT "
        "  ar.specificPath, al.relativePath, i.name, "
        "  ii.creationDate, ii.width, ii.height, ii.orientation "
        "FROM Images i "
        "JOIN Albums           al ON al.id       = i.album "
        "JOIN AlbumRoots       ar ON ar.id       = al.albumRoot "
        "JOIN ImageInformation ii ON ii.imageid  = i.id "
        "JOIN ImageTags        it ON it.imageid  = i.id "
        "WHERE it.tagid IN (%1) "
        "  AND i.status = 1 AND i.category = 1 "
        "ORDER BY ii.creationDate ASC, i.name ASC"
    ).arg(ph.join(',')));

    for (int id : std::as_const(ids))
        q.addBindValue(id);

    if (!q.exec()) {
        qWarning() << "Photo query failed:" << q.lastError().text();
        return;
    }

    while (q.next()) {
        const QString url = buildFilePath(q.value(0).toString(),
                                          q.value(1).toString(),
                                          q.value(2).toString());
        const qreal ar = effectiveAspect(q.value(4).toInt(),
                                         q.value(5).toInt(),
                                         q.value(6).toInt());
        m_photos        << url;
        m_aspectRatios  << ar;
        m_totalAspectSum += ar;
    }
}

// ── Folder mode: load ──────────────────────────────────────────────────────

void DigiKamLibrary::setFolder(const QString &folderUrl)
{
    // QML FolderDialog gives us a file:// URL; convert to a local path.
    QString localPath = QUrl(folderUrl).toLocalFile();
    if (localPath.isEmpty())
        localPath = folderUrl; // already a plain path

    if (!QDir(localPath).exists()) {
        qWarning() << "setFolder: path does not exist:" << localPath;
        return;
    }

    loadFromFolder(localPath);
}

void DigiKamLibrary::loadFromFolder(const QString &localPath)
{
    m_folderTagFiles.clear();
    m_usingFolderMode = true;

    const QDir rootDir(localPath);

    // Root-level images → tag = the selected folder's own name
    const QString rootTag = rootDir.dirName();
    const QStringList rootFiles =
        rootDir.entryList(kImageFilters, QDir::Files, QDir::Name);
    for (const QString &f : rootFiles)
        m_folderTagFiles[rootTag] << rootDir.absoluteFilePath(f);

    // First-level subfolders → tag = subfolder name
    const QStringList subDirs =
        rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &sub : subDirs) {
        QDir subDir(rootDir.absoluteFilePath(sub));
        const QStringList files =
            subDir.entryList(kImageFilters, QDir::Files, QDir::Name);
        for (const QString &f : files)
            m_folderTagFiles[sub] << subDir.absoluteFilePath(f);
    }

    // Drop tags with no images
    for (auto it = m_folderTagFiles.begin(); it != m_folderTagFiles.end(); ) {
        it->isEmpty() ? it = m_folderTagFiles.erase(it) : ++it;
    }

    // Reset selection
    m_selectedTags.clear();
    emit selectedTagsChanged();

    // Rebuild allTags
    m_allTags = m_folderTagFiles.keys();
    std::sort(m_allTags.begin(), m_allTags.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    emit allTagsChanged();

    refreshPhotos();
}

// ── Folder mode: photo query ───────────────────────────────────────────────

void DigiKamLibrary::refreshPhotosFromFolder()
{
    if (m_selectedTags.isEmpty())
        return;

    // Collect and sort paths from all selected tags
    QStringList allPaths;
    for (const QString &tag : std::as_const(m_selectedTags)) {
        const auto it = m_folderTagFiles.constFind(tag);
        if (it != m_folderTagFiles.constEnd())
            allPaths.append(it.value());
    }

    // Sort by filename (date is often encoded there: YYYYMMDD_HHMMSS, etc.)
    std::sort(allPaths.begin(), allPaths.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).fileName().compare(QFileInfo(b).fileName(),
                                               Qt::CaseInsensitive) < 0;
    });
    allPaths.removeDuplicates();

    for (const QString &path : std::as_const(allPaths)) {
        const qreal ar = aspectFromFile(path);
        m_photos        << QUrl::fromLocalFile(path).toString();
        m_aspectRatios  << ar;
        m_totalAspectSum += ar;
    }
}

// ── Shared dispatch ────────────────────────────────────────────────────────

void DigiKamLibrary::refreshPhotos()
{
    m_photos.clear();
    m_aspectRatios.clear();
    m_totalAspectSum = 0.0;

    if (!m_usingFolderMode)
        refreshPhotosFromDb();
    else
        refreshPhotosFromFolder();

    emit photosChanged();
}

void DigiKamLibrary::setSelectedTags(const QStringList &tags)
{
    if (m_selectedTags == tags)
        return;
    m_selectedTags = tags;
    emit selectedTagsChanged();
    refreshPhotos();
}

// ── Static helpers ─────────────────────────────────────────────────────────

QString DigiKamLibrary::buildFilePath(const QString &specificPath,
                                      const QString &relativePath,
                                      const QString &imageName)
{
    QString path = specificPath + relativePath + "/" + imageName;
    if (path.startsWith('/') && path.size() > 2 && path.at(2) == ':')
        path = path.mid(1);
    return QUrl::fromLocalFile(path).toString();
}

qreal DigiKamLibrary::effectiveAspect(int dbWidth, int dbHeight, int orientation)
{
    if (dbWidth <= 0 || dbHeight <= 0)
        return 1.5;
    // EXIF orientations 5–8 = 90°/270° rotation → swap axes
    if (orientation >= 5)
        return static_cast<qreal>(dbHeight) / dbWidth;
    return static_cast<qreal>(dbWidth) / dbHeight;
}

qreal DigiKamLibrary::aspectFromFile(const QString &localPath)
{
    QImageReader reader(localPath);
    reader.setAutoTransform(true);      // accounts for EXIF rotation
    const QSize size = reader.size();   // header-only read, no full decode
    if (size.width() > 0 && size.height() > 0)
        return static_cast<qreal>(size.width()) / size.height();
    return 1.5;
}
