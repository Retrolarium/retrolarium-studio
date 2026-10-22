#pragma once

#include <QCoreApplication>
#include <QString>

// Interface text outside a Q_OBJECT class, translated in `context` (the
// shell's "studio" or a plugin's name). lupdate reads it as translate():
// run it with -tr-function-alias translate+=rlTr. Inside a Q_OBJECT class,
// tr() does the same with the class as the context.
inline QString rlTr(const char* context, const char* text) { return QCoreApplication::translate(context, text); }

// Text a plugin keeps in English because its code compares against it (a
// choice such as "Yes", a view name such as "Levels"), shown translated in
// the "Values" context. The shell applies it where it displays such text and
// hands the plugin back the English original.
inline QString rlValue(const QString& text) { return QCoreApplication::translate("Values", text.toUtf8().constData()); }
