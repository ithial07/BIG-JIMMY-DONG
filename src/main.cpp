#include <QtWidgets>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QStandardPaths>
#include <QDateTime>
#include <cmath>
#include <utility>
#include <functional>
#include <algorithm>

struct Room {
    int id = 0;
    int mudVnum = 0;
    int area = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int env = -1;
    int weight = 1;
    bool locked = false;
    QString name;
    QString terrain;
    QMap<QString, int> exits;
    QMap<QString, int> special;

    QMap<QString, int> allExits() const {
        QMap<QString, int> out = exits;
        for (auto it = special.cbegin(); it != special.cend(); ++it)
            out.insert(it.key(), it.value());
        return out;
    }
};

struct AreaInfo {
    int id = 0;
    QString name;
    int minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
};

struct TriggerRule {
    QString name;
    QString pattern;
    QString command;
    QString script;
    QString builtin;
    bool enabled = true;
};

struct ScriptRule {
    QString name;
    QString registeredEvents;
    QString userEvent;
    QString script;
    bool enabled = true;
};


class TerminalTextEdit : public QTextEdit {
public:
    explicit TerminalTextEdit(QWidget* parent = nullptr) : QTextEdit(parent) {
        viewport()->setAutoFillBackground(false);
        setAutoFillBackground(false);
        viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
        setTextColor(QColor(235, 235, 235));
        QPalette pal = palette();
        pal.setColor(QPalette::Base, QColor(0, 0, 0));
        pal.setColor(QPalette::Text, QColor(235, 235, 235));
        setPalette(pal);
    }

    void setStaticBackgroundImage(const QString& path) {
        QPixmap pix(path);
        if (pix.isNull()) return;
        m_backgroundPath = path;
        m_backgroundPixmap = pix;
        viewport()->setAutoFillBackground(false);
        setAutoFillBackground(false);
        update();
    }

    void clearStaticBackgroundImage() {
        m_backgroundPath.clear();
        m_backgroundPixmap = QPixmap();
        update();
    }

    QString staticBackgroundImagePath() const { return m_backgroundPath; }

protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), QColor(0, 0, 0));
        if (!m_backgroundPixmap.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawPixmap(viewport()->rect(), m_backgroundPixmap, m_backgroundPixmap.rect());
            // Dark glass overlay so ANSI text stays readable even on pale images.
            painter.fillRect(viewport()->rect(), QColor(0, 0, 0, 155));
        }
        painter.end();
        QTextEdit::paintEvent(event);
    }

private:
    QString m_backgroundPath;
    QPixmap m_backgroundPixmap;
};

class MapData {
public:
    QHash<int, Room> rooms;
    QHash<int, int> mudVnumToRoomId; // Real MUD room number from text like (#31902) -> internal mapper room id.
    QHash<int, AreaInfo> areas;
    QString source;
    int version = 0;

    bool load(const QString& resourcePath, QString* error) {
        QFile file(resourcePath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("Could not open %1").arg(resourcePath);
            return false;
        }
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            if (error) *error = QStringLiteral("Map JSON parse error: %1").arg(parseError.errorString());
            return false;
        }
        QJsonObject root = doc.object();
        rooms.clear();
        mudVnumToRoomId.clear();
        areas.clear();
        source = root.value(QStringLiteral("source")).toString();
        version = root.value(QStringLiteral("version")).toInt();

        for (const QJsonValue& av : root.value(QStringLiteral("areas")).toArray()) {
            const QJsonObject a = av.toObject();
            AreaInfo info;
            info.id = a.value(QStringLiteral("id")).toInt();
            info.name = a.value(QStringLiteral("name")).toString(QString::number(info.id));
            const QJsonObject b = a.value(QStringLiteral("bounds")).toObject();
            info.minX = b.value(QStringLiteral("minX")).toInt();
            info.maxX = b.value(QStringLiteral("maxX")).toInt();
            info.minY = b.value(QStringLiteral("minY")).toInt();
            info.maxY = b.value(QStringLiteral("maxY")).toInt();
            info.minZ = b.value(QStringLiteral("minZ")).toInt();
            info.maxZ = b.value(QStringLiteral("maxZ")).toInt();
            areas.insert(info.id, info);
        }

        for (const QJsonValue& rv : root.value(QStringLiteral("rooms")).toArray()) {
            const QJsonObject r = rv.toObject();
            Room room;
            room.id = r.value(QStringLiteral("id")).toInt();
            room.area = r.value(QStringLiteral("area")).toInt();
            room.x = r.value(QStringLiteral("x")).toInt();
            room.y = r.value(QStringLiteral("y")).toInt();
            room.z = r.value(QStringLiteral("z")).toInt();
            room.env = r.value(QStringLiteral("env")).toInt(-1);
            room.weight = r.value(QStringLiteral("weight")).toInt(1);
            room.locked = r.value(QStringLiteral("locked")).toBool(false);
            room.name = r.value(QStringLiteral("name")).toString();
            room.terrain = r.value(QStringLiteral("terrain")).toString();

            // The Mudlet map's internal room id is not the same as the room number printed by the MUD.
            // Example: map id 13017 has name "Dark Tunnel Passage (#31902) [ Floor ]".
            // The game prints #31902, so build an index from the printed MUD number to the mapper id.
            static const QRegularExpression mudVnumRe(QStringLiteral("\\(#\\s*(\\d+)\\)"));
            const QRegularExpressionMatch vnumMatch = mudVnumRe.match(room.name);
            if (vnumMatch.hasMatch()) {
                bool vnumOk = false;
                const int mudVnum = vnumMatch.captured(1).toInt(&vnumOk);
                if (vnumOk) {
                    room.mudVnum = mudVnum;
                    mudVnumToRoomId.insert(mudVnum, room.id);
                }
            }

            const QJsonObject exits = r.value(QStringLiteral("exits")).toObject();
            for (auto it = exits.constBegin(); it != exits.constEnd(); ++it)
                room.exits.insert(it.key(), it.value().toInt());
            const QJsonObject special = r.value(QStringLiteral("special")).toObject();
            for (auto it = special.constBegin(); it != special.constEnd(); ++it)
                room.special.insert(it.key(), it.value().toInt());
            rooms.insert(room.id, room);
        }
        return true;
    }

    QList<QPair<QString, int>> path(int fromId, int toId) const {
        if (!rooms.contains(fromId) || !rooms.contains(toId) || fromId == toId)
            return {};

        QQueue<int> q;
        QHash<int, QPair<int, QString>> prev;
        q.enqueue(fromId);
        prev.insert(fromId, qMakePair(0, QString()));

        while (!q.isEmpty()) {
            int cur = q.dequeue();
            if (cur == toId) break;
            const Room& room = rooms[cur];
            const QMap<QString, int> exits = room.allExits();
            for (auto it = exits.cbegin(); it != exits.cend(); ++it) {
                const int next = it.value();
                if (!rooms.contains(next) || prev.contains(next)) continue;
                prev.insert(next, qMakePair(cur, it.key()));
                q.enqueue(next);
            }
        }
        if (!prev.contains(toId)) return {};

        QList<QPair<QString, int>> reversed;
        int cur = toId;
        while (cur != fromId) {
            const auto p = prev.value(cur);
            reversed.append(qMakePair(p.second, cur));
            cur = p.first;
        }
        QList<QPair<QString, int>> out;
        for (int i = reversed.size() - 1; i >= 0; --i)
            out.append(reversed.at(i));
        return out;
    }

    QList<const Room*> searchRooms(const QString& needle, int limit = 50) const {
        QList<const Room*> results;
        const QString n = needle.trimmed().toLower();
        if (n.isEmpty()) return results;
        const bool exactNumber = QRegularExpression(QStringLiteral("^#?\\d+$")).match(n).hasMatch();
        for (auto it = rooms.cbegin(); it != rooms.cend(); ++it) {
            const Room& r = it.value();
            const QString areaName = areas.contains(r.area) ? areas[r.area].name : QString::number(r.area);
            const bool hit = (exactNumber && QString::number(r.id) == n.mid(n.startsWith('#') ? 1 : 0))
                || r.name.toLower().contains(n)
                || r.terrain.toLower().contains(n)
                || areaName.toLower().contains(n)
                || QString::number(r.id).contains(n);
            if (hit) {
                results.append(&r);
                if (results.size() >= limit) break;
            }
        }
        return results;
    }

    QList<int> sortedAreaIds() const {
        QList<int> ids = areas.keys();
        std::sort(ids.begin(), ids.end(), [this](int a, int b) {
            return areas.value(a).name.toLower() < areas.value(b).name.toLower();
        });
        return ids;
    }

    int resolveRoomNumber(int printedOrInternalNumber) const {
        // Prefer the printed MUD room number, because many MUD vnums overlap
        // with Mudlet's internal mapper ids. Example: a printed #7969 can
        // accidentally be a different mapper id if we check rooms first.
        if (mudVnumToRoomId.contains(printedOrInternalNumber)) return mudVnumToRoomId.value(printedOrInternalNumber);
        if (rooms.contains(printedOrInternalNumber)) return printedOrInternalNumber;
        return 0;
    }

    int mudVnumForRoom(const Room& room) const {
        if (room.mudVnum > 0) return room.mudVnum;
        static const QRegularExpression mudVnumRe(QStringLiteral("\\(#\\s*(\\d+)\\)"));
        const QRegularExpressionMatch m = mudVnumRe.match(room.name);
        if (m.hasMatch()) {
            bool ok = false;
            const int vnum = m.captured(1).toInt(&ok);
            if (ok && vnum > 0) return vnum;
        }
        for (auto it = mudVnumToRoomId.cbegin(); it != mudVnumToRoomId.cend(); ++it) {
            if (it.value() == room.id) return it.key();
        }
        return 0;
    }

    int nextFreeRoomId() const {
        int maxId = 0;
        for (auto it = rooms.cbegin(); it != rooms.cend(); ++it) maxId = qMax(maxId, it.key());
        return maxId + 1;
    }

    int roomAtPosition(int area, int x, int y, int z) const {
        for (auto it = rooms.cbegin(); it != rooms.cend(); ++it) {
            const Room& r = it.value();
            if (r.area == area && r.x == x && r.y == y && r.z == z) return r.id;
        }
        return 0;
    }
};

static QString terrainEmojiFor(const Room& r) {
    // Auto-icon rules.
    // IMPORTANT:
    // 1) [ Floor ] always wins first, regardless of the room name.
    // 2) We do NOT special-case room names containing "cave" / "tunnel" here.
    //    That keeps cave-named floor rooms using the chosen floor tile and avoids extra lag/confusion.
    const QString name = r.name.toLower();
    const QString terrain = r.terrain.toLower();
    const QString s = terrain + QLatin1Char(' ') + name;

    if (terrain.contains(QStringLiteral("floor"))) return QStringLiteral("icon:decorative_cobblestone_tile_icon.png");

    if (name.contains(QStringLiteral("stable")) || name.contains(QStringLiteral("stables"))) return QStringLiteral("🐴");
    if (name.contains(QStringLiteral("butcher"))) return QStringLiteral("icon:butcher_s_pride_emblem.png");
    if (name.contains(QStringLiteral("tailor"))) return QStringLiteral("icon:tailor_s_craftsmanship_emblem.png");
    if (name.contains(QStringLiteral("blacksmith")) || name.contains(QStringLiteral("black smith")) || name.contains(QStringLiteral("forge"))) return QStringLiteral("icon:blacksmith_forge_emblem_icon.png");
    if (name.contains(QStringLiteral("armoury")) || name.contains(QStringLiteral("armory")) || name.contains(QStringLiteral("armour")) || name.contains(QStringLiteral("armor"))) return QStringLiteral("icon:medieval_armor_emblem_with_shield.png");
    if (name.contains(QStringLiteral("shop")) || name.contains(QStringLiteral("store")) || name.contains(QStringLiteral("market"))) return QStringLiteral("icon:merchant_s_crate_and_supplies_emblem.png");
    if (name.contains(QStringLiteral(" inn")) || name.startsWith(QStringLiteral("inn")) || name.contains(QStringLiteral("tavern")) || name.contains(QStringLiteral("alehouse"))) return QStringLiteral("icon:medieval_tavern_beer_mug_emblem.png");
    if (name.contains(QStringLiteral("gate"))) return QStringLiteral("icon:medieval_fortress_gate_emblem.png");
    if (name.contains(QStringLiteral("trophy")) || name.contains(QStringLiteral("victory"))) return QStringLiteral("icon:golden_trophy_with_laurel_wreath.png");
    if (name.contains(QStringLiteral("skeleton")) || name.contains(QStringLiteral("undead")) || name.contains(QStringLiteral("crypt"))) return QStringLiteral("icon:undead_warrior_emblem_with_battered_helm.png");
    if (name.contains(QStringLiteral("bandit")) || name.contains(QStringLiteral("rogue")) || name.contains(QStringLiteral("thief"))) return QStringLiteral("icon:rogue_assassin_emblem_design.png");

    if (s.contains(QStringLiteral("water")) || s.contains(QStringLiteral("river")) || s.contains(QStringLiteral("ford")) || s.contains(QStringLiteral("lake"))) return QStringLiteral("🌊");
    if (s.contains(QStringLiteral("forest")) || s.contains(QStringLiteral("wood")) || s.contains(QStringLiteral("tree"))) return QStringLiteral("🌲");
    if (s.contains(QStringLiteral("road")) || s.contains(QStringLiteral("path")) || s.contains(QStringLiteral("trail")) || s.contains(QStringLiteral("street"))) return QStringLiteral("🟫");
    if (s.contains(QStringLiteral("wall")) || s.contains(QStringLiteral("tower")) || s.contains(QStringLiteral("city")) || s.contains(QStringLiteral("town"))) return QStringLiteral("🏰");
    if (s.contains(QStringLiteral("mount")) || s.contains(QStringLiteral("cliff")) || s.contains(QStringLiteral("ridge")) || s.contains(QStringLiteral("peak"))) return QStringLiteral("⛰️");
    if (s.contains(QStringLiteral("hill"))) return QStringLiteral("🌄");
    if (s.contains(QStringLiteral("field")) || s.contains(QStringLiteral("farm")) || s.contains(QStringLiteral("plain"))) return QStringLiteral("🌾");
    if (s.contains(QStringLiteral("swamp")) || s.contains(QStringLiteral("marsh"))) return QStringLiteral("🐸");
    if (s.contains(QStringLiteral("desert")) || s.contains(QStringLiteral("sand"))) return QStringLiteral("🏜️");
    return QStringLiteral("▪️");
}


static QColor terrainColorFor(const Room& r) {
    const QString s = (r.terrain + QLatin1Char(' ') + r.name).toLower();
    if (s.contains(QStringLiteral("water")) || s.contains(QStringLiteral("river"))) return QColor(35, 90, 150);
    if (s.contains(QStringLiteral("forest")) || s.contains(QStringLiteral("wood"))) return QColor(35, 115, 55);
    if (s.contains(QStringLiteral("cave")) || s.contains(QStringLiteral("tunnel")) || s.contains(QStringLiteral("mine"))) return QColor(80, 80, 80);
    if (s.contains(QStringLiteral("road")) || s.contains(QStringLiteral("path")) || s.contains(QStringLiteral("street"))) return QColor(155, 135, 45);
    if (s.contains(QStringLiteral("city")) || s.contains(QStringLiteral("town")) || s.contains(QStringLiteral("gate"))) return QColor(120, 110, 105);
    if (s.contains(QStringLiteral("field")) || s.contains(QStringLiteral("farm")) || s.contains(QStringLiteral("plain"))) return QColor(105, 130, 45);
    return QColor(70, 70, 70);
}

