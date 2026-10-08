#include "pacman/plugin/maze_model.hpp"

#include "nes/editors.hpp"
#include "pacman/plugin/pacman_session.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>

namespace rl::pacman {

namespace {

using namespace maze;

constexpr int kSheetTiles = 64;     // the engine keeps a maze tile in six bits

}  // namespace

PacmanMazeModel::PacmanMazeModel(PacmanSession* session) : TileMapModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &PacmanSession::startChanged, this, &sdk::EditorModel::toolsChanged);
}

QVector<int> PacmanMazeModel::grid() const {
    return stroke_ ? *stroke_ : session_->mazeGrid(session_->bundle());
}

QSize PacmanMazeModel::gridSize() const {
    return {kColumns, kRows};
}

QVector<sdk::Tool> PacmanMazeModel::tools() const {
    return {{QStringLiteral("select"), tr("Select a cell"), QStringLiteral("select")},
            {QStringLiteral("paint"), tr("Paint the sheet's tile (right button takes one)"), QStringLiteral("paint")},
            {QStringLiteral("pick"), tr("Take a cell's tile as the brush"), QStringLiteral("pick")}};
}

QImage PacmanMazeModel::render() const {
    const QVector<int> g = grid();
    QImage image = session_->painter().maze(g);
    QPainter painter(&image);
    // The original's differences, as the maze studio marked them.
    const QVector<int> saved = session_->mazeGrid(session_->savedBundle());
    painter.setPen(QColor(255, 200, 60, 200));
    for (int i = 0; i < g.size() && i < saved.size(); ++i) {
        if (g.at(i) != saved.at(i)) painter.drawRect((i % kColumns) * 8, (i / kColumns) * 8, 7, 7);
    }
    return image;
}

QVector<sdk::CellBrush> PacmanMazeModel::brushes() const {
    return {{tile_, QStringLiteral("%1 %2").arg(nes::hex(tile_), tileName(tile_)), QColor()}};
}

void PacmanMazeModel::setCurrentBrush(int value) {
    tile_ = std::clamp(value, 0, kSheetTiles - 1);
    emit toolsChanged();
    emit selectionChanged();
}

QString PacmanMazeModel::currentLibraryItem() const {
    return QStringLiteral("tile:%1").arg(tile_);
}

void PacmanMazeModel::setCurrentLibraryItem(const QString& id) {
    if (!id.startsWith(QLatin1String("tile:"))) return;
    const int tile = std::clamp(id.mid(5).toInt(), 0, kSheetTiles - 1);
    if (tile != tile_) {
        tile_ = tile;
        emit toolsChanged();
        emit selectionChanged();
    }
}

int PacmanMazeModel::cellValue(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= kColumns || cell.y() >= kRows) return -1;
    return grid().value(cell.y() * kColumns + cell.x(), -1);
}

// The engine blinks four fixed cells from a table and finds the eaten power
// pellet by its row and column: the power pellets cannot move. (The source
// could be extended one day to place them anywhere.)
QString PacmanMazeModel::fixedPowerPellets() {
    return tr("Power pellets cannot move: the game's code fixes their four places in the corners.");
}

bool PacmanMazeModel::isCellEditable(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= kColumns || cell.y() >= kRows) return false;
    // A corner holding its power pellet stays; one painted over in an older
    // document can only get it back.
    return !isPowerCell(cell.y(), cell.x()) || grid().at(cell.y() * kColumns + cell.x()) != 0x01;
}

QString PacmanMazeModel::lockReason(QPoint cell) const {
    if (isPowerCell(cell.y(), cell.x())) return fixedPowerPellets();
    return TileMapModel::lockReason(cell);
}

bool PacmanMazeModel::refusedTile(QPoint cell, int tile) const {
    if (isPowerCell(cell.y(), cell.x())) return tile != 0x01;
    return isPowerTile(tile);
}


bool PacmanMazeModel::commit(const QVector<int>& g, const QString& text, QString* error) {
    return session_->editBundle(
        text,
        [g](nes::Bundle& b) {
            QJsonObject maze = b.resource(ids::kMaze);
            QJsonArray rows;
            for (int r = 0; r < kRows; ++r) {
                QJsonArray row;
                for (int c = 0; c < kColumns; ++c) row << g.at(r * kColumns + c);
                rows << row;
            }
            maze.insert(QStringLiteral("tiles"), rows);
            b.setResource(ids::kMaze, maze);
        },
        error);
}

