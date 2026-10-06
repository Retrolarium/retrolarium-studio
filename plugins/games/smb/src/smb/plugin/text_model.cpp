#include "smb/plugin/text_model.hpp"

#include "smb/data/screens.hpp"
#include "smb/plugin/smb_session.hpp"

namespace rl::smb {

SmbTextModel::SmbTextModel(SmbSession* session) : TableModel(session), session_(session) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> SmbTextModel::columns() const {
    sdk::TableColumn name;
    name.key = QStringLiteral("name");
    name.label = tr("Name");
    name.width = 220;
    name.muted = true;
    sdk::TableColumn where;
    where.key = QStringLiteral("where");
    where.label = tr("Shown on");
    where.width = 150;
    where.muted = true;
    sdk::TableColumn text;
    text.key = QStringLiteral("text");
    text.label = tr("Game text");
    text.monospace = true;
    text.editable = true;
    sdk::TableColumn length;
    length.key = QStringLiteral("length");
    length.label = tr("Length");
    length.width = 170;
    length.budget = true;
    return {name, where, text, length};
}

int SmbTextModel::rowCount() const {
    return session_->textSlots().size();
}

QString SmbTextModel::cell(int row, int column) const {
    const QVector<SmbTextSlot>& slotList = session_->textSlots();
    if (row < 0 || row >= slotList.size()) {
        return {};
    }
    const SmbTextSlot& slot = slotList.at(row);
    if (column == 0) return slot.label;
    if (column == 1) return slot.screen.isEmpty() ? tr("Game logic") : slot.screenLabel;
    if (column == 2) return session_->text(row);
    return {};
}

sdk::Budget SmbTextModel::cellBudget(int row, int) const {
    sdk::Budget budget;
    if (row >= 0 && row < rowCount()) {
        budget.used = session_->text(row).size();
        budget.capacity = session_->textSlots().at(row).maxWidth;
    }
    return budget;
}

int SmbTextModel::maxLength(int row, int) const {
    return row >= 0 && row < rowCount() ? session_->textSlots().at(row).maxWidth : -1;
}

bool SmbTextModel::isRowDirty(int row) const {
    if (row < 0 || row >= rowCount()) {
        return false;
    }
    return session_->text(row) != slotText(session_->saved(), session_->textSlots().at(row));
}

void SmbTextModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool SmbTextModel::setText(int row, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount()) {
        return false;
    }
    const SmbTextSlot slot = session_->textSlots().at(row);
    return session_->edit(tr("Edit %1").arg(slot.label),
                          [slot, value](SmbResourceDocument& d) { setSlotText(d, slot, value); }, error);
}

bool SmbTextModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column != 2) {
        return TableModel::setCell(row, column, value, error);
    }
    return setText(row, value, error);
}

bool SmbTextModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("text")) {
        return setText(row_, value.toString(), error);
    }
    return TableModel::setField(key, value, error);
}

sdk::Budget SmbTextModel::budget() const {
    int used = 0;
    int capacity = 0;
    for (int row = 0; row < rowCount(); ++row) {
        used += session_->text(row).size();
        capacity += session_->textSlots().at(row).width;
    }
    sdk::Budget budget;
    budget.used = used;
    budget.capacity = capacity;
    budget.text = tr("text %1 / %2 chars").arg(used).arg(capacity);
    const SmbScreenPool pool = screenPool(session_->document());
    if (pool.pooled) {
        // Screen texts that grow take from the bytes the game screens share.
        budget.used = pool.used;
        budget.capacity = pool.size;
        budget.text = tr("text %1 chars · game screens %2 / %3 bytes")
                          .arg(used)
                          .arg(pool.used)
                          .arg(pool.size);
    }
    return budget;
}

sdk::Inspection SmbTextModel::inspect() const {
    sdk::Inspection inspection;
    if (row_ < 0 || row_ >= rowCount()) {
        return inspection;
    }
    const SmbTextSlot& slot = session_->textSlots().at(row_);
    const QString value = session_->text(row_);
    inspection.kicker = slot.screen.isEmpty() ? QStringLiteral("Text field") : tr("Screen text");
    inspection.title = slot.label;
    sdk::Field text;
    text.key = QStringLiteral("text");
    text.label = QStringLiteral("Text");
    text.type = sdk::Field::Type::Text;
    text.value = value;
    const bool grows = slot.maxWidth != slot.width;
    text.hint = tr("max %1").arg(slot.maxWidth);
    text.monospace = true;
    sdk::Field length;
    length.key = QStringLiteral("length");
    length.label = tr("Length");
    length.value = QStringLiteral("%1 / %2").arg(value.size()).arg(slot.maxWidth);
    length.hint = grows ? tr("grows with the text") : tr("fixed slot");
    length.monospace = true;
    inspection.fields = {text, length};
    if (!slot.screen.isEmpty()) {
        sdk::Field where;
        where.key = QStringLiteral("where");
        where.label = tr("Screen");
        where.value = slot.screenLabel;
        inspection.fields << where;
    }
    inspection.preview = value;
    const SmbTextEncoding encoding = slotEncoding(session_->document(), slot);
    inspection.note = grows ? tr("This text is as long as you type it, up to the end of its row: the "
                                 "game screens share their bytes, so a longer text needs another "
                                 "screen text shorter. The font has %1; lowercase becomes uppercase.")
                                  .arg(encoding.summary())
                            : tr("Each text has a fixed slot in the ROM; shorter text is padded with "
                                 "blanks. The font has %1; lowercase becomes uppercase.")
                                  .arg(encoding.summary());
    if (!slot.screen.isEmpty()) {
        inspection.note += tr(" Screens shows where it is drawn.");
    }
    inspection.engineTag = slot.screen.isEmpty() ? QStringLiteral("nes.fixed-text-fields.v1")
                                                 : QStringLiteral("nes.ppu-screen-packets.v1");
    inspection.engine = {{tr("Resource"), slot.resource},
                         {tr("Key"), slot.key},
                         {tr("Saved"), slotText(session_->saved(), slot)}};
    return inspection;
}

QString SmbTextModel::statusText() const {
    if (row_ < 0 || row_ >= rowCount()) {
        return {};
    }
    const SmbTextSlot& slot = session_->textSlots().at(row_);
    return QStringLiteral("%1 · %2 / %3").arg(slot.label).arg(session_->text(row_).size()).arg(slot.maxWidth);
}

}  // namespace rl::smb