class MapWidget : public QWidget {
public:
    explicit MapWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumWidth(120);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);

        // Pulse the current-room marker so it is very easy to see while walking.
        QTimer* pulseTimer = new QTimer(this);
        connect(pulseTimer, &QTimer::timeout, this, [this]() {
            if (m_data && m_data->rooms.contains(m_currentRoomId)) update();
        });
        pulseTimer->start(160);
    }

    void setMap(MapData* data) { m_data = data; update(); }
    int currentRoomId() const { return m_currentRoomId; }
    int areaId() const { return m_area; }
    int zLevel() const { return m_z; }

    void setEmojiEnabled(bool on) { m_emojiEnabled = on; update(); }
    void setNamesEnabled(bool on) { m_namesEnabled = on; update(); }
    void setGridEnabled(bool on) { m_gridEnabled = on; update(); }
    void setRoomShape(const QString& shape) { m_roomShape = shape.toLower() == QStringLiteral("circle") ? QStringLiteral("circle") : QStringLiteral("square"); update(); }
    QString roomShape() const { return m_roomShape; }
    void setLineColor(const QColor& c) { if (c.isValid()) { m_lineColor = c; update(); } }
    void setHighlightColor(const QColor& c) { if (c.isValid()) { m_highlightColor = c; update(); } }
    void setPulseColor(const QColor& c) { if (c.isValid()) { m_pulseColor = c; update(); } }
    void setMappingEnabled(bool on) { m_mappingEnabled = on; update(); }
    bool mappingEnabled() const { return m_mappingEnabled; }

    void setCustomizationPath(const QString& path) { m_customizationPath = path; }

    QString customizationPath() const { return m_customizationPath; }

    void loadMapCustomizations(const QString& path) {
        if (!m_data || path.trimmed().isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return;

        m_customEmoji.clear();
        m_customColor.clear();
        m_customNote.clear();
        m_changedRooms.clear();
        m_deletedRooms.clear();

        const QJsonArray deleted = doc.object().value(QStringLiteral("deletedRooms")).toArray();
        for (const QJsonValue& value : deleted) {
            const int id = value.toInt(0);
            if (id > 0) m_deletedRooms.insert(id);
        }
        for (int id : std::as_const(m_deletedRooms)) m_data->rooms.remove(id);

        const QJsonArray rooms = doc.object().value(QStringLiteral("rooms")).toArray();
        for (const QJsonValue& value : rooms) {
            const QJsonObject o = value.toObject();
            const int id = o.value(QStringLiteral("id")).toInt(0);
            if (id <= 0) continue;
            if (!m_data->rooms.contains(id)) {
                Room created;
                created.id = id;
                created.name = o.value(QStringLiteral("name")).toString(QStringLiteral("Mapped Room (#%1)").arg(id));
                created.terrain = o.value(QStringLiteral("terrain")).toString(QStringLiteral("[ Mapped ]"));
                created.area = o.value(QStringLiteral("area")).toInt(m_area);
                created.x = o.value(QStringLiteral("x")).toInt(0);
                created.y = o.value(QStringLiteral("y")).toInt(0);
                created.z = o.value(QStringLiteral("z")).toInt(0);
                m_data->rooms.insert(id, created);
            }
            Room& r = m_data->rooms[id];
            bool changedRoomData = false;
            if (o.contains(QStringLiteral("name"))) { r.name = o.value(QStringLiteral("name")).toString(r.name); changedRoomData = true; }
            if (o.contains(QStringLiteral("terrain"))) { r.terrain = o.value(QStringLiteral("terrain")).toString(r.terrain); changedRoomData = true; }
            if (o.contains(QStringLiteral("x"))) { r.x = o.value(QStringLiteral("x")).toInt(r.x); changedRoomData = true; }
            if (o.contains(QStringLiteral("y"))) { r.y = o.value(QStringLiteral("y")).toInt(r.y); changedRoomData = true; }
            if (o.contains(QStringLiteral("z"))) { r.z = o.value(QStringLiteral("z")).toInt(r.z); changedRoomData = true; }
            if (o.contains(QStringLiteral("area"))) { r.area = o.value(QStringLiteral("area")).toInt(r.area); changedRoomData = true; }
            const QJsonObject exitsObj = o.value(QStringLiteral("exits")).toObject();
            if (!exitsObj.isEmpty()) {
                r.exits.clear();
                for (auto ex = exitsObj.constBegin(); ex != exitsObj.constEnd(); ++ex) r.exits.insert(ex.key(), ex.value().toInt());
                changedRoomData = true;
            }
            if (changedRoomData) m_changedRooms.insert(id);

            const QString emoji = o.value(QStringLiteral("emoji")).toString();
            if (!emoji.isEmpty()) m_customEmoji.insert(id, emoji);

            const QString color = o.value(QStringLiteral("color")).toString();
            if (!color.isEmpty()) {
                const QColor c(color);
                if (c.isValid()) m_customColor.insert(id, c);
            }

            const QString note = o.value(QStringLiteral("note")).toString();
            if (!note.isEmpty()) m_customNote.insert(id, note);
        }
        update();
    }

    bool saveMapCustomizations(const QString& path = QString(), QString* error = nullptr) const {
        const QString target = path.trimmed().isEmpty() ? m_customizationPath : path;
        if (target.trimmed().isEmpty()) {
            if (error) *error = QStringLiteral("No map save path was configured.");
            return false;
        }

        QSet<int> ids = m_changedRooms;
        for (auto it = m_customEmoji.cbegin(); it != m_customEmoji.cend(); ++it) ids.insert(it.key());
        for (auto it = m_customColor.cbegin(); it != m_customColor.cend(); ++it) ids.insert(it.key());
        for (auto it = m_customNote.cbegin(); it != m_customNote.cend(); ++it) ids.insert(it.key());

        QJsonArray roomArray;
        QList<int> sortedIds = ids.values();
        std::sort(sortedIds.begin(), sortedIds.end());
        for (int id : sortedIds) {
            if (!m_data || !m_data->rooms.contains(id)) continue;
            const Room& r = m_data->rooms[id];
            QJsonObject o;
            o.insert(QStringLiteral("id"), id);
            if (m_changedRooms.contains(id)) {
                o.insert(QStringLiteral("name"), r.name);
                o.insert(QStringLiteral("terrain"), r.terrain);
                o.insert(QStringLiteral("area"), r.area);
                o.insert(QStringLiteral("x"), r.x);
                o.insert(QStringLiteral("y"), r.y);
                o.insert(QStringLiteral("z"), r.z);
                QJsonObject exitsObj;
                for (auto ex = r.exits.cbegin(); ex != r.exits.cend(); ++ex) exitsObj.insert(ex.key(), ex.value());
                o.insert(QStringLiteral("exits"), exitsObj);
            }
            if (m_customEmoji.contains(id)) o.insert(QStringLiteral("emoji"), m_customEmoji.value(id));
            if (m_customColor.contains(id)) o.insert(QStringLiteral("color"), m_customColor.value(id).name(QColor::HexRgb));
            if (m_customNote.contains(id)) o.insert(QStringLiteral("note"), m_customNote.value(id));
            roomArray.append(o);
        }

        QJsonObject root;
        root.insert(QStringLiteral("format"), QStringLiteral("ArdaBestClient map customization file"));
        root.insert(QStringLiteral("savedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        root.insert(QStringLiteral("rooms"), roomArray);
        QJsonArray deletedArray;
        QList<int> deletedIds = m_deletedRooms.values();
        std::sort(deletedIds.begin(), deletedIds.end());
        for (int id : deletedIds) deletedArray.append(id);
        root.insert(QStringLiteral("deletedRooms"), deletedArray);

        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile f(target);
        if (!f.open(QIODevice::WriteOnly)) {
            if (error) *error = QStringLiteral("Could not write %1").arg(target);
            return false;
        }
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return true;
    }

    void setAreaAndZ(int area, int z) {
        m_area = area;
        m_z = z;
        update();
    }

    void centerCurrent() {
        if (m_data && m_data->rooms.contains(m_currentRoomId)) centerOn(m_data->rooms[m_currentRoomId]);
        update();
    }

    void setCurrentRoom(int roomId, bool center = true) {
        if (!m_data || !m_data->rooms.contains(roomId)) return;
        m_currentRoomId = roomId;
        const Room& r = m_data->rooms[roomId];
        m_area = r.area;
        m_z = r.z;
        if (center) centerOn(r);
        update();
    }

    QString roomSummary() const {
        if (!m_data || !m_data->rooms.contains(m_currentRoomId))
            return QStringLiteral("No room selected");
        const Room& r = m_data->rooms[m_currentRoomId];
        const QString areaName = m_data->areas.contains(r.area) ? m_data->areas[r.area].name : QString::number(r.area);
        const int mudVnum = m_data->mudVnumForRoom(r);
        const QString idText = mudVnum > 0
            ? QStringLiteral("MUD #%1  mapper #%2").arg(mudVnum).arg(r.id)
            : QStringLiteral("mapper #%1").arg(r.id);
        return QStringLiteral("📍 %1  %2  | Area: %3  z:%4  x:%5 y:%6")
            .arg(idText).arg(r.name).arg(areaName).arg(r.z).arg(r.x).arg(r.y);
    }

    static QString normalizeDirection(QString dir) {
        dir = dir.trimmed().toLower();
        if (dir == QStringLiteral("n") || dir == QStringLiteral("north")) return QStringLiteral("n");
        if (dir == QStringLiteral("s") || dir == QStringLiteral("south")) return QStringLiteral("s");
        if (dir == QStringLiteral("e") || dir == QStringLiteral("east")) return QStringLiteral("e");
        if (dir == QStringLiteral("w") || dir == QStringLiteral("west")) return QStringLiteral("w");
        if (dir == QStringLiteral("u") || dir == QStringLiteral("up") || dir == QStringLiteral("upwards")) return QStringLiteral("u");
        if (dir == QStringLiteral("d") || dir == QStringLiteral("down") || dir == QStringLiteral("downwards")) return QStringLiteral("d");
        return QString();
    }

    static QString oppositeDirection(const QString& dir) {
        if (dir == QStringLiteral("n")) return QStringLiteral("s");
        if (dir == QStringLiteral("s")) return QStringLiteral("n");
        if (dir == QStringLiteral("e")) return QStringLiteral("w");
        if (dir == QStringLiteral("w")) return QStringLiteral("e");
        if (dir == QStringLiteral("u")) return QStringLiteral("d");
        if (dir == QStringLiteral("d")) return QStringLiteral("u");
        return QString();
    }

    static QPoint dirDelta2D(const QString& dir) {
        if (dir == QStringLiteral("n")) return QPoint(0, 1);
        if (dir == QStringLiteral("s")) return QPoint(0, -1);
        if (dir == QStringLiteral("e")) return QPoint(1, 0);
        if (dir == QStringLiteral("w")) return QPoint(-1, 0);
        return QPoint(0, 0);
    }

    int createRoomFromMapping(const QString& moveDir, int printedMudVnum, const QString& roomName, const QString& terrain, const QStringList& exitWords) {
        if (!m_data || !m_data->rooms.contains(m_currentRoomId)) return 0;
        const QString dir = normalizeDirection(moveDir);
        if (dir.isEmpty()) return 0;
        Room& from = m_data->rooms[m_currentRoomId];
        int nx = from.x;
        int ny = from.y;
        int nz = from.z;
        const QPoint d = dirDelta2D(dir);
        nx += d.x();
        ny += d.y();
        if (dir == QStringLiteral("u")) nz += 1;
        if (dir == QStringLiteral("d")) nz -= 1;

        int newId = m_data->resolveRoomNumber(printedMudVnum);
        if (newId <= 0) newId = m_data->roomAtPosition(from.area, nx, ny, nz);
        if (newId <= 0) {
            newId = m_data->nextFreeRoomId();
            Room r;
            r.id = newId;
            r.mudVnum = printedMudVnum;
            r.area = from.area;
            r.x = nx;
            r.y = ny;
            r.z = nz;
            r.name = roomName.trimmed().isEmpty() ? QStringLiteral("Mapped Room (#%1)").arg(printedMudVnum > 0 ? printedMudVnum : newId) : roomName.trimmed();
            if (printedMudVnum > 0 && !r.name.contains(QStringLiteral("(#"))) r.name += QStringLiteral(" (#%1)").arg(printedMudVnum);
            r.terrain = terrain.trimmed().isEmpty() ? QStringLiteral("[ Mapped ]") : terrain.trimmed();
            m_data->rooms.insert(newId, r);
            if (printedMudVnum > 0) m_data->mudVnumToRoomId.insert(printedMudVnum, newId);
        }

        linkRooms(m_currentRoomId, dir, newId);
        Room& to = m_data->rooms[newId];
        if (printedMudVnum > 0) {
            to.mudVnum = printedMudVnum;
            m_data->mudVnumToRoomId.insert(printedMudVnum, newId);
        }
        if (!roomName.trimmed().isEmpty()) to.name = roomName.trimmed().contains(QStringLiteral("(#")) ? roomName.trimmed() : roomName.trimmed() + (printedMudVnum > 0 ? QStringLiteral(" (#%1)").arg(printedMudVnum) : QString());
        if (!terrain.trimmed().isEmpty()) to.terrain = terrain.trimmed();
        for (const QString& e : exitWords) {
            const QString nd = normalizeDirection(e);
            if (!nd.isEmpty() && nd == oppositeDirection(dir)) to.exits.insert(nd, m_currentRoomId);
        }
        m_changedRooms.insert(m_currentRoomId);
        m_changedRooms.insert(newId);
        setCurrentRoom(newId, true);
        return newId;
    }

    void linkRooms(int fromId, const QString& dir, int toId) {
        if (!m_data || !m_data->rooms.contains(fromId) || !m_data->rooms.contains(toId)) return;
        const QString nd = normalizeDirection(dir);
        const QString opp = oppositeDirection(nd);
        if (nd.isEmpty() || opp.isEmpty()) return;
        m_data->rooms[fromId].exits.insert(nd, toId);
        m_data->rooms[toId].exits.insert(opp, fromId);
        m_changedRooms.insert(fromId);
        m_changedRooms.insert(toId);
        update();
    }

    void deleteRoomAndExits(int roomId) {
        if (!m_data || !m_data->rooms.contains(roomId)) return;
        for (auto it = m_data->rooms.begin(); it != m_data->rooms.end(); ++it) {
            Room& r = it.value();
            for (auto ex = r.exits.begin(); ex != r.exits.end();) {
                if (ex.value() == roomId) ex = r.exits.erase(ex);
                else ++ex;
            }
            m_changedRooms.insert(r.id);
        }
        m_data->rooms.remove(roomId);
        m_customEmoji.remove(roomId);
        m_customColor.remove(roomId);
        m_customNote.remove(roomId);
        m_changedRooms.remove(roomId);
        m_deletedRooms.insert(roomId);
        if (m_currentRoomId == roomId) m_currentRoomId = m_data->rooms.isEmpty() ? 0 : m_data->rooms.cbegin().key();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor(11, 12, 15));

        if (!m_data || m_data->rooms.isEmpty()) {
            p.setPen(Qt::white);
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No map loaded"));
            return;
        }

        if (m_gridEnabled) drawGrid(p);

        const QString areaName = m_data->areas.contains(m_area) ? m_data->areas[m_area].name : QString::number(m_area);
        p.setPen(QColor(220, 220, 220));
        QFont titleFont = p.font();
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.drawText(12, 24, QStringLiteral("🧭 ARDABEST automapper - %1  z:%2  zoom:%3")
                   .arg(areaName).arg(m_z).arg(m_zoom, 0, 'f', 1));
        titleFont.setBold(false);
        p.setFont(titleFont);

        // Neon-green mapper theme: exits are drawn with a soft glow first,
        // then a bright green core line on top.
        for (auto it = m_data->rooms.cbegin(); it != m_data->rooms.cend(); ++it) {
            const Room& r = it.value();
            if (r.area != m_area || r.z != m_z) continue;
            const QPointF a = screenPoint(r);
            const QMap<QString, int> exits = r.allExits();
            for (auto ex = exits.cbegin(); ex != exits.cend(); ++ex) {
                if (!m_data->rooms.contains(ex.value())) continue;
                const Room& to = m_data->rooms[ex.value()];
                if (to.area != m_area || to.z != m_z) continue;
                const QPointF b = screenPoint(to);
                QColor glowLine = m_lineColor;
                glowLine.setAlpha(85);
                p.setPen(QPen(glowLine, qMax(2.8, m_zoom * 0.18), Qt::SolidLine, Qt::RoundCap));
                p.drawLine(a, b);
                QColor coreLine = m_lineColor.lighter(135);
                coreLine.setAlpha(235);
                p.setPen(QPen(coreLine, qMax(1.1, m_zoom * 0.065), Qt::SolidLine, Qt::RoundCap));
                p.drawLine(a, b);
            }
        }

        const double radius = qBound(3.0, m_zoom * 0.30, 13.0);
        QFont emojiFont = p.font();
        emojiFont.setPointSizeF(qBound(7.0, m_zoom * 0.45, 18.0));
        QFont labelFont = p.font();
        labelFont.setPointSize(8);

        const Room* currentRoomToDrawLast = nullptr;

        for (auto it = m_data->rooms.cbegin(); it != m_data->rooms.cend(); ++it) {
            const Room& r = it.value();
            if (r.area != m_area || r.z != m_z) continue;
            if (r.id == m_currentRoomId) {
                currentRoomToDrawLast = &r;
                continue;
            }
            const QPointF pt = screenPoint(r);
            if (!rect().adjusted(-80, -80, 80, 80).contains(pt.toPoint())) continue;
            QRectF box(pt.x() - radius, pt.y() - radius, radius * 2, radius * 2);
            QColor roomGlow = m_highlightColor;
            roomGlow.setAlpha(90);
            p.setPen(QPen(roomGlow, qMax(3.0, radius * 0.34)));
            p.setBrush(Qt::NoBrush);
            drawRoomBody(p, box.adjusted(-1.2, -1.2, 1.2, 1.2));
            QColor roomCore = m_highlightColor.lighter(130);
            roomCore.setAlpha(235);
            p.setPen(QPen(roomCore, qMax(1.1, radius * 0.16)));
            p.setBrush(colorForRoom(r));
            drawRoomBody(p, box);

            if (m_emojiEnabled && m_zoom >= 14.0) {
                drawRoomIcon(p, r, pt, radius, emojiFont);
            }
            if (m_namesEnabled && m_zoom >= 20.0) {
                p.setFont(labelFont);
                p.setPen(QColor(235, 235, 235));
                p.drawText(QPointF(pt.x() + radius + 3, pt.y() - 2), QStringLiteral("#%1 %2").arg(r.id).arg(r.name.left(24)));
            }
        }

        // Draw the current room LAST, but DO NOT cover its icon.
        // The room keeps whatever emoji/icon it already has, and the player marker
        // is only a pulsing neon-green ring/glow around that icon.
        if (currentRoomToDrawLast) {
            const Room& r = *currentRoomToDrawLast;
            const QPointF pt = screenPoint(r);
            if (rect().adjusted(-180, -180, 180, 180).contains(pt.toPoint())) {
                const double pulse = (std::sin(QDateTime::currentMSecsSinceEpoch() / 170.0) + 1.0) * 0.5;
                // Keep the pulse the SAME SIZE as the room square/circle so it does not hide nearby rooms or exit lines.
                const double glowRadius = radius * (1.55 + pulse * 0.25);
                const double outerRingRadius = radius * (1.28 + pulse * 0.18);
                const double innerRingRadius = radius * 1.02;

                // Soft glow outside the icon only.
                p.setPen(Qt::NoPen);
                QColor pulseA = m_pulseColor; pulseA.setAlpha(30);
                p.setBrush(pulseA);
                p.drawEllipse(pt, glowRadius, glowRadius);
                QColor pulseB = m_pulseColor; pulseB.setAlpha(48);
                p.setBrush(pulseB);
                p.drawEllipse(pt, glowRadius * 0.66, glowRadius * 0.66);

                // Pulsing neon rings around the room icon.
                p.setBrush(Qt::NoBrush);
                QColor pulseRing = m_pulseColor; pulseRing.setAlpha(210);
                p.setPen(QPen(pulseRing, qMax(2.4, radius * 0.30)));
                p.drawEllipse(pt, outerRingRadius, outerRingRadius);
                QColor pulseInner = m_pulseColor.lighter(175); pulseInner.setAlpha(240);
                p.setPen(QPen(pulseInner, qMax(1.4, radius * 0.16)));
                p.drawEllipse(pt, innerRingRadius, innerRingRadius);

                // Small crosshair ticks outside the icon, not across it.
                p.setPen(QPen(pulseRing, qMax(1.3, radius * 0.14)));
                const double tickA = outerRingRadius + radius * 0.20;
                const double tickB = outerRingRadius + radius * 1.10;
                p.drawLine(QPointF(pt.x() - tickB, pt.y()), QPointF(pt.x() - tickA, pt.y()));
                p.drawLine(QPointF(pt.x() + tickA, pt.y()), QPointF(pt.x() + tickB, pt.y()));
                p.drawLine(QPointF(pt.x(), pt.y() - tickB), QPointF(pt.x(), pt.y() - tickA));
                p.drawLine(QPointF(pt.x(), pt.y() + tickA), QPointF(pt.x(), pt.y() + tickB));

                // Redraw the actual room circle and the room's existing emoji/icon on top.
                // This is the important part: the location pulse never replaces the icon.
                QRectF box(pt.x() - radius, pt.y() - radius, radius * 2, radius * 2);
                QColor currentRoomCore = m_highlightColor.lighter(130);
                currentRoomCore.setAlpha(235);
                p.setPen(QPen(currentRoomCore, qMax(1.4, radius * 0.18)));
                p.setBrush(colorForRoom(r));
                drawRoomBody(p, box);

                if (m_emojiEnabled && m_zoom >= 12.0) {
                    QFont hereFont = emojiFont;
                    hereFont.setPointSizeF(qBound(9.0, m_zoom * 0.50, 22.0));
                    drawRoomIcon(p, r, pt, radius * 1.06, hereFont);
                }

                if (m_zoom >= 13.0) {
                    const int mudVnum = m_data ? m_data->mudVnumForRoom(r) : 0;

                    QString roomName = r.name.trimmed();
                    roomName.remove(QRegularExpression(QStringLiteral("\\s*\\(#\\s*\\d+\\)\\s*")));
                    if (roomName.isEmpty()) roomName = QStringLiteral("Room");

                    QStringList ordered;
                    const QStringList order = { QStringLiteral("n"), QStringLiteral("e"), QStringLiteral("s"), QStringLiteral("w"), QStringLiteral("u"), QStringLiteral("d") };
                    const QMap<QString, int> exits = r.allExits();
                    QSet<QString> used;
                    for (const QString& d : order) {
                        if (exits.contains(d)) { ordered << d.toUpper(); used.insert(d); }
                    }
                    for (auto it = exits.cbegin(); it != exits.cend(); ++it) {
                        const QString d = it.key().trimmed();
                        if (d.isEmpty() || used.contains(d)) continue;
                        ordered << d.toUpper();
                    }
                    const QString exitText = ordered.isEmpty() ? QStringLiteral("none") : ordered.join(' ');

                    QFont hereLabel = p.font();
                    hereLabel.setBold(true);
                    hereLabel.setPointSize(qBound(8, int(m_zoom * 0.40), 13));
                    p.setFont(hereLabel);

                    const QString label = QStringLiteral("%1  #%2\nExits: %3")
                        .arg(roomName)
                        .arg(mudVnum > 0 ? mudVnum : r.id)
                        .arg(exitText);

                    const QRectF labelBox(pt.x() + outerRingRadius + 7, pt.y() - 22, 320, 48);
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(0, 0, 0, 185));
                    p.drawRoundedRect(labelBox, 6, 6);
                    p.setPen(m_pulseColor.lighter(140));
                    p.drawText(labelBox.adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignTop, label);
                }
            }
        }
    }

    void wheelEvent(QWheelEvent* e) override {
        const double oldZoom = m_zoom;
        m_zoom *= (e->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;
        m_zoom = qBound(5.0, m_zoom, 95.0);
        const QPointF cursor = e->position();
        m_pan = cursor - (cursor - m_pan) * (m_zoom / oldZoom);
        update();
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::RightButton) {
            const Room* hover = roomAt(e->pos());
            if (hover) showRoomContextMenu(hover->id, e->globalPosition().toPoint());
            return;
        }
        if (e->button() == Qt::LeftButton) {
            const Room* hover = roomAt(e->pos());
            if (hover) {
                m_selectedRoomId = hover->id;
                setCurrentRoom(hover->id, false);
                update();
                return;
            }
            m_dragging = true;
            m_lastMouse = e->pos();
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_dragging) {
            m_pan += e->pos() - m_lastMouse;
            m_lastMouse = e->pos();
            update();
            return;
        }
        const Room* hover = roomAt(e->pos());
        if (hover) {
            const QString exits = QStringList(hover->allExits().keys()).join(QStringLiteral(", "));
            setToolTip(QStringLiteral("%1 #%2\n%3\nExits: %4")
                       .arg(terrainEmojiFor(*hover)).arg(hover->id).arg(hover->name).arg(exits));
        } else {
            setToolTip(QString());
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) m_dragging = false;
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override {
        const Room* hover = roomAt(e->pos());
        if (hover) setCurrentRoom(hover->id, false);
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Delete && m_selectedRoomId > 0 && m_data && m_data->rooms.contains(m_selectedRoomId)) {
            const QMessageBox::StandardButton answer = QMessageBox::question(this, QStringLiteral("Delete room"), QStringLiteral("Delete selected room/icon and all connected exits?"));
            if (answer == QMessageBox::Yes) deleteRoomAndExits(m_selectedRoomId);
            return;
        }
        QWidget::keyPressEvent(e);
    }

private:
    MapData* m_data = nullptr;
    int m_currentRoomId = 0;
    int m_selectedRoomId = 0;
    int m_area = 6;
    int m_z = 0;
    double m_zoom = 18.0;
    QPointF m_pan = QPointF(240, 240);
    bool m_dragging = false;
    QPoint m_lastMouse;
    bool m_emojiEnabled = true;
    bool m_namesEnabled = false;
    bool m_gridEnabled = true;
    bool m_mappingEnabled = false;
    QString m_roomShape = QStringLiteral("square");
    QColor m_lineColor = QColor(0, 255, 0);
    QColor m_highlightColor = QColor(0, 255, 0);
    QColor m_pulseColor = QColor(0, 255, 0);
    QString m_customizationPath;
    QHash<int, QString> m_customEmoji;
    QHash<int, QColor> m_customColor;
    QHash<int, QString> m_customNote;
    mutable QHash<QString, QPixmap> m_iconPixmapCache;
    QSet<int> m_changedRooms;
    QSet<int> m_deletedRooms;


    QPointF screenPoint(const Room& r) const {
        return QPointF(r.x * m_zoom, -r.y * m_zoom) + m_pan;
    }

    void centerOn(const Room& r) {
        m_pan = QPointF(rect().center()) - QPointF(r.x * m_zoom, -r.y * m_zoom);
    }

    void drawGrid(QPainter& p) {
        QPen pen(QColor(10, 45, 24));
        pen.setWidth(1);
        p.setPen(pen);
        const double step = qMax(10.0, m_zoom);
        const double startX = std::fmod(m_pan.x(), step);
        const double startY = std::fmod(m_pan.y(), step);
        for (double x = startX; x < width(); x += step) p.drawLine(QPointF(x, 0), QPointF(x, height()));
        for (double y = startY; y < height(); y += step) p.drawLine(QPointF(0, y), QPointF(width(), y));
    }

    void drawRoomBody(QPainter& p, const QRectF& box) const {
        if (m_roomShape == QStringLiteral("circle")) p.drawEllipse(box);
        else p.drawRoundedRect(box, 2.5, 2.5);
    }

    const Room* roomAt(const QPoint& pos) const {
        if (!m_data) return nullptr;
        const double pick = qMax(8.0, m_zoom * 0.42);
        const double pick2 = pick * pick;
        const Room* best = nullptr;
        double bestDist = pick2;
        for (auto it = m_data->rooms.cbegin(); it != m_data->rooms.cend(); ++it) {
            const Room& r = it.value();
            if (r.area != m_area || r.z != m_z) continue;
            const QPointF pt = screenPoint(r);
            const double dx = pt.x() - pos.x();
            const double dy = pt.y() - pos.y();
            const double d2 = dx * dx + dy * dy;
            if (d2 <= bestDist) { bestDist = d2; best = &r; }
        }
        return best;
    }


    QString emojiForRoom(const Room& r) const {
        return m_customEmoji.value(r.id, terrainEmojiFor(r));
    }

    bool isCustomImageIcon(const QString& value) const {
        return value.startsWith(QStringLiteral("icon:"));
    }

    QString iconResourcePath(const QString& value) const {
        return QStringLiteral(":/resources/icons/") + value.mid(QStringLiteral("icon:").size());
    }

    void drawRoomIcon(QPainter& p, const Room& r, const QPointF& pt, double radius, const QFont& fallbackFont) const {
        const QString value = emojiForRoom(r);
        if (isCustomImageIcon(value)) {
            const QString resPath = iconResourcePath(value);
            if (!m_iconPixmapCache.contains(resPath)) {
                m_iconPixmapCache.insert(resPath, QPixmap(resPath));
            }
            const QPixmap pix = m_iconPixmapCache.value(resPath);
            if (!pix.isNull()) {
                const double side = radius * 2.25;
                p.drawPixmap(QRectF(pt.x() - side / 2.0, pt.y() - side / 2.0, side, side), pix, QRectF(pix.rect()));
                return;
            }
        }
        p.setFont(fallbackFont);
        p.drawText(QRectF(pt.x() - radius * 1.7, pt.y() - radius * 1.9, radius * 3.4, radius * 3.4), Qt::AlignCenter, value);
    }

    QColor colorForRoom(const Room& r) const {
        return m_customColor.value(r.id, terrainColorFor(r));
    }

    QVector<QPair<QString, QString>> emojiOptions() const {
        return {
            {QStringLiteral("Default auto terrain"), QString()},
            {QStringLiteral("Water / river 🌊"), QStringLiteral("🌊")},
            {QStringLiteral("Lake 🏞️"), QStringLiteral("🏞️")},
            {QStringLiteral("Trophy 🏆"), QStringLiteral("🏆")},
            {QStringLiteral("Food 🍖"), QStringLiteral("🍖")},
            {QStringLiteral("Bear 🐻"), QStringLiteral("🐻")},
            {QStringLiteral("Troll 🧌"), QStringLiteral("🧌")},
            {QStringLiteral("Orc 👹"), QStringLiteral("👹")},
            {QStringLiteral("Skull and crossbones ☠️"), QStringLiteral("☠️")},
            {QStringLiteral("Skull 💀"), QStringLiteral("💀")},
            {QStringLiteral("Danger ❗"), QStringLiteral("❗")},
            {QStringLiteral("Safe room ✅"), QStringLiteral("✅")},
            {QStringLiteral("Player / here 🟢"), QStringLiteral("🟢")},
            {QStringLiteral("Treasure 💎"), QStringLiteral("💎")},
            {QStringLiteral("Star / important ⭐"), QStringLiteral("⭐")},
            {QStringLiteral("Fire 🔥"), QStringLiteral("🔥")},
            {QStringLiteral("Forest 🌲"), QStringLiteral("🌲")},
            {QStringLiteral("Cave / tunnel 🕳️"), QStringLiteral("🕳️")},
            {QStringLiteral("Mountain ⛰️"), QStringLiteral("⛰️")},
            {QStringLiteral("Dirt path / road 🟫"), QStringLiteral("🟫")},
            {QStringLiteral("Road marker 🟨"), QStringLiteral("🟨")},
            {QStringLiteral("Stone floor 🪨"), QStringLiteral("🪨")},
            {QStringLiteral("Hills 🌄"), QStringLiteral("🌄")},
            {QStringLiteral("City / castle 🏰"), QStringLiteral("🏰")},
            {QStringLiteral("Door 🚪"), QStringLiteral("🚪")},
            {QStringLiteral("Bridge 🌉"), QStringLiteral("🌉")},
            {QStringLiteral("Shop 🛒"), QStringLiteral("🛒")},
            {QStringLiteral("Tavern 🍺"), QStringLiteral("🍺")},
            {QStringLiteral("Inn / rest 🛏️"), QStringLiteral("🛏️")},
            {QStringLiteral("Farm 🌾"), QStringLiteral("🌾")},
            {QStringLiteral("Wolf 🐺"), QStringLiteral("🐺")},
            {QStringLiteral("Spider 🕷️"), QStringLiteral("🕷️")},
            {QStringLiteral("Snake 🐍"), QStringLiteral("🐍")},
            {QStringLiteral("Dragon 🐉"), QStringLiteral("🐉")},
            {QStringLiteral("Guard 🛡️"), QStringLiteral("🛡️")},
            {QStringLiteral("Battle ⚔️"), QStringLiteral("⚔️")},
            {QStringLiteral("Weapon 🗡️"), QStringLiteral("🗡️")},
            {QStringLiteral("Mage 🧙"), QStringLiteral("🧙")},
            {QStringLiteral("Boat ⛵"), QStringLiteral("⛵")},
            {QStringLiteral("Cemetery ⚰️"), QStringLiteral("⚰️")},
            {QStringLiteral("Custom art: Angry orc warrior"), QStringLiteral("icon:angry_orc_warrior_avatar_icon.png")},
            {QStringLiteral("Custom art: Menacing orc warrior"), QStringLiteral("icon:menacing_orc_warrior_emblem.png")},
            {QStringLiteral("Custom art: Goblin rogue"), QStringLiteral("icon:old_goblin_rogue_portrait.png")},
            {QStringLiteral("Custom art: Stone troll / ogre"), QStringLiteral("icon:menacing_stone_skinned_ogre_icon.png")},
            {QStringLiteral("Custom art: Wolf beast"), QStringLiteral("icon:fierce_wolf_emblem_with_glowing_eyes.png")},
            {QStringLiteral("Custom art: Elf"), QStringLiteral("icon:regal_elf_with_ornate_tiara.png")},
            {QStringLiteral("Custom art: Dwarf"), QStringLiteral("icon:dwarf_warrior_portrait_with_braided_beard.png")},
            {QStringLiteral("Custom art: Skull and crossed swords"), QStringLiteral("icon:skull_and_crossed_swords_emblem.png")},
            {QStringLiteral("Custom art: Butcher"), QStringLiteral("icon:butcher_s_pride_emblem.png")},
            {QStringLiteral("Custom art: Armor shop"), QStringLiteral("icon:medieval_armor_emblem_with_shield.png")},
            {QStringLiteral("Custom art: Blacksmith"), QStringLiteral("icon:blacksmith_forge_emblem_icon.png")},
            {QStringLiteral("Custom art: General store"), QStringLiteral("icon:merchant_s_crate_and_supplies_emblem.png")},
            {QStringLiteral("Custom art: City gate"), QStringLiteral("icon:medieval_fortress_gate_emblem.png")},
            {QStringLiteral("Custom art: Trophy"), QStringLiteral("icon:golden_trophy_with_laurel_wreath.png")},
            {QStringLiteral("Custom art: Skeleton / undead"), QStringLiteral("icon:undead_warrior_emblem_with_battered_helm.png")},
            {QStringLiteral("Custom art: Bandit / rogue"), QStringLiteral("icon:rogue_assassin_emblem_design.png")},
            {QStringLiteral("Custom art: Tavern"), QStringLiteral("icon:medieval_tavern_beer_mug_emblem.png")},
            {QStringLiteral("Custom art: Herbal alchemy"), QStringLiteral("icon:herbal_alchemy_emblem_with_potion.png")},
            {QStringLiteral("Question ❓"), QStringLiteral("❓")}
        };
    }

    QString chooseEmoji(const QString& currentEmoji, bool* okOut = nullptr) const {
        QStringList labels;
        QStringList values;
        int currentIndex = 0;
        const auto options = emojiOptions();
        for (int i = 0; i < options.size(); ++i) {
            labels << options[i].first;
            values << options[i].second;
            if (options[i].second == currentEmoji) currentIndex = i;
        }
        bool ok = false;
        const QString chosen = QInputDialog::getItem(const_cast<MapWidget*>(this), QStringLiteral("Change emoji icon"),
                                                     QStringLiteral("Choose this room's icon:"), labels, currentIndex, false, &ok);
        if (okOut) *okOut = ok;
        if (!ok) return currentEmoji;
        const int idx = labels.indexOf(chosen);
        return idx >= 0 ? values.value(idx) : currentEmoji;
    }

    void showRoomContextMenu(int roomId, const QPoint& globalPos) {
        if (!m_data || !m_data->rooms.contains(roomId)) return;
        QMenu menu(this);
        QAction* moveAct = menu.addAction(QStringLiteral("Move"));
        QAction* configureAct = menu.addAction(QStringLiteral("Configure room..."));
        QAction* setExitsAct = menu.addAction(QStringLiteral("Set exits..."));
        QAction* createExitLineAct = menu.addAction(QStringLiteral("Create exit line..."));
        QAction* deleteAct = menu.addAction(QStringLiteral("Delete"));
        menu.addSeparator();
        QAction* movePosAct = menu.addAction(QStringLiteral("Move to position..."));
        QAction* moveAreaAct = menu.addAction(QStringLiteral("Move to area..."));
        QAction* labelAct = menu.addAction(QStringLiteral("Create label..."));
        QAction* exportAct = menu.addAction(QStringLiteral("Export area to image..."));
        QAction* playerAct = menu.addAction(QStringLiteral("Set player location"));
        menu.addSeparator();
        QAction* emojiAct = menu.addAction(QStringLiteral("Change emoji icon..."));
        QAction* colorAct = menu.addAction(QStringLiteral("Change color..."));
        QAction* clearCustomAct = menu.addAction(QStringLiteral("Clear custom color/icon"));
        QAction* saveMapAct = menu.addAction(QStringLiteral("Save map"));
        menu.addSeparator();
        QAction* viewAct = menu.addAction(QStringLiteral("Switch to viewing mode"));

        QAction* picked = menu.exec(globalPos);
        if (!picked) return;
        if (picked == moveAct || picked == playerAct) {
            setCurrentRoom(roomId, true);
            return;
        }
        if (picked == configureAct) { configureRoom(roomId); return; }
        if (picked == emojiAct) { changeEmoji(roomId); return; }
        if (picked == colorAct) { changeColor(roomId); return; }
        if (picked == clearCustomAct) { m_customEmoji.remove(roomId); m_customColor.remove(roomId); update(); return; }
        if (picked == saveMapAct) { saveMapWithPopup(); return; }
        if (picked == movePosAct) { moveRoomToPosition(roomId); return; }
        if (picked == moveAreaAct) { moveRoomToArea(roomId); return; }
        if (picked == labelAct) { createLabel(roomId); return; }
        if (picked == exportAct) { exportAreaImage(); return; }
        if (picked == setExitsAct || picked == createExitLineAct) { showExitEditorPlaceholder(roomId); return; }
        if (picked == deleteAct) {
            const QMessageBox::StandardButton answer = QMessageBox::question(this, QStringLiteral("Delete room"), QStringLiteral("Delete this room/icon and remove all exits connected to it? You can save the map after this."));
            if (answer == QMessageBox::Yes) deleteRoomAndExits(roomId);
            return;
        }
        if (picked == viewAct) { setCursor(Qt::ArrowCursor); return; }
    }

    void changeEmoji(int roomId) {
        if (!m_data || !m_data->rooms.contains(roomId)) return;
        bool ok = false;
        const QString current = m_customEmoji.value(roomId);
        const QString emoji = chooseEmoji(current, &ok);
        if (!ok) return;
        if (emoji.isEmpty()) m_customEmoji.remove(roomId);
        else m_customEmoji.insert(roomId, emoji);
        update();
    }

    void changeColor(int roomId) {
        if (!m_data || !m_data->rooms.contains(roomId)) return;
        const QColor start = m_customColor.value(roomId, terrainColorFor(m_data->rooms[roomId]));
        const QColor chosen = QColorDialog::getColor(start, this, QStringLiteral("Change room color"));
        if (!chosen.isValid()) return;
        m_customColor.insert(roomId, chosen);
        update();
    }

    void configureRoom(int roomId) {
        if (!m_data || !m_data->rooms.contains(roomId)) return;
        Room& r = m_data->rooms[roomId];
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Configure room"));
        QVBoxLayout* outer = new QVBoxLayout(&dialog);
        QFormLayout* form = new QFormLayout;

        QLineEdit* nameEdit = new QLineEdit(r.name);
        QLineEdit* terrainEdit = new QLineEdit(r.terrain);
        QSpinBox* areaSpin = new QSpinBox;
        areaSpin->setRange(-999999, 999999);
        areaSpin->setValue(r.area);
        QSpinBox* xSpin = new QSpinBox;
        xSpin->setRange(-999999, 999999);
        xSpin->setValue(r.x);
        QSpinBox* ySpin = new QSpinBox;
        ySpin->setRange(-999999, 999999);
        ySpin->setValue(r.y);
        QSpinBox* zSpin = new QSpinBox;
        zSpin->setRange(-999999, 999999);
        zSpin->setValue(r.z);
        QPlainTextEdit* noteEdit = new QPlainTextEdit(m_customNote.value(roomId));
        noteEdit->setMaximumHeight(80);

        QComboBox* emojiCombo = new QComboBox;
        const auto options = emojiOptions();
        int emojiIndex = 0;
        const QString currentEmoji = m_customEmoji.value(roomId);
        for (int i = 0; i < options.size(); ++i) {
            emojiCombo->addItem(options[i].first, options[i].second);
            if (options[i].second == currentEmoji) emojiIndex = i;
        }
        emojiCombo->setCurrentIndex(emojiIndex);

        QPushButton* colorButton = new QPushButton(m_customColor.contains(roomId) ? m_customColor.value(roomId).name() : QStringLiteral("Auto terrain color"));
        QColor selectedColor = m_customColor.value(roomId);
        connect(colorButton, &QPushButton::clicked, &dialog, [&]() {
            const QColor start = selectedColor.isValid() ? selectedColor : terrainColorFor(r);
            const QColor chosen = QColorDialog::getColor(start, &dialog, QStringLiteral("Change room color"));
            if (!chosen.isValid()) return;
            selectedColor = chosen;
            colorButton->setText(chosen.name(QColor::HexRgb));
        });

        form->addRow(QStringLiteral("Name:"), nameEdit);
        form->addRow(QStringLiteral("Terrain:"), terrainEdit);
        form->addRow(QStringLiteral("Area:"), areaSpin);
        form->addRow(QStringLiteral("X:"), xSpin);
        form->addRow(QStringLiteral("Y:"), ySpin);
        form->addRow(QStringLiteral("Z:"), zSpin);
        form->addRow(QStringLiteral("Emoji icon:"), emojiCombo);
        form->addRow(QStringLiteral("Color:"), colorButton);
        form->addRow(QStringLiteral("Notes/details:"), noteEdit);
        outer->addLayout(form);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        outer->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted) return;
        r.name = nameEdit->text().trimmed().isEmpty() ? r.name : nameEdit->text().trimmed();
        r.terrain = terrainEdit->text().trimmed();
        r.area = areaSpin->value();
        r.x = xSpin->value();
        r.y = ySpin->value();
        r.z = zSpin->value();
        m_changedRooms.insert(roomId);

        const QString emoji = emojiCombo->currentData().toString();
        if (emoji.isEmpty()) m_customEmoji.remove(roomId);
        else m_customEmoji.insert(roomId, emoji);
        if (selectedColor.isValid()) m_customColor.insert(roomId, selectedColor);
        const QString note = noteEdit->toPlainText().trimmed();
        if (note.isEmpty()) m_customNote.remove(roomId);
        else m_customNote.insert(roomId, note);
        update();
    }

    void moveRoomToPosition(int roomId) {
        if (!m_data || !m_data->rooms.contains(roomId)) return;
        Room& r = m_data->rooms[roomId];
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Move to position"));
        QFormLayout* form = new QFormLayout(&dialog);
        QSpinBox* xSpin = new QSpinBox;
        QSpinBox* ySpin = new QSpinBox;
        QSpinBox* zSpin = new QSpinBox;
        for (QSpinBox* s : {xSpin, ySpin, zSpin}) s->setRange(-999999, 999999);
        xSpin->setValue(r.x); ySpin->setValue(r.y); zSpin->setValue(r.z);
        form->addRow(QStringLiteral("X:"), xSpin);
        form->addRow(QStringLiteral("Y:"), ySpin);
        form->addRow(QStringLiteral("Z:"), zSpin);
        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        r.x = xSpin->value(); r.y = ySpin->value(); r.z = zSpin->value();
        m_changedRooms.insert(roomId);
        update();
    }

    void moveRoomToArea(int roomId) {
        if (!m_data || !m_data->rooms.contains(roomId)) return;
        Room& r = m_data->rooms[roomId];
        bool ok = false;
        const int area = QInputDialog::getInt(this, QStringLiteral("Move to area"), QStringLiteral("Area id:"), r.area, -999999, 999999, 1, &ok);
        if (!ok) return;
        r.area = area;
        m_area = area;
        m_changedRooms.insert(roomId);
        update();
    }

    void createLabel(int roomId) {
        if (!m_data || !m_data->rooms.contains(roomId)) return;
        bool ok = false;
        const QString note = QInputDialog::getMultiLineText(this, QStringLiteral("Create label / details"),
                                                            QStringLiteral("Room label/details:"), m_customNote.value(roomId), &ok).trimmed();
        if (!ok) return;
        if (note.isEmpty()) m_customNote.remove(roomId);
        else m_customNote.insert(roomId, note);
        update();
    }

    void showExitEditorPlaceholder(int roomId) {
        if (!m_data || !m_data->rooms.contains(roomId)) return;
        const Room& r = m_data->rooms[roomId];
        QStringList exits;
        const QMap<QString, int> roomExits = r.allExits();
        for (auto it = roomExits.cbegin(); it != roomExits.cend(); ++it)
            exits << QStringLiteral("%1 -> %2").arg(it.key()).arg(it.value());
        QMessageBox::information(this, QStringLiteral("Set exits"),
                                 QStringLiteral("Existing exits for this room:\n\n%1\n\nExit editing is listed here like Mudlet, but this starter build keeps the original ARDABEST exits locked so pathing does not break yet.")
                                 .arg(exits.isEmpty() ? QStringLiteral("No exits") : exits.join('\n')));
    }

    void exportAreaImage() {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export area to image"), QStringLiteral("ardabest-area.png"), QStringLiteral("PNG image (*.png)"));
        if (path.isEmpty()) return;
        QPixmap pix(size());
        render(&pix);
        if (!pix.save(path, "PNG")) {
            QMessageBox::warning(this, QStringLiteral("Export failed"), QStringLiteral("Could not save the image."));
        }
    }

    void saveMapWithPopup() {
        QString error;
        if (saveMapCustomizations(QString(), &error))
            QMessageBox::information(this, QStringLiteral("Map saved"), QStringLiteral("Saved map customizations to:\n%1").arg(m_customizationPath));
        else
            QMessageBox::warning(this, QStringLiteral("Map save failed"), error);
    }

};