bool PacmanMazeModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    if (!libraryItem.startsWith(QLatin1String("tile:"))) return false;
    setCurrentLibraryItem(libraryItem);
    if (!isCellEditable(cell) || refusedTile(cell, tile_)) {
        // The studio shows the reason and logs it.
        if (error && (isCellEditable(cell) || isPowerCell(cell.y(), cell.x()))) *error = fixedPowerPellets();
        return false;
    }
    QVector<int> g = grid();
    g[cell.y() * kColumns + cell.x()] = tile_;
    selectObject(QStringLiteral("cell:%1").arg(cell.y() * kColumns + cell.x()));
    return commit(g, tr("Set a maze tile"), error);
}

void PacmanMazeModel::beginStroke() {
    stroke_ = grid();
    strokeChanged_ = false;
    strokeRefused_ = false;
}

bool PacmanMazeModel::paintCell(QPoint cell, int value) {
    if (!stroke_ || !isCellEditable(cell) || value < 0 || value >= kSheetTiles) return false;
    if (refusedTile(cell, value)) {
        // A power pellet painted off the corners (the editor reports locked
        // corners itself): in the log once per stroke.
        if (!strokeRefused_) {
            session_->report(sdk::Severity::Warning, QStringLiteral("levels/maze"), fixedPowerPellets());
        }
        strokeRefused_ = true;
        return false;
    }
    int& at = (*stroke_)[cell.y() * kColumns + cell.x()];
    if (at == value) return false;
    at = value;
    strokeChanged_ = true;
    emit contentChanged();
    return true;
}

void PacmanMazeModel::endStroke() {
    if (!stroke_) return;
    const QVector<int> painted = *stroke_;
    const bool changed = strokeChanged_;
    stroke_.reset();
    strokeChanged_ = false;
    if (!changed) {
        emit contentChanged();
        return;
    }
    QString error;
    commit(painted, tr("Paint the maze"), &error);
}

QVector<sdk::MapObject> PacmanMazeModel::objects() const {
    QVector<sdk::MapObject> result;
    const auto landmark = [&result](const QString& id, const QString& label, QRect cells) {
        sdk::MapObject o;
        o.id = id;
        o.label = label;
        o.cells = cells;
        o.movable = false;
        result << o;
    };
    // The engine's fixed places (maze_studio_model.semantic_overlays).
    landmark(QStringLiteral("tunnel-left"), tr("Tunnel"), QRect(0, kTunnelRow, 1, 1));
    landmark(QStringLiteral("tunnel-right"), tr("Tunnel"), QRect(kColumns - 1, kTunnelRow, 1, 1));
    landmark(QStringLiteral("house"), tr("Ghost house"), QRect(9, 11, 5, 4));
    landmark(QStringLiteral("door"), tr("Door"), QRect(kDoorColumn, kDoorRow, 1, 1));
    landmark(QStringLiteral("pacman"), tr("Pac-Man start"), QRect(kPacmanColumn, kPacmanRow, 1, 1));
    const QVector<int> g = grid();
    for (int i = 0; i < g.size(); ++i) {
        if (g.at(i) == 0x01) {
            landmark(QStringLiteral("power:%1").arg(i), tr("Power pellet"), QRect(i % kColumns, i / kColumns, 1, 1));
        }
    }
    for (int i = 0; i < g.size(); ++i) {
        sdk::MapObject o;
        o.id = QStringLiteral("cell:%1").arg(i);
        o.label = QStringLiteral("%1, %2 · %3").arg(i / kColumns).arg(i % kColumns).arg(nes::hex(g.at(i)));
        o.cells = QRect(i % kColumns, i / kColumns, 1, 1);
        o.movable = false;
        o.outlined = false;
        result << o;
    }
    return result;
}

QString PacmanMazeModel::selectedObject() const {
    return cell_ >= 0 ? QStringLiteral("cell:%1").arg(cell_) : QString();
}

void PacmanMazeModel::selectObject(const QString& id) {
    int cell = -1;
    if (id.startsWith(QLatin1String("cell:"))) cell = id.mid(5).toInt();
    if (cell != cell_) {
        cell_ = cell;
        emit selectionChanged();
    }
}

