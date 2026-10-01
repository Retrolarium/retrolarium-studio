#include "sdk/editor_model.hpp"

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
    case EditorType::Emulator:
        return QStringLiteral("EmulatorView");
    case EditorType::None:
        break;
    }
    return QStringLiteral("Editor");
}

EditorModel::~EditorModel() = default;

bool EditorModel::setField(const QString&, const QVariant&, QString* error) {
    if (error) {
        *error = QStringLiteral("This field cannot be edited.");
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
        *error = QStringLiteral("Objects cannot be placed here.");
    }
    return false;
}
bool TileMapModel::removeObject(const QString&, QString* error) {
    if (error) {
        *error = QStringLiteral("Objects cannot be removed here.");
    }
    return false;
}

bool TileModel::isTileDirty(int) const {
    return false;
}
QVector<Tool> TileModel::tools() const {
    return {{QStringLiteral("pencil"), QStringLiteral("Pencil"), QStringLiteral("pencil")},
            {QStringLiteral("fill"), QStringLiteral("Fill"), QStringLiteral("fill")},
            {QStringLiteral("pick"), QStringLiteral("Pick a pen"), QStringLiteral("pick")}};
}
void TileModel::beginStroke() {}
bool TileModel::setPixel(QPoint, int) {
    return false;
}
void TileModel::endStroke() {}
bool TileModel::fill(QPoint, int, QString* error) {
    if (error) {
        *error = QStringLiteral("This bank cannot be filled.");
    }
    return false;
}

bool PaletteModel::assignGamut(int, QString* error) {
    if (error) {
        *error = QStringLiteral("This palette has no gamut.");
    }
    return false;
}
bool PaletteModel::setChannel(int, int, QString* error) {
    if (error) {
        *error = QStringLiteral("This palette has no channels.");
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
bool TableModel::setCell(int, int, const QString&, QString* error) {
    if (error) {
        *error = QStringLiteral("This cell cannot be edited.");
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
        *error = QStringLiteral("Frames cannot be reordered here.");
    }
    return false;
}

void SequencerModel::setChannelMuted(int, bool) {}
QString SequencerModel::pitchName(int pitch) const {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (pitch < 0) {
        return QStringLiteral("Rest");
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

}  // namespace rl::sdk
