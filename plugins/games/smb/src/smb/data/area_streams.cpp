#include "smb/data/area_streams.hpp"

#include <algorithm>

namespace rl::smb {

namespace {

constexpr int kPageMask = 0x1f;

int objectJump(const SmbAreaObject& object) { return isPageCommand(object) ? object.control & kPageMask : -1; }

int enemyJump(const SmbEnemyRecord& record) {
    return record.kind == QLatin1String("page") ? record.objectOrPage & kPageMask : -1;
}

// The page of each record: its page-advance bit moves on one page, a page
// command jumps. Record is SmbAreaObject or SmbEnemyRecord.
template <class Record, class Jump>
QVector<int> pagesOf(const QVector<Record>& records, Jump jump) {
    QVector<int> result;
    result.reserve(records.size());
    int page = 0;
    for (const Record& record : records) {
        if (record.pageAdvance) ++page;
        const int target = jump(record);
        if (target >= 0) page = target;
        result << page;
    }
    return result;
}

template <class Record, class Jump, class Command>
void removeRecord(QVector<Record>& records, int index, Jump jump, Command command) {
    if (index < 0 || index >= records.size()) {
        return;
    }
    const QVector<int> pages = pagesOf(records, jump);
    const int next = index + 1;
    if (next < records.size() && jump(records.at(next)) < 0) {
        // The next record follows the one before the removed record now; it
        // must stay on its page.
        const int before = index > 0 ? pages.at(index - 1) : 0;
        const int step = pages.at(next) - before;
        if (step == 0 || step == 1) {
            records[next].pageAdvance = step == 1;
        } else {
            // Two pages or more: a page command takes the removed record's
            // place (no more bytes than it had).
            records[index] = command(pages.at(next) - (records.at(next).pageAdvance ? 1 : 0));
            return;
        }
    }
    records.removeAt(index);
}

template <class Record, class Jump, class Command>
int insertRecord(QVector<Record>& records, int page, Record record, Jump jump, Command command) {
    page = std::clamp(page, 0, kPageMask);
    const QVector<int> pages = pagesOf(records, jump);
    // After every record at or before (page, column); page commands count as
    // column 0 of their page.
    int index = 0;
    while (index < records.size()) {
        const int column = jump(records.at(index)) >= 0 ? 0 : records.at(index).column;
        if (pages.at(index) > page || (pages.at(index) == page && column > record.column)) {
            break;
        }
        ++index;
    }
    const int before = index > 0 ? pages.at(index - 1) : 0;
    record.pageAdvance = page == before + 1;
    if (record.pageAdvance && index < records.size() && jump(records.at(index)) < 0) {
        // The next record had advanced onto this page itself; it is reached
        // through ours now.
        records[index].pageAdvance = false;
    }
    if (page > before + 1) {
        records.insert(index, command(page));
        ++index;
    }
    records.insert(index, record);
    return index;
}

}  // namespace

bool isPageCommand(const SmbAreaObject& object) { return object.row == 13 && !(object.control & 0x40); }

SmbAreaObject objectPageCommand(int page) { return {0, 13, false, page & kPageMask}; }

SmbEnemyRecord enemyPageCommand(int page) {
    return {QStringLiteral("page"), 0, 15, false, false, page & kPageMask, -1, -1};
}

QVector<int> objectPages(const QVector<SmbAreaObject>& objects) { return pagesOf(objects, objectJump); }

QVector<int> enemyPages(const QVector<SmbEnemyRecord>& enemies) { return pagesOf(enemies, enemyJump); }

void removeObject(QVector<SmbAreaObject>& objects, int index) {
    removeRecord(objects, index, objectJump, objectPageCommand);
}

void removeEnemy(QVector<SmbEnemyRecord>& enemies, int index) {
    removeRecord(enemies, index, enemyJump, enemyPageCommand);
}

int insertObject(QVector<SmbAreaObject>& objects, int page, SmbAreaObject record) {
    return insertRecord(objects, page, record, objectJump, objectPageCommand);
}

int insertEnemy(QVector<SmbEnemyRecord>& enemies, int page, SmbEnemyRecord record) {
    return insertRecord(enemies, page, record, enemyJump, enemyPageCommand);
}

}  // namespace rl::smb
