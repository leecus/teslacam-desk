#pragma once
#include <QString>

// Chinese is the source language of every string in this project; English comes from
// the compiled translation embedded in the executable. Missing translations fall back
// to the Chinese source text, so a partially translated build still reads correctly.
namespace I18n {
enum Language { Chinese, English };

Language language();
Language parse(const QString& name);
QString name(Language language);

// Installs or removes the translator; call before building widgets.
void install(Language language);
void save(Language language);
// Saved language, otherwise the system locale, otherwise Chinese.
Language load();
}
