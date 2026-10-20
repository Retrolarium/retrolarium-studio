#include "sdk/editor_model.hpp"

#include "sdk/i18n.hpp"

#include <algorithm>

namespace rl::sdk {

QString editorName(EditorType type) {
    switch (type) {
    case EditorType::TileMap:
        return QStringLiteral("TileMapEditor");
    case EditorType::Tile:
        return QStringLiteral("TileEditor");
    case EditorType::Palette:
        return QStringLiteral("PaletteEditor");
    case EditorType::Composition:
        return QStringLiteral("CompositionEditor");
    case EditorType::Table:
        return QStringLiteral("TableEditor");
    case EditorType::Sequencer:
        return QStringLiteral("SequencerEditor");
    case EditorType::Hex:
        return QStringLiteral("HexEditor");
    case EditorType::Emulator:
        return QStringLiteral("EmulatorView");
    case EditorType::None:
        break;
    }
    return rlTr("studio", "Editor");
}

EditorModel::~EditorModel() = default;

bool EditorModel::setField(const QString&, const QVariant&, QString* error) {
    if (error) {
        *error = tr("This field cannot be edited.");
    }
    return false;
}

void EditorModel::setViewOption(const QString&, const QString&) {}
void EditorModel::setCurrentLibraryItem(const QString&) {}

void TileMapModel::setCurrentLayer(const QString&) {}
void TileMapModel::setCurrentBrush(int) {}
int TileMapModel::cellValue(QPoint) const {
    return 0;
}
bool CompositionModel::dropItem(const QString&, const QString&, QString* error) {
    if (error) {
        *error = tr("Nothing can be dropped here.");
    }
    return false;
}

QString TileMapModel::lockReason(QPoint) const {
    return tr("This cell cannot be painted.");
}

bool TileMapModel::isCellEditable(QPoint) const {
    return false;
}
void TileMapModel::beginStroke() {}
bool TileMapModel::paintCell(QPoint, int) {
    return false;
}
void TileMapModel::endStroke() {}
void TileMapModel::selectObject(const QString&) {}
void TileMapModel::beginDrag(const QString&) {}
bool TileMapModel::dragObject(const QString&, QPoint) {
    return false;
}
void TileMapModel::endDrag() {}
bool TileMapModel::placeObject(const QString&, QPoint, QString* error) {
    if (error) {
        *error = tr("Objects cannot be placed here.");
    }
    return false;
}
bool TileMapModel::removeObject(const QString&, QString* error) {
    if (error) {
        *error = tr("Objects cannot be removed here.");
    }
    return false;
}

bool TileModel::isTileDirty(int) const {
    return false;
}
QVector<Tool> TileModel::tools() const {
    return {{QStringLiteral("pencil"), tr("Pencil"), QStringLiteral("pencil")},
            {QStringLiteral("fill"), tr("Fill"), QStringLiteral("fill")},
            {QStringLiteral("pick"), tr("Pick a pen"), QStringLiteral("pick")}};
}
void TileModel::beginStroke() {}
bool TileModel::setPixel(QPoint, int) {
    return false;
}
void TileModel::endStroke() {}
bool TileModel::fill(QPoint, int, QString* error) {
    if (error) {
        *error = tr("This bank cannot be filled.");
    }
    return false;
}

bool PaletteModel::assignGamut(int, QString* error) {
    if (error) {
        *error = tr("This palette has no gamut.");
    }
    return false;
}
bool PaletteModel::setChannel(int, int, QString* error) {
    if (error) {
        *error = tr("This palette has no channels.");
    }
    return false;
}

Budget TableModel::cellBudget(int, int) const {
    return {};
}
int TableModel::maxLength(int, int) const {
    return -1;
}
bool TableModel::isRowDirty(int) const {
    return false;
}
QStringList TableModel::cellChoices(int, int) const {
    return {};
}

QVector<QImage> TableModel::cellChoiceIcons(int, int) const {
    return {};
}

bool TableModel::setCell(int, int, const QString&, QString* error) {
    if (error) {
        *error = tr("This cell cannot be edited.");
    }
    return false;
}

QRect CompositionModel::focusRect() const {
    return QRect(QPoint(0, 0), canvasSize());
}
void CompositionModel::selectPiece(const QString&) {}
void CompositionModel::beginDrag(const QString&) {}
bool CompositionModel::dragPiece(const QString&, QPoint) {
    return false;
}
void CompositionModel::endDrag() {}
void CompositionModel::setCurrentFrame(int) {}
int CompositionModel::frameDurationMs(int) const {
    return 100;
}
bool CompositionModel::moveFrame(int, int, QString* error) {
    if (error) {
        *error = tr("Frames cannot be reordered here.");
    }
    return false;
}

void SequencerModel::setChannelMuted(int, bool) {}
QString SequencerModel::pitchName(int pitch) const {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (pitch < 0) {
        return tr("Rest");
    }
    return QStringLiteral("%1%2").arg(QString::fromLatin1(names[pitch % 12])).arg(pitch / 12);
}
void SequencerModel::selectNote(const QString&) {}
void SequencerModel::beginEdit(const QString&) {}
bool SequencerModel::editNote(const QString&, int, int) {
    return false;
}
void SequencerModel::endEdit() {}
void SequencerModel::preview() {}
void SequencerModel::stopPreview() {}
void SequencerModel::setPreviewLoops(bool) {}

bool HexModel::isByteDirty(int) const {
    return false;
}

void HexModel::setCursor(int offset) {
    offset = std::clamp(offset, 0, std::max(0, size() - 1));
    if (offset == cursor_) return;
    cursor_ = offset;
    emit selectionChanged();
}

void HexModel::setSelection(int from, int to) {
    const int count = size();
    if (count <= 0) return;
    from = std::clamp(from, 0, count - 1);
    to = std::clamp(to, 0, count - 1);
    if (from > to) std::swap(from, to);
    if (from == first_ && to == last_) return;
    first_ = from;
    last_ = to;
    emit selectionChanged();
}

void HexModel::clearSelection() {
    if (first_ < 0) return;
    first_ = last_ = -1;
    emit selectionChanged();
}

}  // namespace rl::sdk