static QString defaultAutomapperPattern() {
    // Very flexible RotS room detector.
    // It does not care how the room name looks. It only needs a real MUD room number (#12345)
    // and an "Exits are:" section. A final Mudlet mapper id like (13017) is ignored.
    return QStringLiteral("^\\s*(?:.*?>\\s*)?(.*?)\\s*\\(#\\s*(\\d+)\\)\\s*(\\[[^\\]]+\\])?\\s*Exits?\\s*(?:are)?\\s*:\\s*(.*?)\\s*(?:\\((?!#)\\d+\\))?\\s*$");
}

static QString defaultAutomapperScript() {
    return QString::fromUtf8(R"LUA(-- RotS Automapper - built into ArdaBest Client
-- Detects room lines like:
-- Dark Tunnel Passage (#31902) [ Floor ] Exits are: E S W
-- [HP: 316/316]>A Stone Staircase (#32681) [ Floor ] Exits are: S
-- Dark Tunnel Passage (#31902) [ Floor ] Exits are: E S W  (13017)
--
-- It does NOT care what the room name looks like.
-- It uses the real MUD room number inside (#31902).
-- It ignores a final Mudlet mapper add-on id like (13017).
--
-- In this client the Lua below is shown so it feels like Mudlet.
-- The actual automapper action is compiled in C++ so it is fast and always works.

map = map or {}
map.prompt = map.prompt or {}

local roomName = string.trim(matches[2] or "")
local roomNum  = string.trim(matches[3] or "")
local terrain  = string.trim(matches[4] or "")
local exits    = string.trim(matches[5] or "")

-- Safety cleanup: remove final mapper id if it ever appears.
exits = exits:gsub("%s*%(%d+%)%s*$", "")
exits = string.trim(exits)

local fullRoomName = roomName .. " (" .. roomNum .. ")"
if terrain ~= "" then
  fullRoomName = fullRoomName .. " " .. terrain
end

map.prompt.room = fullRoomName
map.prompt.roomName = roomName
map.prompt.roomNum = roomNum
map.prompt.terrain = terrain
map.prompt.exits = exits

raiseEvent("onNewRoom", exits)
)LUA");
}

static TriggerRule makeDefaultAutomapperTrigger() {
    TriggerRule t;
    t.name = QStringLiteral("RotS Automapper");
    t.pattern = defaultAutomapperPattern();
    t.command = QString();
    t.script = defaultAutomapperScript();
    t.builtin = QStringLiteral("automapper");
    t.enabled = true;
    return t;
}

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(QStringLiteral("ArdaBest MUD Client 🧭 Emoji Mapper"));
        resize(1380, 820);
        setStyleSheet(QStringLiteral(
            "QMainWindow, QWidget { background:#efefef; }"
            "QTabWidget::pane { border:1px solid #b8b8b8; background:#efefef; }"
            "QTabBar::tab { padding:6px 10px; }"
            "QToolButton, QPushButton { padding:4px 8px; }"
            "QTableWidget { background:#ffffff; gridline-color:#b7b7b7; }"
            "QListWidget { background:#ffffff; }"
            "QLineEdit { background:#ffffff; }"
        ));

        QString error;
        if (!m_map.load(QStringLiteral(":/resources/ardabest.json"), &error)) {
            QMessageBox::critical(this, QStringLiteral("Map load failed"), error);
        }

        buildUi();
        if (m_mapWidget) {
            m_mapWidget->setCustomizationPath(mapCustomizationPath());
            m_mapWidget->loadMapCustomizations(mapCustomizationPath());
        }
        wireUi();
        loadProfile();

        if (m_map.rooms.contains(12970)) setCurrentRoom(12970, true);
        else if (!m_map.rooms.isEmpty()) setCurrentRoom(m_map.rooms.cbegin().key(), true);

        connect(&m_socket, &QTcpSocket::connected, this, [this] {
            m_connect->setText(QStringLiteral("Disconnect"));
            appendSystem(QStringLiteral("✅ Connected."));
        });
        connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
            m_connect->setText(QStringLiteral("Connect"));
            appendSystem(QStringLiteral("🔌 Disconnected."));
        });
        connect(&m_socket, &QTcpSocket::readyRead, this, [this] { readNetwork(); });
        connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            appendSystem(QStringLiteral("⚠️ Socket error: %1").arg(m_socket.errorString()));
        });
        connect(&m_walkTimer, &QTimer::timeout, this, [this] { sendNextWalkStep(); });
        m_walkTimer.setInterval(300);

        appendSystem(QStringLiteral("🗺️ Loaded ARDABEST: %1 rooms from %2. Mudlet map version %3.")
                     .arg(m_map.rooms.size()).arg(m_map.source).arg(m_map.version));
        appendSystem(QStringLiteral("💬 Local commands: /connect host port, /find text, /room id, /goto id, /font size, /emoji on, start mapping, stop mapping, save map, update bar, set background, clear background, /alias add name command, /trigger add pattern command, /help"));
        appendSystem(QStringLiteral("🎯 Default RotS Automapper trigger is loaded and ON. It detects any room name with (#room) and Exits are:."));
        appendSystem(QStringLiteral("🟢 Right-click a room icon on the automapper to configure details, change emoji/color, set player location, or save map. Type save map to save map changes."));
        appendSystem(QStringLiteral("🖼️ Background button opens a file picker for .png/.jpg images. Clear background returns the terminal to black."));
        appendSystem(QStringLiteral("🪟 Map can be resized by dragging the bright green divider, or use Map − / Map +. Detach map opens a separate resizable map window."));
    }

