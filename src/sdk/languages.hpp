#pragma once

#include <QLocale>
#include <QString>
#include <QVector>

namespace rl {

// A language the studio and its setup speak: its code (the suffix of
// resources/i18n/retrolarium_studio_<code>.ts) and its name in itself.
struct InterfaceLanguage {
    QString code;
    QString name;
};

// English is the source text; the others come from resources/i18n.
inline const QVector<InterfaceLanguage>& interfaceLanguages() {
    static const QVector<InterfaceLanguage> languages{
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("pt_BR"), QString::fromUtf8("Português (Brasil)")},
        {QStringLiteral("es"), QString::fromUtf8("Español")},
        {QStringLiteral("zh_CN"), QString::fromUtf8("简体中文")},
        {QStringLiteral("ja"), QString::fromUtf8("日本語")},
        {QStringLiteral("fr"), QString::fromUtf8("Français")},
        {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("it"), QStringLiteral("Italiano")},
        {QStringLiteral("pl"), QStringLiteral("Polski")},
        {QStringLiteral("ru"), QString::fromUtf8("Русский")},
    };
    return languages;
}

inline bool isInterfaceLanguage(const QString& code) {
    for (const InterfaceLanguage& language : interfaceLanguages()) {
        if (language.code == code) return true;
    }
    return false;
}

// The interface language the system asks for, when there is one: any
// Portuguese gets Brazilian Portuguese, any Chinese simplified Chinese.
inline QString systemInterfaceLanguage() {
    switch (QLocale::system().language()) {
        case QLocale::Portuguese:
            return QStringLiteral("pt_BR");
        case QLocale::Spanish:
            return QStringLiteral("es");
        case QLocale::Chinese:
            return QStringLiteral("zh_CN");
        case QLocale::Japanese:
            return QStringLiteral("ja");
        case QLocale::French:
            return QStringLiteral("fr");
        case QLocale::German:
            return QStringLiteral("de");
        case QLocale::Italian:
            return QStringLiteral("it");
        case QLocale::Polish:
            return QStringLiteral("pl");
        case QLocale::Russian:
            return QStringLiteral("ru");
        default:
            return QStringLiteral("en");
    }
}

}  // namespace rl