bool PacmanMazeModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (cell_ < 0 || key != QLatin1String("tile")) return TileMapModel::setField(key, value, error);
    const QPoint cell(cell_ % kColumns, cell_ / kColumns);
    const int tile = std::clamp(value.toInt(), 0, kSheetTiles - 1);
    if (!isCellEditable(cell) || refusedTile(cell, tile)) {
        if (error) *error = fixedPowerPellets();
        return false;
    }
    QVector<int> g = grid();
    g[cell_] = tile;
    return commit(g, tr("Set a maze tile"), error);
}

sdk::Library PacmanMazeModel::sheet() const {
    sdk::Library result;
    result.grid = true;
    result.columns = 16;
    result.hint = tr("click: brush · drag onto a cell");
    const QVector<int> palette = session_->painter().colours(ids::kGameplay, 1);
    for (int tile = 0; tile < kSheetTiles; ++tile) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("tile:%1").arg(tile);
        item.label = nes::hex(tile);
        item.hint = tileName(tile);
        item.icon = session_->painter().tile(tile, palette);
        result.items << item;
    }
    return result;
}

sdk::Budget PacmanMazeModel::budget() const {
    const Report report = maze::inspect(grid());
    sdk::Budget budget;
    budget.used = report.rleBytes;
    budget.capacity = kRleBytes;
    budget.text = tr("%n pellets", nullptr, report.pellets) +
                  tr(" · RLE %1 / %2 bytes").arg(report.rleBytes).arg(kRleBytes);
    return budget;
}

sdk::Inspection PacmanMazeModel::inspect() const {
    const QVector<int> g = grid();
    const Report report = maze::inspect(g);
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("pacman.maze-rle.v1");
    QString checks;
    if (report.errors.isEmpty()) checks = tr("The maze passes the engine's checks.");
    else checks = tr("To fix before a build: ") + report.errors.join(QLatin1Char(' '));
    if (!report.warnings.isEmpty()) checks += QLatin1Char(' ') + report.warnings.join(QLatin1Char(' '));
    if (cell_ < 0 || cell_ >= g.size()) {
        inspection.kicker = tr("Maze");
        inspection.title = title();
        sdk::Field pellets;
        pellets.key = QStringLiteral("pellets");
        pellets.label = tr("Pellets");
        pellets.value = tr("%n to eat, the four power pellets included", nullptr, report.pellets);
        inspection.fields = {pellets};
        inspection.note = tr("Every round plays this maze and ends when its last pellet is eaten: the build "
                             "writes the maze's count (up to 255) into the round counter. The four power "
                             "pellets, the door, the spawns and the tunnel ends stay where the engine expects "
                             "them. Changed cells are outlined. ") + checks;
        return inspection;
    }
    inspection.kicker = tr("Maze cell");
    inspection.title = tr("Row %1, column %2").arg(cell_ / kColumns).arg(cell_ % kColumns);
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = tr("Tile");
    tile.type = sdk::Field::Type::Integer;
    tile.value = g.at(cell_);
    tile.minimum = 0;
    tile.maximum = kSheetTiles - 1;
    tile.hint = QStringLiteral("%1 · %2").arg(nes::hex(g.at(cell_)), tileName(g.at(cell_)));
    tile.monospace = true;
    inspection.fields = {tile};
    inspection.note = tr("Tiles below $10 are paths Pac-Man and the ghosts can walk; $10 and up are walls. ") + checks;
    return inspection;
}

QString PacmanMazeModel::statusText() const {
    if (cell_ < 0) return tr("Maze · 27x22");
    const QVector<int> g = grid();
    return tr("Maze · row %1, column %2 · %3").arg(cell_ / kColumns).arg(cell_ % kColumns).arg(nes::hex(g.value(cell_)));
}

QString PacmanMazeModel::startPoint() const {
    return session_->selectedStart();
}

QVector<sdk::ViewOption> PacmanMazeModel::viewOptions() const {
    // Every round plays this maze; the round only picks where Play from
    // selected starts.
    sdk::ViewOption round;
    round.key = QStringLiteral("round");
    round.label = tr("Start");
    for (int r = 1; r <= 23; ++r) round.choices << tr("Round %1").arg(r);
    round.value = session_->selectedStart();
    return {round};
}

void PacmanMazeModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("round")) session_->setStartRound(value.section(QLatin1Char(' '), 1).toInt());
}

}  // namespace rl::pacman
