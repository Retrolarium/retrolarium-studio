#include "flicky/plugin/screen_model.hpp"

#include "flicky/plugin/flicky_session.hpp"

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
    if (name == QLatin1String("Sega1991")) return QStringLiteral("Copyright");
    if (name == QLatin1String("Tm")) return QStringLiteral("Trademark");
    return name;
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
    return {{QStringLiteral("select"), QStringLiteral("Pick a text"), QStringLiteral("select")}};
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
    if (id != selected_ && textIds(session_, screen_).contains(id)) {
        selected_ = id;
        emit selectionChanged();
    }
}

sdk::Inspection ScreenModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = screenTitle(screen_);
    if (selected_.isEmpty()) return inspection;
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
        inspection.note = QStringLiteral("Pick a text on the screen to edit it; the same strings are in Text. The "
                                         "logo, the cast and PUSH START BUTTON are sprites (see Animations). This is "
                                         "the overseas title (the ROM reports an overseas console).");
        break;
    case Screen::Guide:
        inspection.note = QStringLiteral("The guide shown after Start on the title: round 1's scenery, the doors and "
                                         "the chicks are drawn by the game around these lines. Pick a text to edit "
                                         "it; the same strings are in Text.");
        break;
    case Screen::Ending:
        inspection.note = QStringLiteral("The ending after the last round, before the credits roll. Pick a text to "
                                         "edit it; the same strings are in Text.");
        break;
    case Screen::Credits:
        inspection.note = QStringLiteral("The credits roll up over the ending a row every 32 frames; each row here "
                                         "is one line of the roll, in order. Leading spaces indent a line.");
        break;
    }
    inspection.engineTag = QStringLiteral("flicky.semantics.v1");
    inspection.engine = {{QStringLiteral("Text id"), selected_},
                         {QStringLiteral("Saved"), session_->savedSemantics().textValue(selected_)}};
    return inspection;
}

bool ScreenModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key != QLatin1String("text") || selected_.isEmpty()) return TileMapModel::setField(key, value, error);
    const QString id = selected_;
    const QString text = value.toString();
    try {
        session_->semantics().validateTextValue(id, text);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    return session_->editSemantics(QStringLiteral("Edit text %1").arg(id),
                                   [id, text](FlickySemanticsDocument& s) { s.setTextValue(id, text); }, error);
}

QString ScreenModel::statusText() const {
    const QSize grid = gridSize();
    return QStringLiteral("%1 · %2 × %3").arg(screenTitle(screen_)).arg(grid.width() * 8).arg(grid.height() * 8);
}

}  // namespace rl::flicky
