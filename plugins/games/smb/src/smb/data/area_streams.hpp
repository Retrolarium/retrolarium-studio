#pragma once

#include "smb/data/level_records.hpp"

#include <QVector>

namespace rl::smb {

// Editing the two record streams of an SMB area without disturbing the rest.
//
// The game reads each stream in order while the screen scrolls: a record is
// handled when its page and column come up, so records stay in (page,
// column) order. Pages are implicit: a record's page-advance bit moves it to
// the next page, and a page command (an object in row 13 without bit $40, or
// an enemy-stream "page" record) jumps to a page. These helpers insert and
// remove single records and fix the page bits locally, so every other
// record keeps its absolute page and column, and its bytes.

// The page each record is on.
QVector<int> objectPages(const QVector<SmbAreaObject>& objects);
QVector<int> enemyPages(const QVector<SmbEnemyRecord>& enemies);

// A page command that jumps to a page.
SmbAreaObject objectPageCommand(int page);
SmbEnemyRecord enemyPageCommand(int page);
bool isPageCommand(const SmbAreaObject& object);

// Removes one record; the records after it stay where they were.
void removeObject(QVector<SmbAreaObject>& objects, int index);
void removeEnemy(QVector<SmbEnemyRecord>& enemies, int index);

// Inserts a record at an absolute page (its column and row are inside the
// page) after every record at or before that page and column. Returns its
// index. The page bits of the record are set here.
int insertObject(QVector<SmbAreaObject>& objects, int page, SmbAreaObject record);
int insertEnemy(QVector<SmbEnemyRecord>& enemies, int page, SmbEnemyRecord record);

}  // namespace rl::smb
