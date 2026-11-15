#include "media.h"
#include <stdexcept>
namespace Cam {
static void fail(const QString& s){throw std::runtime_error(s.toUtf8().constData());}
bool runFfmpeg(const QString& executable,const QStringList& args,std::atomic_bool& cancel,QString* error,int seconds) {
    QProcess process; process.setProgram(executable); process.setArguments(args); process.setProcessChannelMode(QProcess::MergedChannels); process.start();
    if(!process.waitForStarted(10000)) {if(error) *error="无法启动 FFmpeg："+process.errorString(); return false;}
    QElapsedTimer elapsed; elapsed.start(); QByteArray tail;
    while(!process.waitForFinished(100)) {
        tail+=process.readAll(); if(tail.size()>12000) tail=tail.right(12000);
        if(cancel || elapsed.elapsed()>qint64(seconds)*1000) {process.kill(); process.waitForFinished(5000); if(error) *error=cancel?"操作已取消":"视频处理超时"; return false;}
    }
    tail+=process.readAll(); if(error) *error=QString::fromUtf8(tail.right(12000));
    return process.exitStatus()==QProcess::NormalExit && process.exitCode()==0;
}
qint64 probeDuration(const QString& executable,const QString& path,std::atomic_bool& cancel) {
    QString metadata;
    // With no output argument FFmpeg prints the input metadata and exits without decoding/re-encoding.
    runFfmpeg(executable,{"-hide_banner","-i",path},cancel,&metadata,30);
    if(cancel) return 0;
    static const QRegularExpression pattern("Duration: (\\d+):(\\d{2}):(\\d{2}(?:\\.\\d+)?)");
    const auto match=pattern.match(metadata);
    if(!match.hasMatch()) fail("无法读取视频时长："+QFileInfo(path).fileName()+"\n"+metadata.right(1000));
    const qint64 duration=qRound64((match.captured(1).toDouble()*3600+match.captured(2).toDouble()*60+match.captured(3).toDouble())*1000);
    if(duration<=0) fail("视频时长无效："+path);
    return duration;
}
Result exportRange(const Source& source,const QList<Clip>& clips,const QString& camera,int begin,int end,const QString& destination,const QString& executable,std::atomic_bool& cancel,const Progress& progress) {
    Result result;
    if(clips.isEmpty() || begin<0 || end<=begin || camera.isEmpty()) fail("请选择有效的录像、视角和时间范围。");
    QTemporaryDir temporary; if(!temporary.isValid()) fail("无法建立视频处理临时目录。");
    const QString root=QFileInfo(destination).canonicalFilePath();
    if(root.isEmpty() || root.compare(source.root,Qt::CaseInsensitive)==0 || isWithin(source.root,root)) fail("请选择录像目录以外的位置。");
    const auto start=QDateTime::fromString(clips.first().stamp,"yyyy-MM-dd HH:mm:ss");
    double covered=begin; QStringList parts; int index=0;
    for(const auto& clip:clips) {
        if(cancel) break;
        if(clip.camera!=camera) continue;
        const double offset=start.msecsTo(QDateTime::fromString(clip.stamp,"yyyy-MM-dd HH:mm:ss"))/1000.0;
        if(offset>=end) continue;
        const QString input=verifiedPath(source,clip);
        const double duration=probeDuration(executable,input,cancel)/1000.0;
        if(cancel) break;
        const double a=qMax(covered,offset),b=qMin(double(end),offset+duration);
        if(b<=a) continue;
        if(a>covered+0.1) fail("所选视角在时间范围内缺少片段，请缩小范围。");
        const QString part=temporary.filePath(QString::number(index++)+".mp4"); QString error;
        if(!runFfmpeg(executable,{"-hide_banner","-loglevel","error","-y","-ss",QString::number(a-offset,'f',3),"-i",input,"-t",QString::number(b-a,'f',3),"-an","-vf","scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2,setsar=1","-r","30","-c:v","libx264","-preset","veryfast","-crf","21",part},cancel,&error,600)) {
            if(cancel) break;
            fail("裁剪失败："+error);
        }
        verifiedPath(source,clip);
        const qint64 actual=probeDuration(executable,part,cancel);
        if(cancel) break;
        if(qAbs(actual-qRound64((b-a)*1000))>150) fail("片段实际可解码时长不足，未生成观看版。");
        parts << part; covered=b;
        if(progress) progress(int(covered-begin),end-begin,"裁剪 "+cameraLabel(camera)+" · "+clip.relative);
    }
    if(cancel) {result.canceled=true; return result;}
    if(covered<double(end)-0.1 || parts.isEmpty()) fail("所选视角录像不足，请调整时间范围。");
    QFile list(temporary.filePath("concat.txt")); if(!list.open(QIODevice::WriteOnly)) fail("无法写入视频拼接清单。");
    for(auto part:parts) {part.replace("'","'\\''"); list.write(("file '"+part+"'\n").toUtf8());} list.close();
    const QString name="TeslaCam-"+camera+"-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(6)+".mp4";
    const QString target=QDir(root).filePath(name),partial=target+".part.mp4"; QString error;
    try {
        if(!runFfmpeg(executable,{"-hide_banner","-loglevel","error","-n","-f","concat","-safe","0","-i",list.fileName(),"-c","copy","-movflags","+faststart",partial},cancel,&error,600)) {
            if(cancel){QFile::remove(partial);result.canceled=true;return result;}
            fail("拼接失败："+error);
        }
        const qint64 actual=probeDuration(executable,partial,cancel);
        if(cancel){QFile::remove(partial);result.canceled=true;return result;}
        if(qAbs(actual-qint64(end-begin)*1000)>200) fail("导出文件时长与所选范围不符，已停止保存。");
        if(!QFile::rename(partial,target)) fail("无法完成视频文件保存。");
    } catch(...) {QFile::remove(partial);throw;}
    result.files=1;result.bytes=QFileInfo(target).size();result.destination=target;return result;
}
}