private:
    MapData m_map;
    MapWidget* m_mapWidget = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QPushButton* m_detachMapButton = nullptr;
    QPushButton* m_mapSmallerButton = nullptr;
    QPushButton* m_mapBiggerButton = nullptr;
    QLabel* m_mapRoomLineLabel = nullptr;
    QLabel* m_detachedRoomLineLabel = nullptr;
    QDialog* m_detachedMapWindow = nullptr;
    MapWidget* m_detachedMapWidget = nullptr;
    TerminalTextEdit* m_terminal = nullptr;
    QLineEdit* m_input = nullptr;
    QLineEdit* m_host = nullptr;
    QLineEdit* m_port = nullptr;
    QPushButton* m_connect = nullptr;
    QComboBox* m_areaCombo = nullptr;
    QSpinBox* m_zSpin = nullptr;
    QCheckBox* m_emojiCheck = nullptr;
    QCheckBox* m_namesCheck = nullptr;
    QCheckBox* m_gridCheck = nullptr;
    QComboBox* m_shapeCombo = nullptr;
    QComboBox* m_highlightColorCombo = nullptr;
    QComboBox* m_lineColorCombo = nullptr;
    QComboBox* m_pulseColorCombo = nullptr;
    QCheckBox* m_hpBarCheck = nullptr;
    QCheckBox* m_manaBarCheck = nullptr;
    QCheckBox* m_moveBarCheck = nullptr;
    QCheckBox* m_xpBarCheck = nullptr;
    QProgressBar* m_hpBar = nullptr;
    QProgressBar* m_manaBar = nullptr;
    QProgressBar* m_moveBar = nullptr;
    QProgressBar* m_xpBar = nullptr;
    QPushButton* m_backgroundButton = nullptr;
    QPushButton* m_clearBackgroundButton = nullptr;
    QComboBox* m_textColorCombo = nullptr;
    QSpinBox* m_fontSizeSpin = nullptr;
    QColor m_defaultTextColor = QColor(235, 235, 235);
    QString m_defaultTextColorName = QStringLiteral("White");
    int m_terminalFontSize = 10;
    QString m_terminalBackgroundPath;
    QPushButton* m_startMappingButton = nullptr;
    QPushButton* m_stopMappingButton = nullptr;
    QTableWidget* m_aliasTable = nullptr;
    QListWidget* m_aliasList = nullptr;
    QLineEdit* m_aliasNameEdit = nullptr;
    QLineEdit* m_aliasPatternEdit = nullptr;
    QLineEdit* m_aliasCommandEdit = nullptr;
    QListWidget* m_scriptList = nullptr;
    QLineEdit* m_scriptNameEdit = nullptr;
    QLineEdit* m_scriptRegisteredEventsEdit = nullptr;
    QLineEdit* m_scriptUserEventEdit = nullptr;
    QPlainTextEdit* m_scriptEditor = nullptr;
    QListWidget* m_triggerList = nullptr;
    QLineEdit* m_triggerNameEdit = nullptr;
    QTableWidget* m_triggerTable = nullptr;
    QLineEdit* m_triggerCommandEdit = nullptr;
    QPlainTextEdit* m_triggerScriptEditor = nullptr;
    QListWidget* m_roomList = nullptr;
    QLineEdit* m_searchBox = nullptr;
    QLabel* m_roomDetails = nullptr;
    QTcpSocket m_socket;
    QByteArray m_plainBuffer;
    QTextCharFormat m_currentFormat;
    QQueue<QPair<QString, int>> m_walkQueue;
    QTimer m_walkTimer;
    QMap<QString, QString> m_aliases;
    QVector<ScriptRule> m_scripts;
    QVector<TriggerRule> m_triggers;
    QStringList m_history;
    int m_historyIndex = 0;
    bool m_emojiOutput = true;
    QString m_pendingMappingDirection;
    qint64 m_lastAutoInfoAt = 0;
    int m_xpNeedStart = 0;
    int m_xpNeedCurrent = 0;
    int m_pendingXpGainBeforeInfo = 0;


    static QColor namedBrightColor(const QString& name) {
        const QString n = name.toLower();
        if (n == QStringLiteral("bright blue")) return QColor(0, 120, 255);
        if (n == QStringLiteral("bright red")) return QColor(255, 35, 35);
        if (n == QStringLiteral("bright yellow")) return QColor(255, 245, 0);
        if (n == QStringLiteral("bright pink")) return QColor(255, 50, 210);
        if (n == QStringLiteral("bright purple")) return QColor(170, 70, 255);
        if (n == QStringLiteral("bright green")) return QColor(0, 255, 70);
        if (n == QStringLiteral("green")) return QColor(0, 190, 60);
        if (n == QStringLiteral("light blue")) return QColor(70, 220, 255);
        if (n == QStringLiteral("orange")) return QColor(255, 150, 0);
        if (n == QStringLiteral("white")) return QColor(255, 255, 255);
        return QColor(0, 255, 70);
    }

    void fillBrightColorCombo(QComboBox* combo, const QString& current = QStringLiteral("Bright Green")) {
        const QStringList names = { QStringLiteral("Bright Green"), QStringLiteral("Bright Blue"), QStringLiteral("Bright Red"), QStringLiteral("Bright Yellow"), QStringLiteral("Bright Pink"), QStringLiteral("Bright Purple"), QStringLiteral("Green"), QStringLiteral("Light Blue"), QStringLiteral("Orange"), QStringLiteral("White") };
        for (const QString& n : names) combo->addItem(n, n);
        const int idx = combo->findText(current, Qt::MatchFixedString);
        if (idx >= 0) combo->setCurrentIndex(idx);
    }



    QColor namedTextColor(const QString& name) const {
        const QString n = name.toLower();
        if (n == QStringLiteral("white")) return QColor(235, 235, 235);
        if (n == QStringLiteral("green")) return QColor(0, 190, 60);
        if (n == QStringLiteral("bright green")) return QColor(0, 255, 70);
        if (n == QStringLiteral("light green")) return QColor(135, 255, 150);
        if (n == QStringLiteral("bright blue")) return QColor(70, 150, 255);
        if (n == QStringLiteral("light blue")) return QColor(120, 230, 255);
        if (n == QStringLiteral("bright red")) return QColor(255, 70, 70);
        if (n == QStringLiteral("bright yellow")) return QColor(255, 245, 80);
        if (n == QStringLiteral("bright pink")) return QColor(255, 80, 220);
        if (n == QStringLiteral("bright purple")) return QColor(185, 95, 255);
        if (n == QStringLiteral("orange")) return QColor(255, 170, 40);
        if (n == QStringLiteral("gray")) return QColor(180, 180, 180);
        return QColor(235, 235, 235);
    }

    void fillTextColorCombo(QComboBox* combo, const QString& current = QStringLiteral("White")) {
        const QStringList names = {
            QStringLiteral("White"), QStringLiteral("Green"), QStringLiteral("Bright Green"),
            QStringLiteral("Light Green"), QStringLiteral("Bright Blue"), QStringLiteral("Light Blue"),
            QStringLiteral("Bright Red"), QStringLiteral("Bright Yellow"), QStringLiteral("Bright Pink"),
            QStringLiteral("Bright Purple"), QStringLiteral("Orange"), QStringLiteral("Gray")
        };
        for (const QString& n : names) combo->addItem(n, n);
        const int idx = combo->findText(current, Qt::MatchFixedString);
        if (idx >= 0) combo->setCurrentIndex(idx);
    }

    void applyTerminalTextColor(const QString& name, bool saveNow = false) {
        m_defaultTextColorName = name;
        m_defaultTextColor = namedTextColor(name);
        if (m_terminal) {
            QPalette pal = m_terminal->palette();
            pal.setColor(QPalette::Text, m_defaultTextColor);
            m_terminal->setPalette(pal);
            m_terminal->setTextColor(m_defaultTextColor);
            m_terminal->setStyleSheet(QStringLiteral("QTextEdit { background: rgba(0,0,0,0); color:%1; border:1px solid #303030; selection-background-color:#145a20; }").arg(m_defaultTextColor.name()));
        }
        if (saveNow) saveProfileToDisk();
    }

    void applyTerminalFontSize(int size, bool saveNow = false) {
        m_terminalFontSize = qBound(8, size, 28);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setFamilies({QStringLiteral("Courier New"), QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Monospace")});
        mono.setPointSize(m_terminalFontSize);
        if (m_terminal) m_terminal->setFont(mono);
        if (m_input) m_input->setFont(mono);
        if (m_host) m_host->setFont(mono);
        if (m_port) m_port->setFont(mono);

        if (m_mapRoomLineLabel) m_mapRoomLineLabel->setFont(mono);
        if (m_detachedRoomLineLabel) m_detachedRoomLineLabel->setFont(mono);

        if (m_fontSizeSpin && m_fontSizeSpin->value() != m_terminalFontSize) {
            m_fontSizeSpin->blockSignals(true);
            m_fontSizeSpin->setValue(m_terminalFontSize);
            m_fontSizeSpin->blockSignals(false);
        }
        if (saveNow) saveProfileToDisk();
    }


    void applyTerminalBackground(const QString& path, bool saveNow = false) {
        if (!m_terminal) return;
        if (path.trimmed().isEmpty()) {
            m_terminalBackgroundPath.clear();
            m_terminal->clearStaticBackgroundImage();
            appendSystem(QStringLiteral("🖼️ Terminal background cleared."));
            if (saveNow) saveProfileToDisk();
            return;
        }
        QFileInfo info(path);
        if (!info.exists()) {
            appendSystem(QStringLiteral("⚠️ Background image not found: %1").arg(path));
            return;
        }
        QPixmap pix(path);
        if (pix.isNull()) {
            appendSystem(QStringLiteral("⚠️ Could not load background image: %1").arg(path));
            return;
        }
        m_terminalBackgroundPath = path;
        m_terminal->setStaticBackgroundImage(path);
        appendSystem(QStringLiteral("🖼️ Static terminal background set: %1").arg(info.fileName()));
        if (saveNow) saveProfileToDisk();
    }

    void chooseTerminalBackground() {
        const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        const QString startDir = m_terminalBackgroundPath.isEmpty()
            ? (pictures.isEmpty() ? QDir::homePath() : pictures)
            : QFileInfo(m_terminalBackgroundPath).absolutePath();

        QFileDialog dlg(this, QStringLiteral("Choose static terminal background image"), startDir);
        dlg.setFileMode(QFileDialog::ExistingFile);
        dlg.setAcceptMode(QFileDialog::AcceptOpen);
        dlg.setNameFilters({
            QStringLiteral("Image files (*.png *.jpg *.jpeg *.bmp *.webp)"),
            QStringLiteral("PNG (*.png)"),
            QStringLiteral("JPEG (*.jpg *.jpeg)"),
            QStringLiteral("All files (*.*)")
        });
        dlg.setOption(QFileDialog::DontUseNativeDialog, false);
        if (dlg.exec() != QDialog::Accepted) return;
        const QStringList selected = dlg.selectedFiles();
        if (selected.isEmpty()) return;
        applyTerminalBackground(selected.first(), true);
    }

    void clearTerminalBackground() {
        applyTerminalBackground(QString(), true);
    }

    void updateXpNeed(int need, bool fromInfoLine) {
        if (!m_xpBar || need < 0) return;

        // The MUD tells us: "need X more to advance".
        // If we received XP before the first info line, treat:
        //   start needed = current need + pending gained XP
        // so the bar immediately shows the XP you just earned.
        if (fromInfoLine && m_xpNeedStart <= 0 && m_pendingXpGainBeforeInfo > 0) {
            m_xpNeedStart = qMax(1, need + m_pendingXpGainBeforeInfo);
        } else if (fromInfoLine || m_xpNeedStart <= 0 || need > m_xpNeedCurrent) {
            if (m_xpNeedStart <= 0 || need > m_xpNeedStart || need > m_xpNeedCurrent) {
                m_xpNeedStart = qMax(1, need);
            }
        }

        m_xpNeedCurrent = qMax(0, need);
        m_pendingXpGainBeforeInfo = 0;

        const int maxNeed = qMax(1, m_xpNeedStart);
        const int gainedTowardLevel = qBound(0, maxNeed - m_xpNeedCurrent, maxNeed);
        const int pct = int((double(gainedTowardLevel) / double(maxNeed)) * 100.0 + 0.5);

        m_xpBar->setRange(0, maxNeed);
        m_xpBar->setValue(gainedTowardLevel);
        m_xpBar->setFormat(QStringLiteral("XP %1% | %2 / %3 | need %4")
            .arg(pct)
            .arg(gainedTowardLevel)
            .arg(maxNeed)
            .arg(m_xpNeedCurrent));
    }

    void applyXpGain(int gained, const QString& sourceText = QString()) {
        if (!m_xpBar || gained <= 0) return;

        if (m_xpNeedCurrent > 0) {
            updateXpNeed(qMax(0, m_xpNeedCurrent - gained), false);
            return;
        }

        // We do not know the current "need to advance" yet.
        // Store the gain, show visible movement, and request info once.
        m_pendingXpGainBeforeInfo += gained;
        m_xpBar->setRange(0, qMax(1, m_pendingXpGainBeforeInfo));
        m_xpBar->setValue(m_pendingXpGainBeforeInfo);
        m_xpBar->setFormat(QStringLiteral("XP +%1 seen — waiting for info").arg(m_pendingXpGainBeforeInfo));

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastAutoInfoAt > 2500) {
            m_lastAutoInfoAt = now;
            QTimer::singleShot(250, this, [this](){ sendCommand(QStringLiteral("info")); });
        }
        if (!sourceText.isEmpty()) {
            appendSystem(QStringLiteral("📈 XP gain detected: %1. Requesting info to sync XP bar.").arg(sourceText));
        }
    }

    void updateBarsFromPromptLine(const QString& line) {
        static const QRegularExpression promptRe(QStringLiteral("HP:\\s*(\\d+)\\/(\\d+)\\s+S:\\s*(\\d+)\\/(\\d+)\\s+MV:\\s*(\\d+)\\/(\\d+)"));
        const QRegularExpressionMatch m = promptRe.match(line);
        auto setBar = [](QProgressBar* bar, int cur, int max, const QString& label) {
            if (!bar || max <= 0) return;
            bar->setRange(0, max);
            bar->setValue(qBound(0, cur, max));
            const int pct = int((double(cur) / double(max)) * 100.0 + 0.5);
            bar->setFormat(QStringLiteral("%1 %2% | %3/%4").arg(label).arg(pct).arg(cur).arg(max));
        };
        if (m.hasMatch()) {
            setBar(m_hpBar, m.captured(1).toInt(), m.captured(2).toInt(), QStringLiteral("HP"));
            setBar(m_manaBar, m.captured(3).toInt(), m.captured(4).toInt(), QStringLiteral("Mana"));
            setBar(m_moveBar, m.captured(5).toInt(), m.captured(6).toInt(), QStringLiteral("Move"));
        }
        static const QRegularExpression needRe(QStringLiteral("need\\s+([0-9,]+)\\s+(?:more\\s+)?to\\s+advance"), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch xp = needRe.match(line);
        if (xp.hasMatch()) {
            QString n = xp.captured(1); n.remove(',');
            updateXpNeed(n.toInt(), true);
        }

        static const QRegularExpression scoredRe(QStringLiteral("You\\s+have\\s+scored\\s+[0-9,]+\\s+experience\\s+points,\\s+and\\s+need\\s+([0-9,]+)\\s+more\\s+to\\s+advance\\.?"), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch scored = scoredRe.match(line);
        if (scored.hasMatch()) {
            QString n = scored.captured(1); n.remove(',');
            updateXpNeed(n.toInt(), true);
        }

        // XP gain formats seen on RotS/MUME-like output:
        //   You gain 175 experience.
        //   You receive 175 experience points.
        //   You receive your share of experience -- 175 points.
        static const QRegularExpression gainRe(QStringLiteral("(?:gain|gained|receive|received|earn|earned|get|got|obtain|obtained)\\s+([0-9,]+)\\s+(?:experience|xp)(?:\\s+points?)?"), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch gain = gainRe.match(line);
        if (gain.hasMatch()) {
            QString n = gain.captured(1); n.remove(',');
            applyXpGain(n.toInt(), QStringLiteral("+%1").arg(n));
        }

        static const QRegularExpression shareGainRe(QStringLiteral("receive\\s+your\\s+share\\s+of\\s+experience\\s*[-–—]+\\s*([0-9,]+)\\s+points?"), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch shareGain = shareGainRe.match(line);
        if (shareGain.hasMatch()) {
            QString n = shareGain.captured(1); n.remove(',');
            applyXpGain(n.toInt(), QStringLiteral("share +%1").arg(n));
        }

        static const QRegularExpression levelUpRe(QStringLiteral("feel\\s+more\\s+powerful|feel\\s+more\\s+experienced|have\\s+gained\\s+a\\s+level|have\\s+advanced\\s+a\\s+level"), QRegularExpression::CaseInsensitiveOption);
        if (levelUpRe.match(line).hasMatch()) {
            m_xpNeedStart = 0;
            m_xpNeedCurrent = 0;
            m_pendingXpGainBeforeInfo = 0;
            if (m_xpBar) {
                m_xpBar->setRange(0, 100);
                m_xpBar->setValue(0);
                m_xpBar->setFormat(QStringLiteral("Level up detected — updating XP from info..."));
            }
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastAutoInfoAt > 2500) {
                m_lastAutoInfoAt = now;
                QTimer::singleShot(250, this, [this](){ sendCommand(QStringLiteral("info")); });
            }
        }
    }

    void refreshBarVisibility() {
        if (m_hpBar) m_hpBar->setVisible(!m_hpBarCheck || m_hpBarCheck->isChecked());
        if (m_manaBar) m_manaBar->setVisible(!m_manaBarCheck || m_manaBarCheck->isChecked());
        if (m_moveBar) m_moveBar->setVisible(!m_moveBarCheck || m_moveBarCheck->isChecked());
        if (m_xpBar) m_xpBar->setVisible(!m_xpBarCheck || m_xpBarCheck->isChecked());
    }

    void buildUi() {
        m_terminal = new TerminalTextEdit;
        m_terminal->setReadOnly(true);
        m_terminal->setAcceptRichText(false);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(10);
        m_terminal->setFont(mono);
        m_terminal->document()->setMaximumBlockCount(6000);
        m_terminal->setStyleSheet(QStringLiteral("QTextEdit { background: rgba(0,0,0,0); color:#eeeeee; border:1px solid #303030; selection-background-color:#145a20; }"));

        m_input = new QLineEdit;
        m_input->setPlaceholderText(QStringLiteral("Type MUD command, or /find cave, /room 32633, /goto 32633"));
        m_input->installEventFilter(this);

        m_host = new QLineEdit(QStringLiteral("rotsmud.org"));
        m_host->setMaximumWidth(190);
        m_port = new QLineEdit(QStringLiteral("3791"));
        m_port->setMaximumWidth(72);
        m_connect = new QPushButton(QStringLiteral("Connect"));
        m_backgroundButton = new QPushButton(QStringLiteral("🖼️ Background..."));
        m_clearBackgroundButton = new QPushButton(QStringLiteral("Clear background"));
        m_textColorCombo = new QComboBox;
        fillTextColorCombo(m_textColorCombo, QStringLiteral("White"));
        m_textColorCombo->setToolTip(QStringLiteral("Default terminal text color. ANSI colors from the game can still override it until reset."));
        m_fontSizeSpin = new QSpinBox;
        m_fontSizeSpin->setRange(8, 28);
        m_fontSizeSpin->setValue(m_terminalFontSize);
        m_fontSizeSpin->setToolTip(QStringLiteral("Change terminal/input text size."));
        m_backgroundButton->setToolTip(QStringLiteral("Choose a .png/.jpg/.jpeg/.bmp/.webp image and stretch it behind the terminal text."));
        m_clearBackgroundButton->setToolTip(QStringLiteral("Remove the static terminal background and return to black."));
        connect(m_backgroundButton, &QPushButton::clicked, this, [this]() { chooseTerminalBackground(); });
        connect(m_clearBackgroundButton, &QPushButton::clicked, this, [this]() { clearTerminalBackground(); });
        connect(m_textColorCombo, &QComboBox::currentTextChanged, this, [this](const QString& name) { applyTerminalTextColor(name, true); });
        connect(m_fontSizeSpin, &QSpinBox::valueChanged, this, [this](int v) { applyTerminalFontSize(v, true); });

        QWidget* terminalPage = new QWidget;
        QVBoxLayout* terminalLayout = new QVBoxLayout(terminalPage);
        QHBoxLayout* top = new QHBoxLayout;
        top->addWidget(new QLabel(QStringLiteral("🌐 Host:")));
        top->addWidget(m_host);
        top->addWidget(new QLabel(QStringLiteral("Port:")));
        top->addWidget(m_port);
        top->addWidget(m_connect);
        top->addWidget(m_backgroundButton);
        top->addWidget(m_clearBackgroundButton);
        top->addWidget(new QLabel(QStringLiteral("Text:")));
        top->addWidget(m_textColorCombo);
        top->addWidget(new QLabel(QStringLiteral("Font:")));
        top->addWidget(m_fontSizeSpin);
        top->addStretch(1);
        terminalLayout->addLayout(top);
        terminalLayout->addWidget(m_terminal, 1);

        QWidget* barPanel = new QWidget;
        QVBoxLayout* barOuter = new QVBoxLayout(barPanel);
        barOuter->setContentsMargins(4, 2, 4, 2);
        QHBoxLayout* barChecks = new QHBoxLayout;
        m_hpBarCheck = new QCheckBox(QStringLiteral("Health bar"));
        m_manaBarCheck = new QCheckBox(QStringLiteral("Mana bar"));
        m_moveBarCheck = new QCheckBox(QStringLiteral("Movement bar"));
        m_xpBarCheck = new QCheckBox(QStringLiteral("XP bar"));
        for (QCheckBox* cb : {m_hpBarCheck, m_manaBarCheck, m_moveBarCheck, m_xpBarCheck}) { cb->setChecked(true); barChecks->addWidget(cb); }
        barChecks->addStretch(1);
        m_hpBar = new QProgressBar; m_hpBar->setFormat(QStringLiteral("HP waiting for prompt"));
        m_manaBar = new QProgressBar; m_manaBar->setFormat(QStringLiteral("Mana waiting for prompt"));
        m_moveBar = new QProgressBar; m_moveBar->setFormat(QStringLiteral("Movement waiting for prompt"));
        m_xpBar = new QProgressBar; m_xpBar->setRange(0, 100); m_xpBar->setValue(0); m_xpBar->setFormat(QStringLiteral("XP waiting for 'need to advance'"));
        m_hpBar->setStyleSheet(QStringLiteral("QProgressBar { height: 18px; text-align:center; font-weight:bold; } QProgressBar::chunk { background: rgb(210,0,0); }"));
        m_manaBar->setStyleSheet(QStringLiteral("QProgressBar { height: 18px; text-align:center; font-weight:bold; } QProgressBar::chunk { background: rgb(0,90,255); }"));
        m_moveBar->setStyleSheet(QStringLiteral("QProgressBar { height: 18px; text-align:center; font-weight:bold; } QProgressBar::chunk { background: rgb(230,210,0); }"));
        m_xpBar->setStyleSheet(QStringLiteral("QProgressBar { height: 18px; text-align:center; font-weight:bold; } QProgressBar::chunk { background: rgb(0,210,70); }"));
        barOuter->addLayout(barChecks);
        barOuter->addWidget(m_hpBar);
        barOuter->addWidget(m_manaBar);
        barOuter->addWidget(m_moveBar);
        barOuter->addWidget(m_xpBar);
        terminalLayout->addWidget(barPanel);
        terminalLayout->addWidget(m_input);
        applyTerminalFontSize(m_terminalFontSize, false);

        QWidget* aliasesPage = buildAliasesPage();
        QWidget* scriptsPage = buildScriptsPage();
        QWidget* triggersPage = buildTriggersPage();
        QWidget* roomsPage = buildRoomsPage();

        QTabWidget* leftTabs = new QTabWidget;
        leftTabs->addTab(terminalPage, QStringLiteral("🖥️ Terminal"));
        leftTabs->addTab(roomsPage, QStringLiteral("🔎 Rooms"));
        leftTabs->addTab(aliasesPage, QStringLiteral("⚡ Aliases"));
        leftTabs->addTab(scriptsPage, QStringLiteral("📜 Scripts"));
        leftTabs->addTab(triggersPage, QStringLiteral("🎯 Triggers"));

        QWidget* mapPanel = new QWidget;
        QVBoxLayout* mapLayout = new QVBoxLayout(mapPanel);
        QHBoxLayout* mapTools = new QHBoxLayout;
        m_areaCombo = new QComboBox;
        for (int areaId : m_map.sortedAreaIds()) {
            const AreaInfo a = m_map.areas.value(areaId);
            m_areaCombo->addItem(QStringLiteral("%1 — %2").arg(a.name).arg(areaId), areaId);
        }
        m_zSpin = new QSpinBox;
        m_zSpin->setRange(-20, 20);
        m_emojiCheck = new QCheckBox(QStringLiteral("emoji"));
        m_emojiCheck->setChecked(true);
        m_namesCheck = new QCheckBox(QStringLiteral("names"));
        m_gridCheck = new QCheckBox(QStringLiteral("grid"));
        m_gridCheck->setChecked(true);
        m_shapeCombo = new QComboBox;
        m_shapeCombo->addItem(QStringLiteral("Square"), QStringLiteral("square"));
        m_shapeCombo->addItem(QStringLiteral("Circle"), QStringLiteral("circle"));
        m_highlightColorCombo = new QComboBox; fillBrightColorCombo(m_highlightColorCombo, QStringLiteral("Bright Green"));
        m_lineColorCombo = new QComboBox; fillBrightColorCombo(m_lineColorCombo, QStringLiteral("Bright Green"));
        m_pulseColorCombo = new QComboBox; fillBrightColorCombo(m_pulseColorCombo, QStringLiteral("Bright Green"));
        m_startMappingButton = new QPushButton(QStringLiteral("🟢 Start Mapping"));
        m_stopMappingButton = new QPushButton(QStringLiteral("🔴 Stop Mapping"));
        QPushButton* centerButton = new QPushButton(QStringLiteral("🎯 Center"));
        m_detachMapButton = new QPushButton(QStringLiteral("🪟 Detach map"));
        m_detachMapButton->setToolTip(QStringLiteral("Open a second resizable automapper window. The main map stays docked so startup stays safe."));
        m_mapSmallerButton = new QPushButton(QStringLiteral("Map −"));
        m_mapSmallerButton->setToolTip(QStringLiteral("Make the docked automapper narrower and give the terminal more room."));
        m_mapBiggerButton = new QPushButton(QStringLiteral("Map +"));
        m_mapBiggerButton->setToolTip(QStringLiteral("Make the docked automapper wider."));
        mapTools->addWidget(m_startMappingButton);
        mapTools->addWidget(m_stopMappingButton);
        mapTools->addWidget(m_detachMapButton);
        mapTools->addWidget(m_mapSmallerButton);
        mapTools->addWidget(m_mapBiggerButton);
        mapTools->addWidget(new QLabel(QStringLiteral("🧭 Area:")));
        mapTools->addWidget(m_areaCombo, 1);
        mapTools->addWidget(new QLabel(QStringLiteral("Z:")));
        mapTools->addWidget(m_zSpin);
        mapTools->addWidget(new QLabel(QStringLiteral("Shape:")));
        mapTools->addWidget(m_shapeCombo);
        mapTools->addWidget(new QLabel(QStringLiteral("Highlight:")));
        mapTools->addWidget(m_highlightColorCombo);
        mapTools->addWidget(new QLabel(QStringLiteral("Lines:")));
        mapTools->addWidget(m_lineColorCombo);
        mapTools->addWidget(new QLabel(QStringLiteral("Pulse:")));
        mapTools->addWidget(m_pulseColorCombo);
        mapTools->addWidget(m_emojiCheck);
        mapTools->addWidget(m_namesCheck);
        mapTools->addWidget(m_gridCheck);
        mapTools->addWidget(centerButton);
        m_mapWidget = new MapWidget;
        m_mapWidget->setMap(&m_map);
        m_mapRoomLineLabel = new QLabel(QStringLiteral("No room detected yet"));
        m_mapRoomLineLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_mapRoomLineLabel->setMinimumHeight(24);
        m_mapRoomLineLabel->setStyleSheet(QStringLiteral("QLabel { color: rgb(0,255,80); background: rgba(0,0,0,210); border: 1px solid rgb(0,210,70); padding: 3px 6px; font-weight: bold; font-family: Consolas, 'Courier New', monospace; }"));
        mapLayout->addLayout(mapTools);
        mapLayout->addWidget(m_mapRoomLineLabel);
        mapLayout->addWidget(m_mapWidget, 1);

        m_mainSplitter = new QSplitter(Qt::Horizontal);
        m_mainSplitter->addWidget(leftTabs);
        m_mainSplitter->addWidget(mapPanel);
        m_mainSplitter->setChildrenCollapsible(false);
        m_mainSplitter->setHandleWidth(12);
        leftTabs->setMinimumWidth(360);
        mapPanel->setMinimumWidth(160);
        m_mainSplitter->setStretchFactor(0, 5);
        m_mainSplitter->setStretchFactor(1, 2);
        m_mainSplitter->setSizes({1100, 420});
        m_mainSplitter->setStyleSheet(QStringLiteral(
            "QSplitter::handle { background: rgb(20, 150, 40); border: 1px solid rgb(0, 255, 0); }"
            "QSplitter::handle:hover { background: rgb(0, 255, 70); }"
        ));
        setCentralWidget(m_mainSplitter);

        QAction* saveProfile = new QAction(QStringLiteral("💾 Save profile"), this);
        QAction* clearTerminal = new QAction(QStringLiteral("🧹 Clear terminal"), this);
        QAction* stopWalk = new QAction(QStringLiteral("🛑 Stop speedwalk"), this);
        menuBar()->addAction(saveProfile);
        menuBar()->addAction(clearTerminal);
        menuBar()->addAction(stopWalk);
        connect(saveProfile, &QAction::triggered, this, [this] { saveProfileToDisk(); });
        connect(clearTerminal, &QAction::triggered, this, [this] { m_terminal->clear(); });
        connect(stopWalk, &QAction::triggered, this, [this] { stopWalkNow(); });
        connect(centerButton, &QPushButton::clicked, this, [this] { if (m_mapWidget) m_mapWidget->centerCurrent(); if (m_detachedMapWidget) m_detachedMapWidget->centerCurrent(); });
        connect(m_detachMapButton, &QPushButton::clicked, this, [this] { toggleDetachedMapWindow(); });
        connect(m_mapSmallerButton, &QPushButton::clicked, this, [this] { resizeDockedMapBy(-160); });
        connect(m_mapBiggerButton, &QPushButton::clicked, this, [this] { resizeDockedMapBy(160); });

        statusBar()->showMessage(QStringLiteral("Ready."));
    }

    QWidget* buildAliasesPage() {
        QWidget* page = new QWidget;
        QVBoxLayout* root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(4);

        QHBoxLayout* toolbar = new QHBoxLayout;
        QPushButton* activate = new QPushButton(QStringLiteral("🔒 Activate"));
        QPushButton* saveAlias = new QPushButton(QStringLiteral("💾 Save Alias"));
        QPushButton* add = new QPushButton(QStringLiteral("➕ Add Alias"));
        QPushButton* addGroup = new QPushButton(QStringLiteral("📁 Add Alias Group"));
        QPushButton* remove = new QPushButton(QStringLiteral("❌ Delete Alias"));
        QPushButton* importButton = new QPushButton(QStringLiteral("⬇️ Import"));
        QPushButton* exportButton = new QPushButton(QStringLiteral("⬆️ Export"));
        QPushButton* moduleButton = new QPushButton(QStringLiteral("📦 Create Module"));
        QPushButton* saveProfileAs = new QPushButton(QStringLiteral("💾 Save Profile As"));
        QPushButton* saveProfile = new QPushButton(QStringLiteral("💾 Save Profile"));
        for (QPushButton* b : {activate, saveAlias, add, addGroup, remove, importButton, exportButton, moduleButton, saveProfileAs, saveProfile})
            toolbar->addWidget(b);
        toolbar->addStretch(1);
        root->addLayout(toolbar);

        QSplitter* splitter = new QSplitter;
        splitter->setOrientation(Qt::Horizontal);

        m_aliasList = new QListWidget;
        m_aliasList->setMinimumWidth(190);
        m_aliasList->setMaximumWidth(280);
        m_aliasList->setAlternatingRowColors(true);
        splitter->addWidget(m_aliasList);

        QWidget* editor = new QWidget;
        QVBoxLayout* layout = new QVBoxLayout(editor);
        layout->setContentsMargins(6, 0, 0, 0);
        layout->setSpacing(6);

        QHBoxLayout* nameRow = new QHBoxLayout;
        nameRow->addWidget(new QLabel(QStringLiteral("Name:")));
        m_aliasNameEdit = new QLineEdit;
        m_aliasNameEdit->setPlaceholderText(QStringLiteral("New alias"));
        nameRow->addWidget(m_aliasNameEdit, 1);
        layout->addLayout(nameRow);

        QHBoxLayout* patternRow = new QHBoxLayout;
        patternRow->addWidget(new QLabel(QStringLiteral("Pattern:")));
        m_aliasPatternEdit = new QLineEdit;
        m_aliasPatternEdit->setPlaceholderText(QStringLiteral("^mycommand$  or  command name"));
        patternRow->addWidget(m_aliasPatternEdit, 1);
        layout->addLayout(patternRow);

        QHBoxLayout* commandRow = new QHBoxLayout;
        commandRow->addWidget(new QLabel(QStringLiteral("Command:")));
        m_aliasCommandEdit = new QLineEdit;
        m_aliasCommandEdit->setPlaceholderText(QStringLiteral("Replacement text to send. Use $* for the rest of what you typed."));
        commandRow->addWidget(m_aliasCommandEdit, 1);
        layout->addLayout(commandRow);

        QLabel* note = new QLabel(QStringLiteral("Mudlet-style alias editor. These aliases run from the command line before text is sent to the MUD."));
        note->setWordWrap(true);
        layout->addWidget(note);

        m_aliasTable = new QTableWidget(0, 3);
        m_aliasTable->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Alias / pattern"), QStringLiteral("Command sent")});
        m_aliasTable->verticalHeader()->setVisible(false);
        m_aliasTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_aliasTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_aliasTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_aliasTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        layout->addWidget(m_aliasTable, 1);

        splitter->addWidget(editor);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        root->addWidget(splitter, 1);

        connect(add, &QPushButton::clicked, this, [this] { promptAddAlias(); });
        connect(remove, &QPushButton::clicked, this, [this] { removeSelectedAlias(); });
        connect(saveAlias, &QPushButton::clicked, this, [this] { saveSelectedAliasFromEditor(); });
        connect(saveProfile, &QPushButton::clicked, this, [this] { saveSelectedAliasFromEditor(); saveProfileToDisk(); });
        connect(saveProfileAs, &QPushButton::clicked, this, [this] { saveSelectedAliasFromEditor(); saveProfileToDisk(); });
        connect(activate, &QPushButton::clicked, this, [this] { appendSystem(QStringLiteral("Aliases are active automatically when they are saved.")); });
        connect(addGroup, &QPushButton::clicked, this, [this] { appendSystem(QStringLiteral("Alias groups are visual-only in this starter clone.")); });
        connect(importButton, &QPushButton::clicked, this, [this] { appendSystem(QStringLiteral("Import is a placeholder. For now use Add Alias or /alias add.")); });
        connect(exportButton, &QPushButton::clicked, this, [this] { saveSelectedAliasFromEditor(); saveProfileToDisk(); appendSystem(QStringLiteral("Aliases exported into profile JSON.")); });
        connect(moduleButton, &QPushButton::clicked, this, [this] { appendSystem(QStringLiteral("Create Module is a placeholder for later Mudlet-style package export.")); });
        connect(m_aliasList, &QListWidget::currentRowChanged, this, [this](int row) { loadAliasEditor(row); });
        connect(m_aliasTable, &QTableWidget::itemSelectionChanged, this, [this] {
            if (!m_aliasTable || !m_aliasList) return;
            const int row = m_aliasTable->currentRow();
            if (row >= 0 && row < m_aliasList->count()) m_aliasList->setCurrentRow(row);
        });
        return page;
    }

    QWidget* buildScriptsPage() {
        QWidget* page = new QWidget;
        QVBoxLayout* root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(4);

        QHBoxLayout* toolbar = new QHBoxLayout;
        QPushButton* activate = new QPushButton(QStringLiteral("🔒 Activate"));
        QPushButton* saveScript = new QPushButton(QStringLiteral("💾 Save Script"));
        QPushButton* add = new QPushButton(QStringLiteral("➕ Add Script"));
        QPushButton* addGroup = new QPushButton(QStringLiteral("📁 Add Script Group"));
        QPushButton* remove = new QPushButton(QStringLiteral("❌ Delete Script"));
        QPushButton* importButton = new QPushButton(QStringLiteral("⬇️ Import"));
        QPushButton* exportButton = new QPushButton(QStringLiteral("⬆️ Export"));
        QPushButton* moduleButton = new QPushButton(QStringLiteral("📦 Create Module"));
        QPushButton* saveProfileAs = new QPushButton(QStringLiteral("💾 Save Profile As"));
        QPushButton* saveProfile = new QPushButton(QStringLiteral("💾 Save Profile"));
        for (QPushButton* b : {activate, saveScript, add, addGroup, remove, importButton, exportButton, moduleButton, saveProfileAs, saveProfile})
            toolbar->addWidget(b);
        toolbar->addStretch(1);
        root->addLayout(toolbar);

        QSplitter* splitter = new QSplitter;
        splitter->setOrientation(Qt::Horizontal);

        m_scriptList = new QListWidget;
        m_scriptList->setMinimumWidth(190);
        m_scriptList->setMaximumWidth(280);
        m_scriptList->setAlternatingRowColors(true);
        splitter->addWidget(m_scriptList);

        QWidget* editor = new QWidget;
        QVBoxLayout* layout = new QVBoxLayout(editor);
        layout->setContentsMargins(6, 0, 0, 0);
        layout->setSpacing(6);

        QHBoxLayout* nameRow = new QHBoxLayout;
        nameRow->addWidget(new QLabel(QStringLiteral("Name:")));
        m_scriptNameEdit = new QLineEdit;
        m_scriptNameEdit->setPlaceholderText(QStringLiteral("New script"));
        nameRow->addWidget(m_scriptNameEdit, 1);
        layout->addLayout(nameRow);

        QHBoxLayout* registeredRow = new QHBoxLayout;
        registeredRow->addWidget(new QLabel(QStringLiteral("Registered Events:")));
        m_scriptRegisteredEventsEdit = new QLineEdit;
        m_scriptRegisteredEventsEdit->setPlaceholderText(QStringLiteral("roomChanged, onNewRoom, gmcp.Room.Info"));
        registeredRow->addWidget(m_scriptRegisteredEventsEdit, 1);
        QPushButton* browseEvents = new QPushButton(QStringLiteral("..."));
        registeredRow->addWidget(browseEvents);
        layout->addLayout(registeredRow);

        QHBoxLayout* userEventRow = new QHBoxLayout;
        userEventRow->addWidget(new QLabel(QStringLiteral("Add User Event:")));
        m_scriptUserEventEdit = new QLineEdit;
        m_scriptUserEventEdit->setPlaceholderText(QStringLiteral("Type an event name, then press +"));
        userEventRow->addWidget(m_scriptUserEventEdit, 1);
        QPushButton* addEvent = new QPushButton(QStringLiteral("+"));
        userEventRow->addWidget(addEvent);
        layout->addLayout(userEventRow);

        m_scriptEditor = new QPlainTextEdit;
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(10);
        m_scriptEditor->setFont(mono);
        m_scriptEditor->setPlaceholderText(QStringLiteral("-- add your Lua code here"));
        m_scriptEditor->setPlainText(QStringLiteral("-- add your Lua code here"));
        layout->addWidget(m_scriptEditor, 1);

        splitter->addWidget(editor);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        root->addWidget(splitter, 1);

        connect(add, &QPushButton::clicked, this, [this] { promptAddScript(); });
        connect(remove, &QPushButton::clicked, this, [this] { removeSelectedScript(); });
        connect(saveScript, &QPushButton::clicked, this, [this] { saveSelectedScriptFromEditor(); });
        connect(saveProfile, &QPushButton::clicked, this, [this] { saveSelectedScriptFromEditor(); saveProfileToDisk(); });
        connect(saveProfileAs, &QPushButton::clicked, this, [this] { saveSelectedScriptFromEditor(); saveProfileToDisk(); });
        connect(activate, &QPushButton::clicked, this, [this] { toggleSelectedScriptEnabled(); });
        connect(addGroup, &QPushButton::clicked, this, [this] { appendSystem(QStringLiteral("Script groups are visual-only in this starter clone.")); });
        connect(importButton, &QPushButton::clicked, this, [this] { appendSystem(QStringLiteral("Import is a placeholder. Paste code into the script editor and Save Script.")); });
        connect(exportButton, &QPushButton::clicked, this, [this] { saveSelectedScriptFromEditor(); saveProfileToDisk(); appendSystem(QStringLiteral("Scripts exported into profile JSON.")); });
        connect(moduleButton, &QPushButton::clicked, this, [this] { appendSystem(QStringLiteral("Create Module is a placeholder for later Mudlet-style package export.")); });
        connect(browseEvents, &QPushButton::clicked, this, [this] { appendSystem(QStringLiteral("Common script events: onNewRoom, roomChanged, mapChanged, gmcp.Room.Info, sysConnectionEvent.")); });
        connect(addEvent, &QPushButton::clicked, this, [this] { addUserEventToSelectedScript(); });
        connect(m_scriptList, &QListWidget::currentRowChanged, this, [this](int row) { loadScriptEditor(row); });
        return page;
    }

    QWidget* buildTriggersPage() {
        QWidget* page = new QWidget;
        QVBoxLayout* root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(4);

        QHBoxLayout* toolbar = new QHBoxLayout;
        toolbar->setSpacing(6);
        QPushButton* activate = new QPushButton(QStringLiteral("✅ Activate"));
        QPushButton* saveTrigger = new QPushButton(QStringLiteral("💾 Save Trigger"));
        QPushButton* add = new QPushButton(QStringLiteral("➕ Add Trigger"));
        QPushButton* addGroup = new QPushButton(QStringLiteral("📁 Add Trigger Group"));
        QPushButton* remove = new QPushButton(QStringLiteral("❌ Delete Trigger"));
        QPushButton* importButton = new QPushButton(QStringLiteral("⬇️ Import"));
        QPushButton* exportButton = new QPushButton(QStringLiteral("⬆️ Export"));
        QPushButton* moduleButton = new QPushButton(QStringLiteral("📦 Create Module"));
        QPushButton* restoreButton = new QPushButton(QStringLiteral("🎯 Restore RotS Automapper"));
        QPushButton* saveProfileAs = new QPushButton(QStringLiteral("💾 Save Profile As"));
        QPushButton* saveProfile = new QPushButton(QStringLiteral("💾 Save Profile"));
        for (QPushButton* b : {activate, saveTrigger, add, addGroup, remove, importButton, exportButton, moduleButton, restoreButton, saveProfileAs, saveProfile})
            toolbar->addWidget(b);
        toolbar->addStretch(1);
        root->addLayout(toolbar);

        QSplitter* splitter = new QSplitter;
        splitter->setOrientation(Qt::Horizontal);

        m_triggerList = new QListWidget;
        m_triggerList->setMinimumWidth(190);
        m_triggerList->setMaximumWidth(260);
        m_triggerList->setAlternatingRowColors(true);
        splitter->addWidget(m_triggerList);

        QWidget* editor = new QWidget;
        QVBoxLayout* layout = new QVBoxLayout(editor);
        layout->setContentsMargins(6, 0, 0, 0);
        layout->setSpacing(5);

        QHBoxLayout* nameRow = new QHBoxLayout;
        nameRow->addWidget(new QLabel(QStringLiteral("Name:")));
        m_triggerNameEdit = new QLineEdit;
        m_triggerNameEdit->setPlaceholderText(QStringLiteral("Trigger name"));
        nameRow->addWidget(m_triggerNameEdit, 1);
        nameRow->addWidget(new QLabel(QStringLiteral("Command:")));
        m_triggerCommandEdit = new QLineEdit;
        m_triggerCommandEdit->setPlaceholderText(QStringLiteral("Text to send to the game (optional)"));
        nameRow->addWidget(m_triggerCommandEdit, 1);
        layout->addLayout(nameRow);

        m_triggerTable = new QTableWidget(2, 3);
        m_triggerTable->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Pattern / regex"), QStringLiteral("Match type")});
        m_triggerTable->verticalHeader()->setVisible(false);
        m_triggerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_triggerTable->horizontalHeader()->setStretchLastSection(false);
        m_triggerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_triggerTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_triggerTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_triggerTable->setMaximumHeight(120);
        layout->addWidget(m_triggerTable);

        QLabel* info = new QLabel(QStringLiteral("RotS Automapper is built in: it watches incoming MUD output, detects (#roomNumber) + Exits are:, ignores any final Mudlet mapper id, and centers the ARDABEST map."));
        info->setWordWrap(true);
        info->setStyleSheet(QStringLiteral("QLabel { color:#304050; }"));
        layout->addWidget(info);

        m_triggerScriptEditor = new QPlainTextEdit;
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(10);
        m_triggerScriptEditor->setFont(mono);
        m_triggerScriptEditor->setPlaceholderText(QStringLiteral("-- add your Lua-style notes/code here"));
        m_triggerScriptEditor->setStyleSheet(QStringLiteral("QPlainTextEdit { background:#ffffff; color:#101010; border:1px solid #b8b8b8; }"));
        layout->addWidget(m_triggerScriptEditor, 1);

        splitter->addWidget(editor);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        root->addWidget(splitter, 1);

        connect(add, &QPushButton::clicked, this, [this] { promptAddTrigger(); });
        connect(remove, &QPushButton::clicked, this, [this] { removeSelectedTrigger(); });
        connect(saveTrigger, &QPushButton::clicked, this, [this] { saveSelectedTriggerFromEditor(); });
        connect(activate, &QPushButton::clicked, this, [this] { toggleSelectedTriggerEnabled(); });
        connect(restoreButton, &QPushButton::clicked, this, [this] { restoreDefaultAutomapperTrigger(); });
        connect(saveProfile, &QPushButton::clicked, this, [this] { saveSelectedTriggerFromEditor(); saveProfileToDisk(); });
        connect(saveProfileAs, &QPushButton::clicked, this, [this] { saveSelectedTriggerFromEditor(); saveProfileToDisk(); });
        connect(addGroup, &QPushButton::clicked, this, [this] { appendSystem(QStringLiteral("Trigger groups are visual-only in this starter clone. Add normal triggers under the list for now.")); });
        connect(importButton, &QPushButton::clicked, this, [this] { appendSystem(QStringLiteral("Import button is a placeholder. For now use /trigger add pattern => command.")); });
        connect(exportButton, &QPushButton::clicked, this, [this] { saveSelectedTriggerFromEditor(); saveProfileToDisk(); appendSystem(QStringLiteral("Triggers exported into the saved profile JSON.")); });
        connect(moduleButton, &QPushButton::clicked, this, [this] { appendSystem(QStringLiteral("Create Module is a placeholder for later Mudlet-style package export.")); });
        connect(m_triggerList, &QListWidget::currentRowChanged, this, [this](int row) { loadTriggerEditor(row); });
        return page;
    }

    QWidget* buildRoomsPage() {
        QWidget* page = new QWidget;
        QVBoxLayout* layout = new QVBoxLayout(page);
        m_searchBox = new QLineEdit;
        m_searchBox->setPlaceholderText(QStringLiteral("Search rooms, ids, terrain, or area..."));
        QPushButton* searchButton = new QPushButton(QStringLiteral("🔎 Search"));
        QHBoxLayout* searchRow = new QHBoxLayout;
        searchRow->addWidget(m_searchBox, 1);
        searchRow->addWidget(searchButton);
        m_roomList = new QListWidget;
        m_roomDetails = new QLabel(QStringLiteral("Double-click a result to select it on the map."));
        m_roomDetails->setWordWrap(true);
        layout->addLayout(searchRow);
        layout->addWidget(m_roomList, 1);
        layout->addWidget(m_roomDetails);
        connect(searchButton, &QPushButton::clicked, this, [this] { searchRoomsUi(); });
        connect(m_searchBox, &QLineEdit::returnPressed, this, [this] { searchRoomsUi(); });
        connect(m_roomList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
            const int id = item->data(Qt::UserRole).toInt();
            setCurrentRoom(id, true);
        });
        return page;
    }

    void resizeDockedMapBy(int delta) {
        if (!m_mainSplitter || m_mainSplitter->count() < 2) return;
        QList<int> sizes = m_mainSplitter->sizes();
        if (sizes.size() < 2) return;
        const int total = sizes[0] + sizes[1];
        int mapWidth = qBound(160, sizes[1] + delta, qMax(180, total - 420));
        int leftWidth = qMax(360, total - mapWidth);
        m_mainSplitter->setSizes({leftWidth, mapWidth});
        appendSystem(QStringLiteral("Docked map width changed. You can also drag the bright green divider between the terminal and map."));
    }

    void applyMapSettings(MapWidget* w, bool center) {
        if (!w || !m_areaCombo || !m_zSpin) return;
        w->setMap(&m_map);
        w->setCustomizationPath(mapCustomizationPath());
        if (!mapCustomizationPath().isEmpty()) w->loadMapCustomizations(mapCustomizationPath());
        w->setAreaAndZ(m_areaCombo->currentData().toInt(), m_zSpin->value());
        w->setEmojiEnabled(m_emojiCheck ? m_emojiCheck->isChecked() : true);
        w->setNamesEnabled(m_namesCheck ? m_namesCheck->isChecked() : false);
        w->setGridEnabled(m_gridCheck ? m_gridCheck->isChecked() : true);
        if (m_shapeCombo) w->setRoomShape(m_shapeCombo->currentData().toString());
        if (m_highlightColorCombo) w->setHighlightColor(namedBrightColor(m_highlightColorCombo->currentData().toString()));
        if (m_lineColorCombo) w->setLineColor(namedBrightColor(m_lineColorCombo->currentData().toString()));
        if (m_pulseColorCombo) w->setPulseColor(namedBrightColor(m_pulseColorCombo->currentData().toString()));
        if (m_mapWidget) w->setMappingEnabled(m_mapWidget->mappingEnabled());
        if (m_mapWidget && m_mapWidget->currentRoomId() > 0) w->setCurrentRoom(m_mapWidget->currentRoomId(), center);
    }

    void applyMapSettingsToAll(bool center) {
        applyMapSettings(m_mapWidget, center);
        applyMapSettings(m_detachedMapWidget, center);
    }

    void toggleDetachedMapWindow() {
        if (m_detachedMapWindow) {
            m_detachedMapWindow->raise();
            m_detachedMapWindow->activateWindow();
            return;
        }

        m_detachedMapWindow = new QDialog(this);
        m_detachedMapWindow->setWindowTitle(QStringLiteral("ArdaBest detached automapper"));
        m_detachedMapWindow->resize(900, 700);
        m_detachedMapWindow->setAttribute(Qt::WA_DeleteOnClose, true);

        QVBoxLayout* root = new QVBoxLayout(m_detachedMapWindow);
        QHBoxLayout* top = new QHBoxLayout;
        QPushButton* center = new QPushButton(QStringLiteral("🎯 Center"));
        QPushButton* close = new QPushButton(QStringLiteral("Dock/Close"));
        QLabel* hint = new QLabel(QStringLiteral("Detached mirror map. Resize this window freely; main map stays docked and safe."));
        hint->setStyleSheet(QStringLiteral("color: rgb(0,255,80); font-weight: bold;"));
        top->addWidget(hint, 1);
        top->addWidget(center);
        top->addWidget(close);
        root->addLayout(top);

        m_detachedRoomLineLabel = new QLabel(currentRoomLineText());
        m_detachedRoomLineLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_detachedRoomLineLabel->setMinimumHeight(24);
        m_detachedRoomLineLabel->setStyleSheet(QStringLiteral("QLabel { color: rgb(0,255,80); background: rgba(0,0,0,220); border: 1px solid rgb(0,210,70); padding: 3px 6px; font-weight: bold; font-family: Consolas, 'Courier New', monospace; }"));
        root->addWidget(m_detachedRoomLineLabel);

        m_detachedMapWidget = new MapWidget;
        root->addWidget(m_detachedMapWidget, 1);
        applyMapSettings(m_detachedMapWidget, true);
        updateMapRoomLineLabels();

        connect(center, &QPushButton::clicked, this, [this]() { if (m_detachedMapWidget) m_detachedMapWidget->centerCurrent(); });
        connect(close, &QPushButton::clicked, m_detachedMapWindow, &QDialog::close);
        connect(m_detachedMapWindow, &QObject::destroyed, this, [this]() {
            m_detachedMapWindow = nullptr;
            m_detachedMapWidget = nullptr;
            m_detachedRoomLineLabel = nullptr;
            if (m_detachMapButton) m_detachMapButton->setText(QStringLiteral("🪟 Detach map"));
        });
        if (m_detachMapButton) m_detachMapButton->setText(QStringLiteral("🪟 Map detached"));
        m_detachedMapWindow->show();
        appendSystem(QStringLiteral("Detached automapper opened. Resize or drag that window anywhere. The docked map still stays in the main client."));
    }

    void wireUi() {
        connect(m_input, &QLineEdit::returnPressed, this, [this] { handleInput(); });
        connect(m_connect, &QPushButton::clicked, this, [this] { toggleConnection(); });
        connect(m_areaCombo, &QComboBox::currentIndexChanged, this, [this] {
            applyMapSettingsToAll(false);
        });
        connect(m_zSpin, &QSpinBox::valueChanged, this, [this](int z) {
            Q_UNUSED(z); applyMapSettingsToAll(false);
        });
        connect(m_emojiCheck, &QCheckBox::toggled, this, [this](bool on) { Q_UNUSED(on); applyMapSettingsToAll(false); });
        connect(m_namesCheck, &QCheckBox::toggled, this, [this](bool on) { Q_UNUSED(on); applyMapSettingsToAll(false); });
        connect(m_gridCheck, &QCheckBox::toggled, this, [this](bool on) { Q_UNUSED(on); applyMapSettingsToAll(false); });
        connect(m_shapeCombo, &QComboBox::currentIndexChanged, this, [this] { applyMapSettingsToAll(false); });
        connect(m_highlightColorCombo, &QComboBox::currentIndexChanged, this, [this] { applyMapSettingsToAll(false); });
        connect(m_lineColorCombo, &QComboBox::currentIndexChanged, this, [this] { applyMapSettingsToAll(false); });
        connect(m_pulseColorCombo, &QComboBox::currentIndexChanged, this, [this] { applyMapSettingsToAll(false); });
        for (QCheckBox* cb : {m_hpBarCheck, m_manaBarCheck, m_moveBarCheck, m_xpBarCheck}) connect(cb, &QCheckBox::toggled, this, [this] { refreshBarVisibility(); });
        connect(m_startMappingButton, &QPushButton::clicked, this, [this] { setMappingMode(true); });
        connect(m_stopMappingButton, &QPushButton::clicked, this, [this] { setMappingMode(false); });
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == m_input && event->type() == QEvent::KeyPress) {
            QKeyEvent* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Up && !m_history.isEmpty()) {
                m_historyIndex = qMax(0, m_historyIndex - 1);
                m_input->setText(m_history.value(m_historyIndex));
                return true;
            }
            if (key->key() == Qt::Key_Down && !m_history.isEmpty()) {
                m_historyIndex = qMin(m_history.size(), m_historyIndex + 1);
                m_input->setText(m_historyIndex >= m_history.size() ? QString() : m_history.value(m_historyIndex));
                return true;
            }
        }
        return QMainWindow::eventFilter(watched, event);
    }

    QString mapCustomizationPath() const {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        return dir + QStringLiteral("/ardabest_map_customizations.json");
    }

    void saveMapToDisk() {
        if (!m_mapWidget) return;
        QString error;
        if (m_mapWidget->saveMapCustomizations(mapCustomizationPath(), &error)) {
            appendSystem(QStringLiteral("🗺️ Map saved to %1").arg(mapCustomizationPath()));
        } else {
            appendSystem(QStringLiteral("⚠️ Could not save map: %1").arg(error));
        }
    }

    QString profilePath() const {
        // NUCLEAR SAFE: use a portable profile beside the EXE, not old AppData settings.
        // This avoids crashes from corrupted saved settings from earlier experimental builds.
        QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/profile");
        QDir().mkpath(dir);
        return dir + QStringLiteral("/profile.json");
    }

    void loadProfile() {
        if (qEnvironmentVariableIsSet("ARDABEST_SAFE_MODE")) {
            ensureDefaultAutomapperTrigger(); refreshAliasTable(); refreshScriptList(); refreshTriggerTable();
            return;
        }
        QFile f(profilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            ensureDefaultAutomapperTrigger();
            refreshAliasTable();
            refreshScriptList();
            refreshTriggerTable();
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QJsonObject root = doc.object();
        m_host->setText(root.value(QStringLiteral("host")).toString(m_host->text()));
        const QString savedPort = root.value(QStringLiteral("port")).toString(m_port->text());
        m_port->setText(savedPort == QStringLiteral("4000") ? QStringLiteral("3791") : savedPort);
        const QString savedTextColor = root.value(QStringLiteral("textColor")).toString(QStringLiteral("White"));
        if (m_textColorCombo) { const int idx = m_textColorCombo->findText(savedTextColor, Qt::MatchFixedString); if (idx >= 0) m_textColorCombo->setCurrentIndex(idx); }
        applyTerminalTextColor(savedTextColor, false);
        const int savedFontSize = root.value(QStringLiteral("terminalFontSize")).toInt(m_terminalFontSize);
        applyTerminalFontSize(savedFontSize, false);
        const QString savedBackground = root.value(QStringLiteral("terminalBackgroundPath")).toString();
        if (!savedBackground.isEmpty() && !qEnvironmentVariableIsSet("ARDABEST_NO_BACKGROUND")) applyTerminalBackground(savedBackground, false);
        m_aliases.clear();
        for (auto it = root.value(QStringLiteral("aliases")).toObject().begin(); it != root.value(QStringLiteral("aliases")).toObject().end(); ++it)
            m_aliases.insert(it.key(), it.value().toString());
        m_scripts.clear();
        for (const QJsonValue& sv : root.value(QStringLiteral("scripts")).toArray()) {
            const QJsonObject o = sv.toObject();
            ScriptRule sc;
            sc.name = o.value(QStringLiteral("name")).toString(QStringLiteral("New script"));
            sc.registeredEvents = o.value(QStringLiteral("registeredEvents")).toString();
            sc.userEvent = o.value(QStringLiteral("userEvent")).toString();
            sc.script = o.value(QStringLiteral("script")).toString(QStringLiteral("-- add your Lua code here"));
            sc.enabled = o.value(QStringLiteral("enabled")).toBool(true);
            m_scripts.append(sc);
        }
        m_triggers.clear();
        for (const QJsonValue& tv : root.value(QStringLiteral("triggers")).toArray()) {
            const QJsonObject o = tv.toObject();
            TriggerRule t;
            t.name = o.value(QStringLiteral("name")).toString(QStringLiteral("New trigger"));
            t.pattern = o.value(QStringLiteral("pattern")).toString();
            t.command = o.value(QStringLiteral("command")).toString();
            t.script = o.value(QStringLiteral("script")).toString();
            t.builtin = o.value(QStringLiteral("builtin")).toString();
            t.enabled = o.value(QStringLiteral("enabled")).toBool(true);
            if (!t.pattern.isEmpty() || !t.script.isEmpty()) m_triggers.append(t);
        }
        ensureDefaultAutomapperTrigger();
        refreshAliasTable();
        refreshScriptList();
        refreshTriggerTable();
    }

    void saveProfileToDisk() {
        QJsonObject root;
        root.insert(QStringLiteral("host"), m_host->text());
        root.insert(QStringLiteral("port"), m_port->text());
        root.insert(QStringLiteral("terminalBackgroundPath"), m_terminalBackgroundPath);
        root.insert(QStringLiteral("textColor"), m_defaultTextColorName);
        root.insert(QStringLiteral("terminalFontSize"), m_terminalFontSize);
        QJsonObject aliases;
        for (auto it = m_aliases.cbegin(); it != m_aliases.cend(); ++it) aliases.insert(it.key(), it.value());
        root.insert(QStringLiteral("aliases"), aliases);
        QJsonArray scripts;
        for (const ScriptRule& sc : m_scripts) {
            QJsonObject o;
            o.insert(QStringLiteral("name"), sc.name);
            o.insert(QStringLiteral("registeredEvents"), sc.registeredEvents);
            o.insert(QStringLiteral("userEvent"), sc.userEvent);
            o.insert(QStringLiteral("script"), sc.script);
            o.insert(QStringLiteral("enabled"), sc.enabled);
            scripts.append(o);
        }
        root.insert(QStringLiteral("scripts"), scripts);
        QJsonArray triggers;
        for (const TriggerRule& t : m_triggers) {
            QJsonObject o;
            o.insert(QStringLiteral("name"), t.name);
            o.insert(QStringLiteral("pattern"), t.pattern);
            o.insert(QStringLiteral("command"), t.command);
            o.insert(QStringLiteral("script"), t.script);
            o.insert(QStringLiteral("builtin"), t.builtin);
            o.insert(QStringLiteral("enabled"), t.enabled);
            triggers.append(o);
        }
        root.insert(QStringLiteral("triggers"), triggers);
        QFile f(profilePath());
        if (!f.open(QIODevice::WriteOnly)) {
            appendSystem(QStringLiteral("⚠️ Could not save profile."));
            return;
        }
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        appendSystem(QStringLiteral("💾 Profile saved to %1").arg(profilePath()));
    }

    void appendSystem(const QString& text) {
        QTextCharFormat fmt;
        fmt.setForeground(QColor(120, 210, 255));
        QTextCursor c = m_terminal->textCursor();
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("\n[ArdaBest] %1\n").arg(text), fmt);
        m_terminal->setTextCursor(c);
        m_terminal->ensureCursorVisible();
    }

    void toggleConnection() {
        if (m_socket.state() == QAbstractSocket::ConnectedState || m_socket.state() == QAbstractSocket::ConnectingState) {
            m_socket.disconnectFromHost();
            return;
        }
        const QString host = m_host->text().trimmed();
        bool ok = false;
        const quint16 port = m_port->text().toUShort(&ok);
        if (host.isEmpty() || !ok) {
            appendSystem(QStringLiteral("Enter a host and numeric port."));
            return;
        }
        appendSystem(QStringLiteral("🌐 Connecting to %1:%2...").arg(host).arg(port));
        m_socket.connectToHost(host, port);
    }

    void handleInput() {
        const QString text = m_input->text();
        if (text.isEmpty()) return;
        m_input->clear();
        m_history.append(text);
        while (m_history.size() > 200) m_history.removeFirst();
        m_historyIndex = m_history.size();
        const QString loweredText = text.trimmed().toLower();
        if (loweredText == QStringLiteral("save map")) {
            saveMapToDisk();
        } else if (loweredText == QStringLiteral("start mapping")) {
            setMappingMode(true);
        } else if (loweredText == QStringLiteral("stop mapping")) {
            setMappingMode(false);
        } else if (loweredText == QStringLiteral("update bar") || loweredText == QStringLiteral("update bars") || loweredText == QStringLiteral("xp update") || loweredText == QStringLiteral("update xp")) {
            if (m_xpBar) {
                m_xpBar->setRange(0, 100);
                m_xpBar->setValue(0);
                m_xpBar->setFormat(QStringLiteral("XP updating from info..."));
            }
            appendSystem(QStringLiteral("📈 Updating XP bar: sending info."));
            sendCommand(QStringLiteral("info"));
        } else if (text.startsWith('/')) {
            handleLocalCommand(text.mid(1).trimmed());
        } else {
            const QString expanded = expandAlias(text);
            rememberMappingMove(expanded);
            sendCommand(expanded);
        }
    }

    void setMappingMode(bool on) {
        if (!m_mapWidget) return;
        m_mapWidget->setMappingEnabled(on);
        if (!on) m_pendingMappingDirection.clear();
        statusBar()->showMessage(on ? QStringLiteral("Start mapping ON: move n/s/e/w/u/d and new rooms/exits will be created.") : QStringLiteral("Mapping stopped."));
        appendSystem(on ? QStringLiteral("🟢 Start mapping ON. Move north/south/east/west/up/down to create/link rooms like Mudlet.") : QStringLiteral("🔴 Stop mapping. Movement will no longer create rooms."));
    }

    void rememberMappingMove(const QString& command) {
        if (!m_mapWidget || !m_mapWidget->mappingEnabled()) return;
        const QString first = command.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).value(0);
        const QString dir = MapWidget::normalizeDirection(first);
        if (!dir.isEmpty()) m_pendingMappingDirection = dir;
    }

    QString expandAlias(const QString& text) const {
        const QString trimmed = text.trimmed();
        const int space = trimmed.indexOf(QRegularExpression(QStringLiteral("\\s")));
        const QString first = (space < 0 ? trimmed : trimmed.left(space)).toLower();
        const QString rest = (space < 0 ? QString() : trimmed.mid(space + 1));
        if (!m_aliases.contains(first)) return text;
        QString expanded = m_aliases.value(first);
        expanded.replace(QStringLiteral("$*"), rest);
        return expanded;
    }

    void handleLocalCommand(const QString& cmdLine) {
        const QStringList parts = cmdLine.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.isEmpty()) return;
        const QString cmd = parts.first().toLower();
        if (cmd == QStringLiteral("help")) {
            appendSystem(QStringLiteral("Commands:\n/connect host port\n/find text\n/room roomId\n/goto roomId\n/font size\n/stopwalk\n/emoji on|off\n/alias add name command with $* optional args\n/alias del name\n/trigger add pattern command\n/trigger del number\n/save map\n/profile save\n/clear\n/set background\n/clear background"));
            return;
        }
        if (cmd == QStringLiteral("connect") && parts.size() >= 3) {
            m_host->setText(parts.at(1));
            m_port->setText(parts.at(2));
            toggleConnection();
            return;
        }
        if (cmd == QStringLiteral("clear") && parts.size() >= 2 && parts.at(1).toLower() == QStringLiteral("background")) {
            clearTerminalBackground();
            return;
        }

        if (cmd == QStringLiteral("font") && parts.size() >= 2) {
            bool ok = false;
            const int sz = parts.at(1).toInt(&ok);
            if (!ok) {
                appendSystem(QStringLiteral("Usage: /font 12"));
                return;
            }
            applyTerminalFontSize(sz, true);
            appendSystem(QStringLiteral("🔠 Terminal font size set to %1.").arg(m_terminalFontSize));
            return;
        }
        if (cmd == QStringLiteral("clear")) {
            m_terminal->clear();
            return;
        }
        if (cmd == QStringLiteral("start") && parts.size() >= 2 && parts.at(1).toLower() == QStringLiteral("mapping")) {
            setMappingMode(true);
            return;
        }
        if (cmd == QStringLiteral("stop") && parts.size() >= 2 && parts.at(1).toLower() == QStringLiteral("mapping")) {
            setMappingMode(false);
            return;
        }
        if (cmd == QStringLiteral("emoji") && parts.size() >= 2) {
            const bool on = parts.at(1).toLower() != QStringLiteral("off");
            m_emojiOutput = on;
            m_emojiCheck->setChecked(on);
            appendSystem(on ? QStringLiteral("😀 Emoji map/output helpers on.") : QStringLiteral("Emoji helpers off."));
            return;
        }
        if (cmd == QStringLiteral("save") && parts.size() >= 2 && parts.at(1).toLower() == QStringLiteral("map")) {
            saveMapToDisk();
            return;
        }
        if (cmd == QStringLiteral("map") && parts.size() >= 2 && parts.at(1).toLower() == QStringLiteral("save")) {
            saveMapToDisk();
            return;
        }
        if (cmd == QStringLiteral("set") && parts.size() >= 2 && parts.at(1).toLower() == QStringLiteral("background")) {
            chooseTerminalBackground();
            return;
        }
        if (cmd == QStringLiteral("update") && parts.size() >= 2 && (parts.at(1).toLower() == QStringLiteral("bar") || parts.at(1).toLower() == QStringLiteral("bars") || parts.at(1).toLower() == QStringLiteral("xp"))) {
            if (m_xpBar) {
                m_xpBar->setRange(0, 100);
                m_xpBar->setValue(0);
                m_xpBar->setFormat(QStringLiteral("XP updating from info..."));
            }
            appendSystem(QStringLiteral("📈 Updating XP bar: sending info."));
            sendCommand(QStringLiteral("info"));
            return;
        }
        if (cmd == QStringLiteral("profile") && parts.size() >= 2 && parts.at(1).toLower() == QStringLiteral("save")) {
            saveProfileToDisk();
            return;
        }
        if (cmd == QStringLiteral("alias")) {
            handleAliasCommand(cmdLine);
            return;
        }
        if (cmd == QStringLiteral("trigger")) {
            handleTriggerCommand(cmdLine);
            return;
        }
        if (cmd == QStringLiteral("find")) {
            const QString needle = cmdLine.mid(QStringLiteral("find").size()).trimmed();
            QList<const Room*> hits = m_map.searchRooms(needle, 35);
            if (hits.isEmpty()) {
                appendSystem(QStringLiteral("No rooms found for '%1'.").arg(needle));
            } else {
                QStringList lines;
                for (const Room* r : hits) {
                    const QString areaName = m_map.areas.contains(r->area) ? m_map.areas[r->area].name : QString::number(r->area);
                    const int mudVnum = m_map.mudVnumForRoom(*r);
                    lines << QStringLiteral("%1 MUD #%2 / mapper #%3  %4  [%5 z:%6]").arg(terrainEmojiFor(*r)).arg(mudVnum ? mudVnum : r->id).arg(r->id).arg(r->name).arg(areaName).arg(r->z);
                }
                appendSystem(QStringLiteral("Search results:\n%1").arg(lines.join('\n')));
            }
            return;
        }
        if (cmd == QStringLiteral("room") && parts.size() >= 2) {
            bool ok = false;
            const int requestedNumber = cleanRoomId(parts.at(1)).toInt(&ok);
            const int id = ok ? m_map.resolveRoomNumber(requestedNumber) : 0;
            if (id > 0 && m_map.rooms.contains(id)) {
                setCurrentRoom(id, true);
                const int mudVnum = m_map.mudVnumForRoom(m_map.rooms[id]);
                appendSystem(QStringLiteral("📍 Map room set to MUD #%1 / mapper #%2.").arg(mudVnum ? mudVnum : requestedNumber).arg(id));
            } else {
                appendSystem(QStringLiteral("Unknown room id. You can use the MUD room number, like /room 31902."));
            }
            return;
        }
        if (cmd == QStringLiteral("goto") && parts.size() >= 2) {
            bool ok = false;
            const int requestedNumber = cleanRoomId(parts.at(1)).toInt(&ok);
            const int to = ok ? m_map.resolveRoomNumber(requestedNumber) : 0;
            const int from = m_mapWidget->currentRoomId();
            if (!ok || !m_map.rooms.contains(to)) {
                appendSystem(QStringLiteral("Unknown destination room. You can use the MUD room number, like /goto 31902."));
                return;
            }
            QList<QPair<QString, int>> steps = m_map.path(from, to);
            if (steps.isEmpty()) {
                appendSystem(QStringLiteral("No path from mapper #%1 to mapper #%2.").arg(from).arg(to));
                return;
            }
            m_walkQueue.clear();
            for (const auto& step : steps) m_walkQueue.enqueue(step);
            const int mudVnum = m_map.mudVnumForRoom(m_map.rooms[to]);
            appendSystem(QStringLiteral("🚶 Walking %1 steps to MUD #%2 / mapper #%3. Use /stopwalk to cancel.").arg(steps.size()).arg(mudVnum ? mudVnum : requestedNumber).arg(to));
            if (!m_walkTimer.isActive()) m_walkTimer.start();
            return;
        }
        if (cmd == QStringLiteral("stopwalk")) {
            stopWalkNow();
            return;
        }
        appendSystem(QStringLiteral("Unknown local command. Type /help."));
    }

    void handleAliasCommand(const QString& cmdLine) {
        const QString lower = cmdLine.toLower();
        if (lower.startsWith(QStringLiteral("alias add "))) {
            QString rest = cmdLine.mid(QStringLiteral("alias add ").size()).trimmed();
            int space = rest.indexOf(QRegularExpression(QStringLiteral("\\s")));
            if (space <= 0) { appendSystem(QStringLiteral("Usage: /alias add name command")); return; }
            const QString name = rest.left(space).toLower();
            const QString command = rest.mid(space + 1).trimmed();
            m_aliases.insert(name, command);
            refreshAliasTable();
            appendSystem(QStringLiteral("⚡ Alias '%1' added.").arg(name));
            return;
        }
        if (lower.startsWith(QStringLiteral("alias del "))) {
            const QString name = cmdLine.mid(QStringLiteral("alias del ").size()).trimmed().toLower();
            m_aliases.remove(name);
            refreshAliasTable();
            appendSystem(QStringLiteral("Alias '%1' removed.").arg(name));
            return;
        }
        appendSystem(QStringLiteral("Aliases:"));
        for (auto it = m_aliases.cbegin(); it != m_aliases.cend(); ++it) appendSystem(QStringLiteral("%1 -> %2").arg(it.key(), it.value()));
    }

    void handleTriggerCommand(const QString& cmdLine) {
        const QString lower = cmdLine.toLower();
        if (lower.startsWith(QStringLiteral("trigger add "))) {
            QString rest = cmdLine.mid(QStringLiteral("trigger add ").size()).trimmed();
            int sep = rest.indexOf(QStringLiteral(" => "));
            if (sep < 0) sep = rest.indexOf(QStringLiteral(" -> "));
            if (sep < 0) { appendSystem(QStringLiteral("Usage: /trigger add pattern => command")); return; }
            TriggerRule t;
            t.name = QStringLiteral("Command trigger");
            t.pattern = rest.left(sep).trimmed();
            t.command = rest.mid(sep + 4).trimmed();
            t.script = QStringLiteral("-- command trigger created from /trigger add");
            t.enabled = true;
            m_triggers.append(t);
            refreshTriggerTable();
            appendSystem(QStringLiteral("🎯 Trigger added."));
            return;
        }
        if (lower.startsWith(QStringLiteral("trigger del "))) {
            bool ok = false;
            int index = cmdLine.mid(QStringLiteral("trigger del ").size()).trimmed().toInt(&ok);
            if (ok && index >= 1 && index <= m_triggers.size()) {
                m_triggers.removeAt(index - 1);
                refreshTriggerTable();
                appendSystem(QStringLiteral("Trigger removed."));
            }
            return;
        }
        QStringList lines;
        for (int i = 0; i < m_triggers.size(); ++i) lines << QStringLiteral("%1. %2 [%3] %4 => %5").arg(i + 1).arg(m_triggers[i].name, m_triggers[i].enabled ? QStringLiteral("on") : QStringLiteral("off"), m_triggers[i].pattern, m_triggers[i].command);
        appendSystem(lines.isEmpty() ? QStringLiteral("No triggers.") : lines.join('\n'));
    }

    static QString cleanRoomId(QString s) {
        s.remove('#');
        s.remove(QRegularExpression(QStringLiteral("[^0-9-]")));
        return s;
    }

    void sendNextWalkStep() {
        if (m_walkQueue.isEmpty()) {
            m_walkTimer.stop();
            appendSystem(QStringLiteral("✅ Speedwalk complete."));
            return;
        }
        if (m_socket.state() != QAbstractSocket::ConnectedState) {
            m_walkTimer.stop();
            appendSystem(QStringLiteral("Not connected; speedwalk stopped."));
            return;
        }
        const auto step = m_walkQueue.dequeue();
        sendCommand(step.first);
        if (m_map.rooms.contains(step.second)) setCurrentRoom(step.second, true);
    }

    void stopWalkNow() {
        m_walkQueue.clear();
        m_walkTimer.stop();
        appendSystem(QStringLiteral("🛑 Speedwalk stopped."));
    }

    void sendCommand(const QString& command) {
        const QStringList commands = command.split(';', Qt::SkipEmptyParts);
        for (const QString& raw : commands) {
            const QString one = raw.trimmed();
            if (one.isEmpty()) continue;
            QTextCharFormat fmt;
            fmt.setForeground(QColor(255, 255, 160));
            QTextCursor c = m_terminal->textCursor();
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral("> %1\n").arg(one), fmt);
            m_terminal->setTextCursor(c);
            m_terminal->ensureCursorVisible();

            if (m_socket.state() == QAbstractSocket::ConnectedState) {
                QByteArray out = one.toUtf8();
                out.append('\n');
                m_socket.write(out);
            } else {
                appendSystem(QStringLiteral("Not connected; command was not sent."));
            }
        }
    }

    void readNetwork() {
        QByteArray raw = m_socket.readAll();
        QByteArray clean = stripAndAnswerTelnet(raw);
        QString text = QString::fromUtf8(clean);
        if (text.isEmpty()) text = QString::fromLocal8Bit(clean);
        appendAnsi(text);
        const QString plainText = stripAnsi(text);
        updateBarsFromPromptLine(plainText);
        processPlainText(plainText);
    }

    QByteArray stripAndAnswerTelnet(const QByteArray& in) {
        QByteArray out;
        const unsigned char IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240;
        for (int i = 0; i < in.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(in.at(i));
            if (c != IAC) { out.append(char(c)); continue; }
            if (i + 1 >= in.size()) break;
            const unsigned char cmd = static_cast<unsigned char>(in.at(++i));
            if (cmd == IAC) { out.append(char(IAC)); continue; }
            if ((cmd == DO || cmd == DONT || cmd == WILL || cmd == WONT) && i + 1 < in.size()) {
                const unsigned char opt = static_cast<unsigned char>(in.at(++i));
                if (cmd == DO || cmd == WILL) {
                    QByteArray reply;
                    reply.append(char(IAC));
                    reply.append(char(cmd == DO ? WONT : DONT));
                    reply.append(char(opt));
                    if (m_socket.state() == QAbstractSocket::ConnectedState) m_socket.write(reply);
                }
                continue;
            }
            if (cmd == SB) {
                while (i + 1 < in.size()) {
                    if (static_cast<unsigned char>(in.at(i)) == IAC && static_cast<unsigned char>(in.at(i + 1)) == SE) { ++i; break; }
                    ++i;
                }
            }
        }
        return out;
    }

    static QString stripAnsi(QString text) {
        static const QRegularExpression ansi(QStringLiteral("\\x1B\\[[0-9;?]*[ -/]*[@-~]"));
        text.remove(ansi);
        return text;
    }

    void processPlainText(const QString& text) {
        m_plainBuffer.append(text.toUtf8());
        int idx = -1;
        while ((idx = m_plainBuffer.indexOf('\n')) >= 0) {
            QByteArray lineBytes = m_plainBuffer.left(idx);
            m_plainBuffer.remove(0, idx + 1);
            QString line = QString::fromUtf8(lineBytes).trimmed();
            if (line.isEmpty()) continue;
            updateBarsFromPromptLine(line);
            runAutomapperTrigger(line);
            runTriggers(line);
        }
    }

    bool automapperTriggerEnabled() const {
        for (const TriggerRule& t : m_triggers) {
            if (t.builtin == QStringLiteral("automapper")) return t.enabled;
        }
        return false;
    }

    void runAutomapperTrigger(const QString& line) {
        if (!automapperTriggerEnabled()) return;
        detectRoomLine(line);
    }

    void detectRoomLine(const QString& rawLine) {
        // Flexible RotS-style room detector.
        // It ignores weird room name styling and keys off (#MUD_NUMBER) + Exits are:.
        // If Start Mapping is ON and the room is not in ARDABEST, it creates the room
        // from the line that just appeared and links it to the previous room using
        // the last movement command you typed: n/s/e/w/u/d.
        QString line = rawLine.trimmed();
        static const QRegularExpression trailingMapperIdRe(QStringLiteral("\\s+\\((?!#)\\d+\\)\\s*$"));
        line.remove(trailingMapperIdRe);

        static const QRegularExpression roomWithExitsRe(
            QStringLiteral("^\\s*(?:.*?>\\s*)?(.*?)\\s*\\(#\\s*(\\d+)\\)\\s*(\\[[^\\]]+\\])?\\s*Exits?\\s*(?:are)?\\s*:\\s*(.*?)\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression anyMudRoomNumberRe(QStringLiteral("\\(#\\s*(\\d+)\\)"));

        QRegularExpressionMatch m = roomWithExitsRe.match(line);
        QString roomName;
        QString terrain;
        QString exitsText;
        int printedMudVnum = 0;
        bool ok = false;

        if (m.hasMatch()) {
            roomName = m.captured(1).trimmed();
            printedMudVnum = m.captured(2).toInt(&ok);
            terrain = m.captured(3).trimmed();
            exitsText = m.captured(4).trimmed();
        } else {
            m = anyMudRoomNumberRe.match(line);
            if (!m.hasMatch()) return;
            printedMudVnum = m.captured(1).toInt(&ok);
        }
        if (!ok) return;

        const int mapperId = m_map.resolveRoomNumber(printedMudVnum);
        if (mapperId > 0 && m_map.rooms.contains(mapperId)) {
            if (printedMudVnum > 0) {
                m_map.rooms[mapperId].mudVnum = printedMudVnum;
                m_map.mudVnumToRoomId.insert(printedMudVnum, mapperId);
                if (!roomName.isEmpty() && !m_map.rooms[mapperId].name.contains(QStringLiteral("(#"))) {
                    m_map.rooms[mapperId].name = roomName + QStringLiteral(" (#%1)").arg(printedMudVnum);
                }
                if (!terrain.isEmpty()) m_map.rooms[mapperId].terrain = terrain;
            }
            if (m_mapWidget->mappingEnabled() && !m_pendingMappingDirection.isEmpty() && m_mapWidget->currentRoomId() != mapperId) {
                m_mapWidget->linkRooms(m_mapWidget->currentRoomId(), m_pendingMappingDirection, mapperId);
                m_pendingMappingDirection.clear();
            }
            setCurrentRoom(mapperId, true);
            if (m_emojiOutput) {
                statusBar()->showMessage(QStringLiteral("Detected MUD room #%1 → mapper #%2 | %3")
                    .arg(printedMudVnum).arg(mapperId).arg(m_mapWidget->roomSummary()));
            }
            return;
        }

        if (m_mapWidget->mappingEnabled() && !m_pendingMappingDirection.isEmpty()) {
            const QStringList exitWords = exitsText.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            const int newId = m_mapWidget->createRoomFromMapping(m_pendingMappingDirection, printedMudVnum, roomName, terrain, exitWords);
            m_pendingMappingDirection.clear();
            if (newId > 0) {
                updateMapRoomLineLabels();
                appendSystem(QStringLiteral("🗺️ Mapped new room from MUD #%1 as mapper #%2 and linked exits. Type save map to keep it.").arg(printedMudVnum).arg(newId));
                statusBar()->showMessage(QStringLiteral("Mapped new room #%1").arg(newId));
            }
        }
    }

    void runTriggers(const QString& line) {
        for (const TriggerRule& t : std::as_const(m_triggers)) {
            if (!t.enabled) continue;
            if (!t.builtin.isEmpty()) continue; // built-in automapper is handled before normal command triggers
            if (t.command.trimmed().isEmpty()) continue;
            QRegularExpression re(t.pattern);
            if (!re.isValid()) continue;
            QRegularExpressionMatch match = re.match(line);
            if (match.hasMatch()) {
                QString cmd = t.command;
                for (int i = 0; i <= match.lastCapturedIndex(); ++i)
                    cmd.replace(QStringLiteral("$%1").arg(i), match.captured(i));
                sendCommand(cmd);
            }
        }
    }

    void appendAnsi(const QString& text) {
        QTextCursor c = m_terminal->textCursor();
        c.movePosition(QTextCursor::End);
        QString pending;
        for (int i = 0; i < text.size(); ++i) {
            const QChar ch = text.at(i);
            if (ch == QChar(0x1b) && i + 1 < text.size() && text.at(i + 1) == '[') {
                if (!pending.isEmpty()) { c.insertText(pending, m_currentFormat); pending.clear(); }
                int end = i + 2;
                while (end < text.size() && text.at(end) != 'm') ++end;
                if (end < text.size()) {
                    applySgr(text.mid(i + 2, end - (i + 2)));
                    i = end;
                    continue;
                }
            }
            pending.append(ch);
        }
        if (!pending.isEmpty()) c.insertText(pending, m_currentFormat);
        m_terminal->setTextCursor(c);
        m_terminal->ensureCursorVisible();
    }

    QColor xterm256(int n) const {
        static const QVector<QColor> base = {
            QColor(0,0,0), QColor(128,0,0), QColor(0,128,0), QColor(128,128,0),
            QColor(0,0,128), QColor(128,0,128), QColor(0,128,128), QColor(192,192,192),
            QColor(128,128,128), QColor(255,0,0), QColor(0,255,0), QColor(255,255,0),
            QColor(0,0,255), QColor(255,0,255), QColor(0,255,255), QColor(255,255,255)
        };
        if (n >= 0 && n < 16) return base[n];
        if (n >= 16 && n <= 231) {
            n -= 16;
            const int r = n / 36;
            const int g = (n / 6) % 6;
            const int b = n % 6;
            auto v = [](int x) { return x == 0 ? 0 : 55 + x * 40; };
            return QColor(v(r), v(g), v(b));
        }
        if (n >= 232 && n <= 255) {
            int v = 8 + (n - 232) * 10;
            return QColor(v, v, v);
        }
        return QColor(210, 210, 210);
    }

    void applySgr(const QString& seq) {
        const QList<QColor> normal = { Qt::black, QColor(170,0,0), QColor(0,170,0), QColor(170,85,0), QColor(0,0,170), QColor(170,0,170), QColor(0,170,170), QColor(210,210,210) };
        const QList<QColor> bright = { QColor(85,85,85), QColor(255,85,85), QColor(85,255,85), QColor(255,255,85), QColor(85,85,255), QColor(255,85,255), QColor(85,255,255), Qt::white };
        const QStringList parts = seq.isEmpty() ? QStringList{QStringLiteral("0")} : seq.split(';');
        for (int i = 0; i < parts.size(); ++i) {
            bool ok = false;
            int code = parts.at(i).toInt(&ok);
            if (!ok) continue;
            if (code == 0) { m_currentFormat = QTextCharFormat(); continue; }
            if (code == 1) { m_currentFormat.setFontWeight(QFont::Bold); continue; }
            if (code == 3) { m_currentFormat.setFontItalic(true); continue; }
            if (code == 4) { m_currentFormat.setFontUnderline(true); continue; }
            if (code == 22) { m_currentFormat.setFontWeight(QFont::Normal); continue; }
            if (code == 23) { m_currentFormat.setFontItalic(false); continue; }
            if (code == 24) { m_currentFormat.setFontUnderline(false); continue; }
            if (code == 39) { m_currentFormat.clearProperty(QTextFormat::ForegroundBrush); continue; }
            if (code == 49) { m_currentFormat.clearProperty(QTextFormat::BackgroundBrush); continue; }
            if (code >= 30 && code <= 37) { m_currentFormat.setForeground(normal.at(code - 30)); continue; }
            if (code >= 90 && code <= 97) { m_currentFormat.setForeground(bright.at(code - 90)); continue; }
            if (code >= 40 && code <= 47) { m_currentFormat.setBackground(normal.at(code - 40)); continue; }
            if (code >= 100 && code <= 107) { m_currentFormat.setBackground(bright.at(code - 100)); continue; }
            if ((code == 38 || code == 48) && i + 2 < parts.size()) {
                const bool foreground = (code == 38);
                const int mode = parts.at(i + 1).toInt();
                if (mode == 5 && i + 2 < parts.size()) {
                    const QColor c = xterm256(parts.at(i + 2).toInt());
                    if (foreground) m_currentFormat.setForeground(c); else m_currentFormat.setBackground(c);
                    i += 2;
                    continue;
                }
                if (mode == 2 && i + 4 < parts.size()) {
                    QColor c(parts.at(i + 2).toInt(), parts.at(i + 3).toInt(), parts.at(i + 4).toInt());
                    if (foreground) m_currentFormat.setForeground(c); else m_currentFormat.setBackground(c);
                    i += 4;
                }
            }
        }
    }

    QString currentRoomLineText() const {
        if (!m_mapWidget) return QStringLiteral("No room detected yet");
        const int id = m_mapWidget->currentRoomId();
        if (!m_map.rooms.contains(id)) return QStringLiteral("No room detected yet");
        const Room& r = m_map.rooms[id];
        int mudVnum = m_map.mudVnumForRoom(r);
        if (mudVnum <= 0) mudVnum = r.id;

        QString roomName = r.name.trimmed();
        roomName.remove(QRegularExpression(QStringLiteral("\\s*\\(#\\s*\\d+\\)\\s*")));
        if (roomName.isEmpty()) roomName = QStringLiteral("Room");

        QString terrain = r.terrain.trimmed();
        if (!terrain.isEmpty() && !terrain.startsWith('[')) terrain = QStringLiteral("[ %1 ]").arg(terrain);

        QStringList ordered;
        const QStringList order = { QStringLiteral("n"), QStringLiteral("e"), QStringLiteral("s"), QStringLiteral("w"), QStringLiteral("u"), QStringLiteral("d") };
        const QMap<QString, int> exits = r.allExits();
        QSet<QString> used;
        for (const QString& d : order) {
            if (exits.contains(d)) { ordered << d.toUpper(); used.insert(d); }
        }
        for (auto it = exits.cbegin(); it != exits.cend(); ++it) {
            const QString d = it.key().trimmed();
            if (d.isEmpty() || used.contains(d)) continue;
            ordered << d.toUpper();
        }
        const QString exitText = ordered.isEmpty() ? QStringLiteral("none") : ordered.join(' ');
        return terrain.isEmpty()
            ? QStringLiteral("%1 (#%2)   Exits are: %3").arg(roomName).arg(mudVnum).arg(exitText)
            : QStringLiteral("%1 (#%2) %3   Exits are: %4").arg(roomName).arg(mudVnum).arg(terrain).arg(exitText);
    }

    void updateMapRoomLineLabels() {
        const QString line = currentRoomLineText();
        if (m_mapRoomLineLabel) m_mapRoomLineLabel->setText(line);
        if (m_detachedRoomLineLabel) m_detachedRoomLineLabel->setText(line);
    }

    void setCurrentRoom(int id, bool center) {
        if (!m_map.rooms.contains(id)) return;
        m_mapWidget->setCurrentRoom(id, center);
        if (m_detachedMapWidget) m_detachedMapWidget->setCurrentRoom(id, center);
        updateMapRoomLineLabels();
        const Room& r = m_map.rooms[id];
        int areaIndex = m_areaCombo->findData(r.area);
        if (areaIndex >= 0) m_areaCombo->setCurrentIndex(areaIndex);
        if (m_zSpin->value() != r.z) m_zSpin->setValue(r.z);
        const int mudVnum = m_map.mudVnumForRoom(r);
        m_roomDetails->setText(QStringLiteral("%1 MUD #%2 / mapper #%3 — %4\nTerrain: %5\n%6")
                               .arg(terrainEmojiFor(r)).arg(mudVnum ? mudVnum : r.id).arg(r.id).arg(r.name).arg(r.terrain)
                               .arg(m_mapWidget->roomSummary()));
        statusBar()->showMessage(m_mapWidget->roomSummary());
    }

    void searchRoomsUi() {
        m_roomList->clear();
        const QString q = m_searchBox->text().trimmed();
        for (const Room* r : m_map.searchRooms(q, 100)) {
            const QString areaName = m_map.areas.contains(r->area) ? m_map.areas[r->area].name : QString::number(r->area);
            const int mudVnum = m_map.mudVnumForRoom(*r);
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1 MUD #%2 / mapper #%3  %4   [%5 z:%6]").arg(terrainEmojiFor(*r)).arg(mudVnum ? mudVnum : r->id).arg(r->id).arg(r->name).arg(areaName).arg(r->z));
            item->setData(Qt::UserRole, r->id);
            m_roomList->addItem(item);
        }
        if (m_roomList->count() == 0) m_roomDetails->setText(QStringLiteral("No rooms found."));
        else m_roomDetails->setText(QStringLiteral("Found %1 rooms. Double-click one to select it.").arg(m_roomList->count()));
    }

    void refreshAliasTable() {
        if (m_aliasTable) {
            m_aliasTable->setRowCount(0);
            int count = 1;
            for (auto it = m_aliases.cbegin(); it != m_aliases.cend(); ++it) {
                const int row = m_aliasTable->rowCount();
                m_aliasTable->insertRow(row);
                QTableWidgetItem* num = new QTableWidgetItem(QString::number(count++));
                num->setFlags(num->flags() & ~Qt::ItemIsEditable);
                num->setTextAlignment(Qt::AlignCenter);
                m_aliasTable->setItem(row, 0, num);
                m_aliasTable->setItem(row, 1, new QTableWidgetItem(it.key()));
                m_aliasTable->setItem(row, 2, new QTableWidgetItem(it.value()));
            }
        }
        if (m_aliasList) {
            const int oldRow = qMax(0, m_aliasList->currentRow());
            m_aliasList->blockSignals(true);
            m_aliasList->clear();
            for (auto it = m_aliases.cbegin(); it != m_aliases.cend(); ++it) {
                QListWidgetItem* item = new QListWidgetItem(QStringLiteral("☑ %1").arg(it.key()));
                item->setData(Qt::UserRole, it.key());
                m_aliasList->addItem(item);
            }
            m_aliasList->blockSignals(false);
            if (m_aliasList->count() > 0) m_aliasList->setCurrentRow(qMin(oldRow, m_aliasList->count() - 1));
            else loadAliasEditor(-1);
        }
    }

    void loadAliasEditor(int row) {
        if (!m_aliasNameEdit || !m_aliasPatternEdit || !m_aliasCommandEdit) return;
        const bool valid = m_aliasList && row >= 0 && row < m_aliasList->count();
        m_aliasNameEdit->setEnabled(valid);
        m_aliasPatternEdit->setEnabled(valid);
        m_aliasCommandEdit->setEnabled(valid);
        if (!valid) {
            m_aliasNameEdit->setText(QStringLiteral("New alias"));
            m_aliasPatternEdit->clear();
            m_aliasCommandEdit->clear();
            return;
        }
        const QString key = m_aliasList->item(row)->data(Qt::UserRole).toString();
        m_aliasNameEdit->setText(key);
        m_aliasPatternEdit->setText(key);
        m_aliasCommandEdit->setText(m_aliases.value(key));
        if (m_aliasTable && row < m_aliasTable->rowCount()) m_aliasTable->selectRow(row);
    }

    void saveSelectedAliasFromEditor() {
        if (!m_aliasNameEdit || !m_aliasPatternEdit || !m_aliasCommandEdit) return;
        QString key = m_aliasPatternEdit->text().trimmed().isEmpty() ? m_aliasNameEdit->text().trimmed() : m_aliasPatternEdit->text().trimmed();
        QString command = m_aliasCommandEdit->text().trimmed();
        if (key.isEmpty() || command.isEmpty()) return;
        key = key.toLower();
        if (m_aliasList && m_aliasList->currentRow() >= 0 && m_aliasList->currentRow() < m_aliasList->count()) {
            const QString oldKey = m_aliasList->item(m_aliasList->currentRow())->data(Qt::UserRole).toString();
            if (!oldKey.isEmpty() && oldKey != key) m_aliases.remove(oldKey);
        }
        m_aliases.insert(key, command);
        refreshAliasTable();
        appendSystem(QStringLiteral("💾 Alias saved: %1 -> %2").arg(key, command));
    }

    void promptAddAlias() {
        const QString base = QStringLiteral("newalias%1").arg(m_aliases.size() + 1);
        m_aliases.insert(base, QStringLiteral("look"));
        refreshAliasTable();
        if (m_aliasList) m_aliasList->setCurrentRow(m_aliasList->count() - 1);
        appendSystem(QStringLiteral("⚡ New alias added. Edit it and press Save Alias."));
    }

    void removeSelectedAlias() {
        if (!m_aliasList) return;
        const int row = m_aliasList->currentRow();
        if (row < 0 || row >= m_aliasList->count()) return;
        const QString key = m_aliasList->item(row)->data(Qt::UserRole).toString();
        m_aliases.remove(key);
        refreshAliasTable();
        appendSystem(QStringLiteral("Alias removed."));
    }

    void refreshScriptList() {
        if (!m_scriptList) return;
        const int oldRow = qMax(0, m_scriptList->currentRow());
        m_scriptList->blockSignals(true);
        m_scriptList->clear();
        for (int i = 0; i < m_scripts.size(); ++i) {
            const ScriptRule& sc = m_scripts[i];
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1 %2").arg(sc.enabled ? QStringLiteral("☑") : QStringLiteral("☐"), sc.name.isEmpty() ? QStringLiteral("New script") : sc.name));
            item->setData(Qt::UserRole, i);
            m_scriptList->addItem(item);
        }
        m_scriptList->blockSignals(false);
        if (!m_scripts.isEmpty()) m_scriptList->setCurrentRow(qMin(oldRow, m_scripts.size() - 1));
        else loadScriptEditor(-1);
    }

    void loadScriptEditor(int row) {
        if (!m_scriptNameEdit || !m_scriptRegisteredEventsEdit || !m_scriptUserEventEdit || !m_scriptEditor) return;
        const bool valid = row >= 0 && row < m_scripts.size();
        m_scriptNameEdit->setEnabled(valid);
        m_scriptRegisteredEventsEdit->setEnabled(valid);
        m_scriptUserEventEdit->setEnabled(valid);
        m_scriptEditor->setEnabled(valid);
        if (!valid) {
            m_scriptNameEdit->setText(QStringLiteral("New script"));
            m_scriptRegisteredEventsEdit->clear();
            m_scriptUserEventEdit->clear();
            m_scriptEditor->setPlainText(QStringLiteral("-- add your Lua code here"));
            return;
        }
        const ScriptRule& sc = m_scripts[row];
        m_scriptNameEdit->setText(sc.name);
        m_scriptRegisteredEventsEdit->setText(sc.registeredEvents);
        m_scriptUserEventEdit->setText(sc.userEvent);
        m_scriptEditor->setPlainText(sc.script);
    }

    void saveSelectedScriptFromEditor() {
        if (!m_scriptList || !m_scriptNameEdit || !m_scriptRegisteredEventsEdit || !m_scriptUserEventEdit || !m_scriptEditor) return;
        const int row = m_scriptList->currentRow();
        if (row < 0 || row >= m_scripts.size()) return;
        ScriptRule& sc = m_scripts[row];
        sc.name = m_scriptNameEdit->text().trimmed().isEmpty() ? QStringLiteral("New script") : m_scriptNameEdit->text().trimmed();
        sc.registeredEvents = m_scriptRegisteredEventsEdit->text().trimmed();
        sc.userEvent = m_scriptUserEventEdit->text().trimmed();
        sc.script = m_scriptEditor->toPlainText();
        refreshScriptList();
        if (m_scriptList) m_scriptList->setCurrentRow(row);
        appendSystem(QStringLiteral("💾 Script saved."));
    }

    void promptAddScript() {
        ScriptRule sc;
        sc.name = QStringLiteral("New script");
        sc.registeredEvents = QString();
        sc.userEvent = QString();
        sc.script = QStringLiteral("-- add your Lua code here");
        sc.enabled = true;
        m_scripts.append(sc);
        refreshScriptList();
        if (m_scriptList) m_scriptList->setCurrentRow(m_scripts.size() - 1);
        appendSystem(QStringLiteral("📜 New script added."));
    }

    void removeSelectedScript() {
        if (!m_scriptList) return;
        const int row = m_scriptList->currentRow();
        if (row < 0 || row >= m_scripts.size()) return;
        m_scripts.removeAt(row);
        refreshScriptList();
        appendSystem(QStringLiteral("Script removed."));
    }

    void toggleSelectedScriptEnabled() {
        if (!m_scriptList) return;
        const int row = m_scriptList->currentRow();
        if (row < 0 || row >= m_scripts.size()) return;
        m_scripts[row].enabled = !m_scripts[row].enabled;
        const bool on = m_scripts[row].enabled;
        refreshScriptList();
        if (m_scriptList) m_scriptList->setCurrentRow(row);
        appendSystem(QStringLiteral("%1 Script %2 is now %3.").arg(on ? QStringLiteral("✅") : QStringLiteral("☐"), m_scripts[row].name, on ? QStringLiteral("ON") : QStringLiteral("OFF")));
    }

    void addUserEventToSelectedScript() {
        if (!m_scriptRegisteredEventsEdit || !m_scriptUserEventEdit) return;
        const QString ev = m_scriptUserEventEdit->text().trimmed();
        if (ev.isEmpty()) return;
        QStringList events = m_scriptRegisteredEventsEdit->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
        if (!events.contains(ev, Qt::CaseInsensitive)) events.append(ev);
        m_scriptRegisteredEventsEdit->setText(events.join(QStringLiteral(", ")));
        m_scriptUserEventEdit->clear();
    }


    void refreshTriggerTable() {
        int oldRow = 0;
        if (m_triggerList) oldRow = qMax(0, m_triggerList->currentRow());
        if (m_triggerList) {
            m_triggerList->blockSignals(true);
            m_triggerList->clear();
            for (int i = 0; i < m_triggers.size(); ++i) {
                const TriggerRule& t = m_triggers[i];
                const QString label = QStringLiteral("%1 %2").arg(t.enabled ? QStringLiteral("☑") : QStringLiteral("☐"), t.name.isEmpty() ? QStringLiteral("New trigger") : t.name);
                QListWidgetItem* item = new QListWidgetItem(label);
                item->setData(Qt::UserRole, i);
                m_triggerList->addItem(item);
            }
            m_triggerList->blockSignals(false);
            if (!m_triggers.isEmpty()) {
                const int row = qMin(oldRow, m_triggers.size() - 1);
                m_triggerList->setCurrentRow(row);
                loadTriggerEditor(row);
            } else {
                loadTriggerEditor(-1);
            }
        } else {
            loadTriggerEditor(oldRow);
        }
    }

    void loadTriggerEditor(int row) {
        const bool valid = row >= 0 && row < m_triggers.size();
        if (m_triggerNameEdit) m_triggerNameEdit->setEnabled(valid);
        if (m_triggerCommandEdit) m_triggerCommandEdit->setEnabled(valid);
        if (m_triggerScriptEditor) m_triggerScriptEditor->setEnabled(valid);
        if (m_triggerTable) m_triggerTable->setEnabled(valid);

        if (!valid) {
            if (m_triggerNameEdit) m_triggerNameEdit->setText(QStringLiteral("New trigger"));
            if (m_triggerCommandEdit) m_triggerCommandEdit->clear();
            if (m_triggerScriptEditor) m_triggerScriptEditor->setPlainText(QStringLiteral("-- add your Lua-style notes/code here"));
            if (m_triggerTable) {
                m_triggerTable->blockSignals(true);
                m_triggerTable->setRowCount(2);
                for (int r = 0; r < 2; ++r) for (int c = 0; c < 3; ++c) delete m_triggerTable->takeItem(r, c);
                m_triggerTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("1")));
                m_triggerTable->setItem(0, 1, new QTableWidgetItem(QStringLiteral("Text to find anywhere in the game output")));
                m_triggerTable->setItem(0, 2, new QTableWidgetItem(QStringLiteral("perl regex")));
                m_triggerTable->setItem(1, 0, new QTableWidgetItem(QStringLiteral("2")));
                m_triggerTable->setItem(1, 1, new QTableWidgetItem(QStringLiteral("Text to find anywhere in the game output")));
                m_triggerTable->setItem(1, 2, new QTableWidgetItem(QStringLiteral("substring")));
                for (int r = 0; r < 2; ++r) {
                    if (auto* item = m_triggerTable->item(r, 0)) {
                        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                        item->setTextAlignment(Qt::AlignCenter);
                    }
                }
                m_triggerTable->blockSignals(false);
            }
            return;
        }

        const TriggerRule& t = m_triggers[row];
        if (m_triggerNameEdit) m_triggerNameEdit->setText(t.name.isEmpty() ? QStringLiteral("New trigger") : t.name);
        if (m_triggerCommandEdit) m_triggerCommandEdit->setText(t.command);
        if (m_triggerScriptEditor) m_triggerScriptEditor->setPlainText(t.script.isEmpty() ? QStringLiteral("-- add your Lua-style notes/code here") : t.script);
        if (m_triggerTable) {
            m_triggerTable->blockSignals(true);
            m_triggerTable->setRowCount(2);
            for (int r = 0; r < 2; ++r) for (int c = 0; c < 3; ++c) delete m_triggerTable->takeItem(r, c);
            QTableWidgetItem* num1 = new QTableWidgetItem(QStringLiteral("1"));
            num1->setFlags(num1->flags() & ~Qt::ItemIsEditable);
            num1->setTextAlignment(Qt::AlignCenter);
            m_triggerTable->setItem(0, 0, num1);
            m_triggerTable->setItem(0, 1, new QTableWidgetItem(t.pattern));
            m_triggerTable->setItem(0, 2, new QTableWidgetItem(t.builtin == QStringLiteral("automapper") ? QStringLiteral("perl regex") : QStringLiteral("substring/perl regex")));
            QTableWidgetItem* num2 = new QTableWidgetItem(QStringLiteral("2"));
            num2->setFlags(num2->flags() & ~Qt::ItemIsEditable);
            num2->setTextAlignment(Qt::AlignCenter);
            m_triggerTable->setItem(1, 0, num2);
            m_triggerTable->setItem(1, 1, new QTableWidgetItem(QStringLiteral("Text to find anywhere in the game output")));
            m_triggerTable->setItem(1, 2, new QTableWidgetItem(QStringLiteral("substring")));
            m_triggerTable->selectRow(0);
            m_triggerTable->blockSignals(false);
        }
    }

    void saveSelectedTriggerFromEditor() {
        if (!m_triggerList || !m_triggerNameEdit || !m_triggerCommandEdit || !m_triggerScriptEditor || !m_triggerTable) return;
        const int row = m_triggerList->currentRow();
        if (row < 0 || row >= m_triggers.size()) return;
        TriggerRule& t = m_triggers[row];
        t.name = m_triggerNameEdit->text().trimmed().isEmpty() ? QStringLiteral("New trigger") : m_triggerNameEdit->text().trimmed();
        t.command = m_triggerCommandEdit->text().trimmed();
        if (QTableWidgetItem* patternItem = m_triggerTable->item(0, 1)) {
            const QString pattern = patternItem->text().trimmed();
            if (!pattern.isEmpty()) t.pattern = pattern;
        }
        t.script = m_triggerScriptEditor->toPlainText();
        refreshTriggerTable();
        if (m_triggerList) m_triggerList->setCurrentRow(row);
        appendSystem(QStringLiteral("💾 Trigger saved: %1").arg(t.name));
    }

    void toggleSelectedTriggerEnabled() {
        if (!m_triggerList) return;
        const int row = m_triggerList->currentRow();
        if (row < 0 || row >= m_triggers.size()) return;
        m_triggers[row].enabled = !m_triggers[row].enabled;
        const bool on = m_triggers[row].enabled;
        const QString name = m_triggers[row].name;
        refreshTriggerTable();
        if (m_triggerList) m_triggerList->setCurrentRow(row);
        appendSystem(QStringLiteral("%1 Trigger %2 is now %3.").arg(on ? QStringLiteral("✅") : QStringLiteral("☐"), name, on ? QStringLiteral("ON") : QStringLiteral("OFF")));
    }

    void restoreDefaultAutomapperTrigger() {
        int existing = -1;
        for (int i = 0; i < m_triggers.size(); ++i) {
            if (m_triggers[i].builtin == QStringLiteral("automapper") || m_triggers[i].name.compare(QStringLiteral("RotS Automapper"), Qt::CaseInsensitive) == 0) {
                existing = i;
                break;
            }
        }
        TriggerRule t = makeDefaultAutomapperTrigger();
        if (existing >= 0) m_triggers[existing] = t;
        else {
            m_triggers.prepend(t);
            existing = 0;
        }
        refreshTriggerTable();
        if (m_triggerList) m_triggerList->setCurrentRow(existing);
        appendSystem(QStringLiteral("🎯 RotS Automapper trigger restored."));
    }

    void ensureDefaultAutomapperTrigger() {
        for (TriggerRule& t : m_triggers) {
            if (t.builtin == QStringLiteral("automapper") || t.name.compare(QStringLiteral("RotS Automapper"), Qt::CaseInsensitive) == 0) {
                t.name = QStringLiteral("RotS Automapper");
                t.pattern = defaultAutomapperPattern();
                if (t.script.trimmed().isEmpty()) t.script = defaultAutomapperScript();
                t.builtin = QStringLiteral("automapper");
                return;
            }
        }
        m_triggers.prepend(makeDefaultAutomapperTrigger());
    }

    void promptAddTrigger() {
        TriggerRule t;
        t.name = QStringLiteral("New trigger");
        t.pattern = QStringLiteral("Text to find anywhere in the game output");
        t.command = QString();
        t.script = QStringLiteral("-- add your Lua-style notes/code here");
        t.enabled = true;
        m_triggers.append(t);
        refreshTriggerTable();
        if (m_triggerList) m_triggerList->setCurrentRow(m_triggers.size() - 1);
        appendSystem(QStringLiteral("🎯 New trigger added. Edit it and press Save Trigger."));
    }

    void removeSelectedTrigger() {
        if (!m_triggerList) return;
        const int row = m_triggerList->currentRow();
        if (row < 0 || row >= m_triggers.size()) return;
        if (m_triggers[row].builtin == QStringLiteral("automapper")) {
            const QMessageBox::StandardButton answer = QMessageBox::question(this, QStringLiteral("Delete RotS Automapper?"), QStringLiteral("This trigger keeps the ARDABEST map following your room. Delete it anyway?"));
            if (answer != QMessageBox::Yes) return;
        }
        m_triggers.removeAt(row);
        refreshTriggerTable();
        appendSystem(QStringLiteral("Trigger removed."));
    }
};

