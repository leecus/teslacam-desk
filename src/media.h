#pragma once
#include "library.h"
namespace Cam {
bool runFfmpeg(const QString& executable,const QStringList& args,std::atomic_bool& cancel,QString* error,int seconds=180);
qint64 probeDuration(const QString& executable,const QString& path,std::atomic_bool& cancel);
Result exportRange(const Source& source,const QList<Clip>& clips,const QString& camera,
                   int begin,int end,const QString& destination,const QString& executable,
                   std::atomic_bool& cancel,const Progress& progress={});
}
