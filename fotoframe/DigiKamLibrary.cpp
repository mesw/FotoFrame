#include "DigiKamLibrary.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QSettings>
#include <QStandardPaths>
#include <QFile>
#include <QUrl>
#include <QDebug>
#include <functional>

// ── Construction / destruction ─────────────────────────────────────────────

DigiKamLibrary::DigiKamLibrary(QObject *parent)
    : QObject(parent)
{
    if (openDatabase())
        loadTags();
}

DigiKamLibrary::~DigiKamLibrary()
{
    const QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase(); // detach the handle before removeDatabase
    QSqlDatabase::removeDatabase(connName);
}

// ── Database init ──────────────────────────────────────────────────────────

bool DigiKamLibrary::openDatabase()
{
    // digikam stores its config in %APPDATA%\digikam\digikamrc on Windows.
    // QStandardPaths::GenericConfigLocation → %APPDATA% on Windows.
    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString iniPath = configDir + "/digikam/digikamrc";

    if (!QFile::exists(iniPath)) {
        m_statusMessage = QStringLiteral("digikamrc not found at %1").arg(iniPath);
        emit statusMessageChanged();
        qWarning() << m_statusMessage;
        return false;
    }

    QSettings ini(iniPath, QSettings::IniFormat);
    ini.beginGroup("Database Settings");
    QString dbPath = ini.value("Database Name").toString();
    ini.endGroup();

    if (dbPath.isEmpty()) {
        m_statusMessage = QStringLiteral("'Database Name' key not found in digikamrc");
        emit statusMessageChanged();
        return false;
    }

    // KDE on Windows prefixes drive letters with a leading slash: "/C:/..." → "C:/..."
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

// ── Tag loading ────────────────────────────────────────────────────────────

void DigiKamLibrary::loadTags()
{
    // Step 1: load all tags as (id → {pid, name})
    QMap<int, QPair<int, QString>> raw;
    {
        QSqlQuery q(m_db);
        q.exec("SELECT id, pid, name FROM Tags");
        while (q.next())
            raw[q.value(0).toInt()] = { q.value(1).toInt(), q.value(2).toString() };
    }

    // Step 2: build full paths using memoised recursion so parent order doesn't matter
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

    // Step 3: collect IDs of tags that have at least one visible, in-scope image
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

    // Step 4: build path → IDs map (only for active tags)
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

// ── Photo query ────────────────────────────────────────────────────────────

void DigiKamLibrary::setSelectedTags(const QStringList &tags)
{
    if (m_selectedTags == tags)
        return;
    m_selectedTags = tags;
    emit selectedTagsChanged();
    refreshPhotos();
}

void DigiKamLibrary::refreshPhotos()
{
    m_photos.clear();
    m_aspectRatios.clear();
    m_totalAspectSum = 0.0;

    if (m_selectedTags.isEmpty() || !m_db.isOpen()) {
        emit photosChanged();
        return;
    }

    // Gather all tag IDs for the selected names
    QList<int> ids;
    for (const QString &name : std::as_const(m_selectedTags)) {
        const auto it = m_tagNameToIds.constFind(name);
        if (it != m_tagNameToIds.constEnd())
            ids.append(it.value());
    }
    // ids may now contain sublists; flatten is already done by append(QList)

    if (ids.isEmpty()) {
        emit photosChanged();
        return;
    }

    // Build "?, ?, ?" placeholder string
    QStringList ph;
    ph.reserve(ids.size());
    for (int i = 0; i < ids.size(); ++i) ph << "?";

    QSqlQuery q(m_db);
    q.prepare(QString(
        "SELECT DISTINCT "
        "  ar.specificPath, al.relativePath, i.name, "
        "  ii.creationDate, ii.width, ii.height, ii.orientation "
        "FROM Images i "
        "JOIN Albums         al ON al.id      = i.album "
        "JOIN AlbumRoots     ar ON ar.id      = al.albumRoot "
        "JOIN ImageInformation ii ON ii.imageid = i.id "
        "JOIN ImageTags      it ON it.imageid  = i.id "
        "WHERE it.tagid IN (%1) "
        "  AND i.status = 1 AND i.category = 1 "
        "ORDER BY ii.creationDate ASC, i.name ASC"
    ).arg(ph.join(',')));

    for (int id : std::as_const(ids))
        q.addBindValue(id);

    if (!q.exec()) {
        qWarning() << "Photo query failed:" << q.lastError().text();
        emit photosChanged();
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

    emit photosChanged();
}

// ── Static helpers ─────────────────────────────────────────────────────────

QString DigiKamLibrary::buildFilePath(const QString &specificPath,
                                      const QString &relativePath,
                                      const QString &imageName)
{
    QString path = specificPath + relativePath + "/" + imageName;
    // KDE on Windows stores "/C:/..." — strip the leading slash before the drive letter
    if (path.startsWith('/') && path.size() > 2 && path.at(2) == ':')
        path = path.mid(1);
    return QUrl::fromLocalFile(path).toString();
}

qreal DigiKamLibrary::effectiveAspect(int dbWidth, int dbHeight, int orientation)
{
    if (dbWidth <= 0 || dbHeight <= 0)
        return 1.5; // 3:2 fallback

    // EXIF orientations 5–8 are 90°/270° rotations: display width ↔ height
    if (orientation >= 5)
        return static_cast<qreal>(dbHeight) / dbWidth;
    return static_cast<qreal>(dbWidth) / dbHeight;
}