static QFile* gLogFile = nullptr;
static void ardaMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    if (!gLogFile) return;
    QTextStream out(gLogFile);
    const char* level = "INFO";
    if (type == QtWarningMsg) level = "WARN";
    else if (type == QtCriticalMsg) level = "CRIT";
    else if (type == QtFatalMsg) level = "FATAL";
    out << QDateTime::currentDateTime().toString(Qt::ISODate) << " [" << level << "] " << msg << "\n";
    out.flush();
}

int main(int argc, char** argv) {
    qputenv("QT_OPENGL", "software");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "0");
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ArdaBestClient"));
    QApplication::setOrganizationName(QStringLiteral("The New Shadow"));
    QFile log(QCoreApplication::applicationDirPath() + QStringLiteral("/ardabest_startup_log.txt"));
    if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        gLogFile = &log;
        qInstallMessageHandler(ardaMessageHandler);
        qInfo() << "Starting ArdaBestClient v22 nuclear safe";
        qInfo() << "App dir:" << QCoreApplication::applicationDirPath();
    }
    try {
        QFont appFont = app.font();
        appFont.setPointSize(10);
        app.setFont(appFont);
        MainWindow w;
        w.show();
        const int rc = app.exec();
        qInfo() << "Closed normally rc=" << rc;
        return rc;
    } catch (const std::exception& e) {
        qCritical() << "C++ exception:" << e.what();
        QMessageBox::critical(nullptr, QStringLiteral("ArdaBest startup exception"), QString::fromUtf8(e.what()));
        return 2;
    } catch (...) {
        qCritical() << "Unknown C++ exception";
        QMessageBox::critical(nullptr, QStringLiteral("ArdaBest startup exception"), QStringLiteral("Unknown startup exception."));
        return 3;
    }
}
