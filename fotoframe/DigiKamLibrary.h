#pragma once

#include <QObject>
#include <QStringList>
#include <QList>
#include <QSqlDatabase>
#include <QMap>
#include <QSet>

// Single C++ object exposed to QML as a context property "photoLibrary".
// Reads tags and photos from the digikam SQLite database (read-only).
//
// Workflow:
//   1. On construction, opens digikam4.db via digikamrc and populates allTags.
//   2. QML binds a tag-selector to allTags / selectedTags.
//   3. When selectedTags changes, photos and aspectRatios are re-queried and
//      exposed to the river view.
class DigiKamLibrary : public QObject
{
    Q_OBJECT

    // All tag full-path names that have at least one visible image, sorted A-Z.
    Q_PROPERTY(QStringList  allTags       READ allTags       NOTIFY allTagsChanged)

    // Currently selected tag names (subset of allTags).
    Q_PROPERTY(QStringList  selectedTags  READ selectedTags  WRITE setSelectedTags
               NOTIFY selectedTagsChanged)

    // file:// URLs of matching photos, sorted by creationDate ASC.
    Q_PROPERTY(QStringList  photos        READ photos        NOTIFY photosChanged)

    // Display aspect ratio (width / height) for each entry in photos[].
    // EXIF orientation is already applied (portrait photos have ar < 1).
    Q_PROPERTY(QList<qreal> aspectRatios  READ aspectRatios  NOTIFY photosChanged)

    // Sum of all aspect ratios — used by QML to compute the seamless loop width
    // without iterating the list on every frame.
    Q_PROPERTY(qreal        totalAspectSum READ totalAspectSum NOTIFY photosChanged)

    // Empty on success; human-readable error text when the DB cannot be opened.
    Q_PROPERTY(QString      statusMessage READ statusMessage  NOTIFY statusMessageChanged)

public:
    explicit DigiKamLibrary(QObject *parent = nullptr);
    ~DigiKamLibrary() override;

    QStringList  allTags()        const { return m_allTags; }
    QStringList  selectedTags()   const { return m_selectedTags; }
    QStringList  photos()         const { return m_photos; }
    QList<qreal> aspectRatios()   const { return m_aspectRatios; }
    qreal        totalAspectSum() const { return m_totalAspectSum; }
    QString      statusMessage()  const { return m_statusMessage; }

    void setSelectedTags(const QStringList &tags);

signals:
    void allTagsChanged();
    void selectedTagsChanged();
    void photosChanged();
    void statusMessageChanged();

private:
    bool openDatabase();
    void loadTags();
    void refreshPhotos();

    static QString buildFilePath(const QString &specificPath,
                                 const QString &relativePath,
                                 const QString &imageName);
    static qreal effectiveAspect(int dbWidth, int dbHeight, int orientation);

    QSqlDatabase m_db;

    QStringList  m_allTags;
    QStringList  m_selectedTags;
    QStringList  m_photos;
    QList<qreal> m_aspectRatios;
    qreal        m_totalAspectSum = 0.0;
    QString      m_statusMessage;

    // Full tag path (e.g. "People/Alice") → list of tag IDs.
    // One path may map to multiple IDs when different subtrees share a name.
    QMap<QString, QList<int>> m_tagNameToIds;
};
