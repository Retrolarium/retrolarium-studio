#include "flicky/plugin/screen_model.hpp"

#include "flicky/plugin/flicky_session.hpp"
#include "sdk/i18n.hpp"

#include <QPainter>

namespace rl::flicky {

namespace {

struct ScreenDocument {
    const char* id;
    ScreenModel::Screen screen;
    const char* title;
};

constexpr ScreenDocument kScreens[] = {
    {"title", ScreenModel::Screen::Title, "Title screen"},
    {"guide", ScreenModel::Screen::Guide, "Guide"},
    {"ending", ScreenModel::Screen::Ending, "Ending"},
    {"credits", ScreenModel::Screen::Credits, "Credits"},
};

QString label(const QString& id) {
    // "Title_ChirpText" -> "Chirp", "Ending_KFuzzyText" -> "KFuzzy"
    QString name = id.section(QLatin1Char('_'), 1);
    if (name.endsWith(QLatin1String("Text"))) name.chop(4);
    if (name == QLatin1String("Sega1991")) return rlTr("flicky", "Copyright");
    if (name == QLatin1String("Tm")) return rlTr("flicky", "Trademark");
    return name;
}

QString spriteLabel(const QString& group, int index) {
    static const char* const kCast[4] = {"Flicky", "Chirp", "Tiger", "Lizard"};
    if (group == QLatin1String("cast") && index >= 0 && index < 4) return QString::fromLatin1(kCast[index]);
    return rlTr("flicky", "Logo piece %1").arg(index + 1);
}

}  // namespace

ScreenModel::ScreenModel(FlickySession* session, Screen screen)
    : TileMapModel(session), session_(session), screen_(screen) {
    connect(session_, &FlickySession::semanticsChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::graphicsChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::sequencesChanged, this, &sdk::EditorModel::contentChanged);
    const QStringList ids = textIds(session_, screen_);
    if (!ids.isEmpty()) selected_ = ids.first();
}

QStringList ScreenModel::documentIds() {
    QStringList ids;
    for (const ScreenDocument& s : kScreens) ids << QString::fromLatin1(s.id);
    return ids;
}

bool ScreenModel::screenOf(const QString& document, Screen* screen) {
    for (const ScreenDocument& s : kScreens) {
        if (document == QLatin1String(s.id)) {
            *screen = s.screen;
            return true;
        }
    }
    return false;
}

QString ScreenModel::screenTitle(Screen screen) {
    for (const ScreenDocument& s : kScreens) {
        if (s.screen == screen) return QString::fromLatin1(s.title);
    }
    return {};
}

QStringList ScreenModel::textIds(const FlickySession* session, Screen screen) {
    QStringList ids;
    try {
        for (const auto& text : session->renderer().screenTexts(screen)) ids << text.id;
    } catch (const std::exception&) {
    }
    return ids;
}

QSize ScreenModel::gridSize() const {
    try {
        const QSize pixels = session_->renderer().screenSize(screen_);
        return {pixels.width() / 8, pixels.height() / 8};
    } catch (const std::exception&) {
        return {32, 28};
    }
}

QVector<sdk::Tool> ScreenModel::tools() const {
    return {{QStringLiteral("select"), tr("Pick a text"), QStringLiteral("select")}};
}

QImage ScreenModel::render() const {
    try {
        return session_->renderer().renderScreen(screen_);
    } catch (const std::exception& e) {
        QImage image(256, 224, QImage::Format_RGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        painter.setPen(Qt::white);
        painter.drawText(image.rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                         QString::fromUtf8(e.what()));
        return image;
    }
}

QVector<sdk::MapObject> ScreenModel::objects() const {
    QVector<sdk::MapObject> result;
    QVector<FlickyLevelSceneRenderer::ScreenText> texts;
    try {
        texts = session_->renderer().screenTexts(screen_);
    } catch (const std::exception&) {
        return result;
    }
    if (movesSprites()) {
        // Under the texts, so a text over a sprite is still picked.
        for (const auto& s : session_->renderer().titleSprites()) {
            sdk::MapObject object;
            object.id = QStringLiteral("sprite:%1:%2").arg(s.group).arg(s.index);
            object.label = spriteLabel(s.group, s.index);
            const QRect b = s.bounds;
            const int left = b.left() >= 0 ? b.left() / 8 : (b.left() - 7) / 8;
            const int top = b.top() >= 0 ? b.top() / 8 : (b.top() - 7) / 8;
            object.cells = QRect(left, top, (b.right() / 8) - left + 1, (b.bottom() / 8) - top + 1);
            result.append(object);
        }
    }
    for (const auto& text : texts) {
        int length = session_->semantics().textValue(text.id).size();
        if (text.id == QLatin1String("Title_Sega1991Text")) ++length;    // the (c) before it
        sdk::MapObject object;
        object.id = text.id;
        object.label = label(text.id);
        object.cells = QRect(text.cell.x(), text.cell.y(), std::max(1, length), 1);
        object.movable = false;
        result.append(object);
    }
    return result;
}

void ScreenModel::selectObject(const QString& id) {
    if (id != selected_ && (textIds(session_, screen_).contains(id) || sprite(id) || id.isEmpty())) {
        selected_ = id;
        emit selectionChanged();
    }
}

bool ScreenModel::movesSprites() const {
    return screen_ == Screen::Title && session_->semantics().hasTitleLayout();
}

std::optional<FlickyLevelSceneRenderer::TitleSprite> ScreenModel::sprite(const QString& id) const {
    if (!movesSprites() || !id.startsWith(QLatin1String("sprite:"))) return std::nullopt;
    for (const auto& s : session_->renderer().titleSprites()) {
        if (id == QStringLiteral("sprite:%1:%2").arg(s.group).arg(s.index)) return s;
    }
    return std::nullopt;
}

void ScreenModel::beginDrag(const QString& id) {
    const auto s = sprite(id);
    if (!s) return;
    before_ = session_->semantics();
    beforeRevision_ = session_->semanticsRevision();
    // Keep the grab point: objects are dragged by their top-left cell.
    const QPoint screen = s->position - QPoint(128, 128);
    const int left = s->bounds.left() >= 0 ? s->bounds.left() / 8 : (s->bounds.left() - 7) / 8;
    const int top = s->bounds.top() >= 0 ? s->bounds.top() / 8 : (s->bounds.top() - 7) / 8;
    dragOffset_ = screen - QPoint(left * 8, top * 8);
    selectObject(id);
}

bool ScreenModel::dragObject(const QString& id, QPoint topLeftCell) {
    const auto s = sprite(id);
    if (!before_ || !s) return false;
    const QPoint position = topLeftCell * 8 + dragOffset_ + QPoint(128, 128);
    if (position == s->position) return false;
    try {
        session_->semanticsLive().setTitlePosition(s->group, s->index, position);
    } catch (const std::exception&) {
        return false;
    }
    session_->touchSemantics();
    return true;
}

void ScreenModel::endDrag() {
    const auto s = sprite(selected_);
    if (before_ && s && before_->titlePositions(s->group) != session_->semantics().titlePositions(s->group)) {
        session_->commitSemantics(*before_, beforeRevision_, tr("Move %1").arg(spriteLabel(s->group, s->index)));
    }
    before_.reset();
}

sdk::Inspection ScreenModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = screenTitle(screen_);
    if (const auto s = sprite(selected_)) {
        inspection.title = spriteLabel(s->group, s->index);
        sdk::Field x;
        x.key = QStringLiteral("x");
        x.label = QStringLiteral("X");
        x.type = sdk::Field::Type::Integer;
        x.minimum = 0;
        x.maximum = 0x1FF;
        x.value = s->position.x() - 128;
        x.hint = QStringLiteral("px");
        sdk::Field y = x;
        y.key = QStringLiteral("y");
        y.label = QStringLiteral("Y");
        y.value = s->position.y() - 128;
        inspection.fields = {x, y};
        inspection.note = tr("A title sprite placed by Title_Init. Drag it on the screen or type its "
                             "screen position; the build writes it into the title's position table.");
        inspection.engineTag = QStringLiteral("flicky.semantics.v1");
        inspection.engine = {{tr("Mapping"), s->mapping},
                             {tr("VDP position"), QStringLiteral("$%1, $%2").arg(s->position.x(), 0, 16)
                                                                   .arg(s->position.y(), 0, 16)}};
        return inspection;
    }
    if (selected_.isEmpty()) {
        if (movesSprites()) {
            inspection.title = title();
            inspection.note = tr("Pick a text to edit it, or drag the logo pieces and the cast.");
        }
        return inspection;
    }
    const FlickySemanticsDocument& s = session_->semantics();
    inspection.title = label(selected_);
    sdk::Field text;
    text.key = QStringLiteral("text");
    text.label = QStringLiteral("Text");
    text.type = sdk::Field::Type::Text;
    text.value = s.textValue(selected_);
    text.hint = QStringLiteral("%1 / %2").arg(s.textValue(selected_).size()).arg(s.textCapacity(selected_));
    inspection.fields = {text};
    switch (screen_) {
    case Screen::Title:
        inspection.note = movesSprites()
                              ? tr("Pick a text on the screen to edit it; the same strings are in Text. "
                                   "Drag the logo pieces and the cast to move them. This is the overseas "
                                   "title (the ROM reports an overseas console).")
                              : tr("Pick a text on the screen to edit it; the same strings are in Text. The "
                                   "logo, the cast and PUSH START BUTTON are sprites (see Animations). This "
                                   "is the overseas title (the ROM reports an overseas console).");
        break;
    case Screen::Guide:
        inspection.note = tr("The guide shown after Start on the title: round 1's scenery, the doors and "
                             "the chicks are drawn by the game around these lines. Pick a text to edit "
                             "it; the same strings are in Text.");
        break;
    case Screen::Ending:
        inspection.note = tr("The ending after the last round, before the credits roll. Pick a text to "
                             "edit it; the same strings are in Text.");
        break;
    case Screen::Credits:
        inspection.note = tr("The credits roll up over the ending a row every 32 frames; each row here "
                             "is one line of the roll, in order. Leading spaces indent a line.");
        break;
    }
    inspection.engineTag = QStringLiteral("flicky.semantics.v1");
    inspection.engine = {{QStringLiteral("Text id"), selected_},
                         {tr("Saved"), session_->savedSemantics().textValue(selected_)}};
    return inspection;
}

bool ScreenModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (const auto s = sprite(selected_); s && (key == QLatin1String("x") || key == QLatin1String("y"))) {
        QPoint position = s->position;
        if (key == QLatin1String("x")) position.setX(value.toInt() + 128);
        else position.setY(value.toInt() + 128);
        const QString group = s->group;
        const int index = s->index;
        return session_->editSemantics(tr("Move %1").arg(spriteLabel(group, index)),
                                       [group, index, position](FlickySemanticsDocument& d) {
                                           d.setTitlePosition(group, index, position);
                                       },
                                       error);
    }
    if (key != QLatin1String("text") || selected_.isEmpty()) return TileMapModel::setField(key, value, error);
    const QString id = selected_;
    const QString text = value.toString();
    try {
        session_->semantics().validateTextValue(id, text);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    return session_->editSemantics(tr("Edit text %1").arg(id),
                                   [id, text](FlickySemanticsDocument& s) { s.setTextValue(id, text); }, error);
}

QString ScreenModel::statusText() const {
    const QSize grid = gridSize();
    return QStringLiteral("%1 · %2 × %3").arg(screenTitle(screen_)).arg(grid.width() * 8).arg(grid.height() * 8);
}

}  // namespace rl::flicky
