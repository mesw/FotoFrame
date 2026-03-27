#pragma once

#include <QObject>
#include <QStringList>
#include <QList>
#include <QSqlDatabase>
#include <QMap>
#include <QSet>

// Single C++ object exposed to QML as a context property "photoLibrary".
//
// Two source modes, selected automatically:
//   DB mode    — reads tags and photos from digikam4.db (located via digikamrc).
//   Folder mode — activated by calling setFolder(); subfolders become tags.
//
// In both modes the QML surface is identical:
//   allTags / selectedTags / photos / aspectRatios / totalAspectSum
class DigiKamLibrary : public QObject
{
    Q_OBJECT

    // All tag names that have at least one visible image, sorted A-Z.
    // DB mode   : full hierarchical paths ("People/Alice").
    // Folder mode: subfolder names; root-level images use the folder's own name.
    Q_PROPERTY(QStringList  allTags        READ allTags        NOTIFY allTagsChanged)

    // Currently selected tag names (subset of allTags).
    Q_PROPERTY(QStringList  selectedTags   READ selectedTags   WRITE setSelectedTags
               NOTIFY selectedTagsChanged)

    // file:// URLs of matching photos, sorted by date (DB) or filename (folder).
    Q_PROPERTY(QStringList  photos         READ photos         NOTIFY photosChanged)

    // Display aspect ratio (w/h) per photo, EXIF rotation already applied.
    Q_PROPERTY(QList<qreal> aspectRatios   READ aspectRatios   NOTIFY photosChanged)

    // Sum of all aspect ratios — lets QML compute loopWidth with one multiply.
    Q_PROPERTY(qreal        totalAspectSum READ totalAspectSum NOTIFY photosChanged)

    // True when digikam4.db was opened successfully.
    // False triggers the "select a folder" fallback UI in QML.
    Q_PROPERTY(bool         databaseAvailable READ databaseAvailable
               NOTIFY databaseAvailableChanged)

    // Human-readable status; empty on success, error text on failure.
    Q_PROPERTY(QString      statusMessage  READ statusMessage  NOTIFY statusMessageChanged)

public:
    explicit DigiKamLibrary(QObject *parent = nullptr);
    ~DigiKamLibrary() override;

    QStringList  allTags()           const { return m_allTags; }
    QStringList  selectedTags()      const { return m_selectedTags; }
    QStringList  photos()            const { return m_photos; }
    QList<qreal> aspectRatios()      const { return m_aspectRatios; }
    qreal        totalAspectSum()    const { return m_totalAspectSum; }
    bool         databaseAvailable() const { return m_databaseAvailable; }
    QString      statusMessage()     const { return m_statusMessage; }

    void setSelectedTags(const QStringList &tags);

    // Called from QML after a FolderDialog is accepted.
    // folderUrl is a file:// URL string as returned by FolderDialog.selectedFolder.
    Q_INVOKABLE void setFolder(const QString &folderUrl);

signals:
    void allTagsChanged();
    void selectedTagsChanged();
    void photosChanged();
    void databaseAvailableChanged();
    void statusMessageChanged();

private:
    // ── DB mode ──────────────────────────────────────────────────────────
    bool openDatabase();
    void loadDbTags();
    void refreshPhotosFromDb();

    static QString buildFilePath(const QString &specificPath,
                                 const QString &relativePath,
                                 const QString &imageName);
    static qreal effectiveAspect(int dbWidth, int dbHeight, int orientation);

    QSqlDatabase m_db;
    QMap<QString, QList<int>> m_tagNameToIds;  // full path → tag IDs

    // ── Folder mode ───────────────────────────────────────────────────────
    void loadFromFolder(const QString &localPath);
    void refreshPhotosFromFolder();

    static qreal aspectFromFile(const QString &localPath);

    QMap<QString, QStringList> m_folderTagFiles;  // tag name → sorted local paths

    // ── Shared state ──────────────────────────────────────────────────────
    void refreshPhotos();  // dispatches to DB or folder mode

    bool         m_databaseAvailable = false;
    bool         m_usingFolderMode   = false;

    QStringList  m_allTags;
    QStringList  m_selectedTags;
    QStringList  m_photos;
    QList<qreal> m_aspectRatios;
    qreal        m_totalAspectSum = 0.0;
    QString      m_statusMessage;
};
