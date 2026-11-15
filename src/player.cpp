#include "player.h"
#include "ui.h"
#include <QPainter>

class SpanBar:public QWidget {
public:
    struct Span { qint64 start=0, end=0; bool known=false; };
    void setSpans(const QVector<Span>& values,qint64 total) { spans=values; length=total; setVisible(total>0 && !spans.isEmpty()); update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(),Theme::color("track"));
        if(length<=0) return;
        const qreal scale=width()/qreal(length);
        for(const auto& span:spans) {
            const int x=qRound(span.start*scale), w=qMax(2,qRound((span.end-span.start)*scale));
            painter.fillRect(x,0,w,height(),Theme::color(span.known?"spanKnown":"spanEstimate"));
        }
    }
private:
    QVector<Span> spans;
    qint64 length=0;
};

static QString clockText(qint64 ms) {
    const qint64 s=ms/1000;
    return QString("%1:%2").arg(s/60,2,10,QChar('0')).arg(s%60,2,10,QChar('0'));
}
EventPlayer::EventPlayer(QWidget* parent):QWidget(parent) {
    auto layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(10);
    surface=new QWidget; grid=new QGridLayout(surface); grid->setContentsMargins(0,0,0,0); grid->setSpacing(8);
    surface->setMinimumSize(400,240); layout->addWidget(surface,1);
    marker=new QLabel; marker->setObjectName("muted"); marker->hide(); layout->addWidget(marker);
    slider=new QSlider(Qt::Horizontal); slider->setAccessibleName(tr("录像时间轴")); slider->setSingleStep(1000); slider->setPageStep(10000); slider->setAccessibleDescription(tr("方向键移动一秒，上下翻页键移动十秒")); layout->addWidget(slider);
    coverage=new SpanBar; coverage->setFixedHeight(8); coverage->setAccessibleName(tr("已录片段")); coverage->setToolTip(tr("深色是已确认的画面，浅色按约一分钟估算")); layout->addWidget(coverage);
    transport=new QWidget; auto controls=new QHBoxLayout(transport); controls->setContentsMargins(0,0,0,0);
    auto backward=new QPushButton(tr("−10 秒")); play=new QPushButton(tr("播放")); auto forward=new QPushButton(tr("+10 秒"));
    backward->setAccessibleName(tr("后退十秒")); forward->setAccessibleName(tr("前进十秒"));
    speed=new QComboBox; speed->addItems({"0.5×","1×","2×","4×"}); speed->setCurrentIndex(1); speed->setAccessibleName(tr("播放速度"));
    time=new QLabel("00:00 / 00:00"); time->setAccessibleName(tr("播放位置与总时长"));
    controls->addWidget(backward); controls->addWidget(play); controls->addWidget(forward); controls->addWidget(speed); controls->addStretch(); controls->addWidget(time); layout->addWidget(transport);
    tools=new QWidget; auto actions=new QHBoxLayout(tools); actions->setContentsMargins(0,0,0,0);
    cameraChoice=new QComboBox; cameraChoice->setMinimumWidth(75); cameraChoice->setAccessibleName(tr("截图与裁剪视角"));
    auto screenshot=new QPushButton(tr("截图")); screenshot->setAccessibleName(tr("保存所选视角截图"));
    cropToggle=new QToolButton; cropToggle->setText(tr("裁剪导出")); cropToggle->setCheckable(true); cropToggle->setArrowType(Qt::RightArrow); cropToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); cropToggle->setAccessibleName(tr("展开裁剪导出"));
    actions->addWidget(Ui::label(tr("视角"),cameraChoice)); actions->addWidget(cameraChoice); actions->addWidget(screenshot); actions->addWidget(cropToggle); actions->addStretch();
    actions->addWidget(Ui::info(tr("播放与导出说明"),tr("双击画面全屏，Esc 返回。未确认时长的片段按约一分钟估算，时间轴浅色是估算、深色是已确认。截图与裁剪使用所选视角；裁剪导出为观看版，不替代原片。"))); layout->addWidget(tools);
    connect(screenshot,&QPushButton::clicked,this,&EventPlayer::saveScreenshot);
    rangePanel=new QWidget; rangePanel->setObjectName("rangePanel"); auto range=new QHBoxLayout(rangePanel); range->setContentsMargins(10,10,10,10);
    from=new QSpinBox; to=new QSpinBox; from->setSuffix(tr(" 秒")); to->setSuffix(tr(" 秒"));
    from->setAccessibleName(tr("裁剪起点秒数")); to->setAccessibleName(tr("裁剪终点秒数"));
    auto exportButton=new QPushButton(tr("导出片段")); exportButton->setObjectName("primary");
    range->addWidget(Ui::label(tr("开始"),from)); range->addWidget(from); range->addWidget(Ui::label(tr("结束"),to)); range->addWidget(to); range->addWidget(exportButton); layout->addWidget(rangePanel);
    connect(cropToggle,&QToolButton::toggled,this,[this](bool expanded){rangePanel->setVisible(expanded); cropToggle->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow); cropToggle->setAccessibleName(expanded?tr("收起裁剪导出"):tr("展开裁剪导出"));});
    connect(exportButton,&QPushButton::clicked,this,&EventPlayer::rangeExportRequested);
    connect(play,&QPushButton::clicked,this,[this] {
        if(clips.isEmpty()) return;
        if(playing) pause();
        else { if(position>=length) position=0; playing=true; anchor=position; elapsed.restart(); play->setText(tr("暂停")); sync(); }
    });
    connect(backward,&QPushButton::clicked,this,[this]{seek(position-10000);});
    connect(forward,&QPushButton::clicked,this,[this]{seek(position+10000);});
    connect(slider,&QSlider::sliderMoved,this,[this](int v){seek(v);});
    connect(slider,&QSlider::valueChanged,this,[this](int v){if(!slider->isSliderDown() && qAbs(position-v)>150) seek(v);});
    connect(speed,&QComboBox::currentIndexChanged,this,[this]{anchor=position; elapsed.restart(); sync();});
    connect(cameraChoice,&QComboBox::currentIndexChanged,this,[this]{refreshCoverage();});
    connect(&timer,&QTimer::timeout,this,[this] {
        if(!playing) return;
        const double rates[]={0.5,1,2,4};
        position=qMin(length,anchor+qint64(elapsed.elapsed()*rates[speed->currentIndex()]));
        if(position>=length) pause();
        sync();
    });
    timer.start(80);
    clear();
}
void EventPlayer::clear() {
    pause(); clips.clear(); durations.clear();
    for(auto lane:lanes) { lane->player->stop(); delete lane->player; delete lane; }
    lanes.clear();
    while(auto item=grid->takeAt(0)) { delete item->widget(); delete item; }
    auto empty=new QLabel(tr("未选择录像")); empty->setAlignment(Qt::AlignCenter); empty->setObjectName("muted"); grid->addWidget(empty,0,0);
    cameraChoice->clear(); length=position=0; slider->setRange(0,0); from->setRange(0,0); to->setRange(0,0); static_cast<SpanBar*>(coverage)->setSpans({},0);
    time->setText("00:00 / 00:00"); marker->clear(); marker->hide(); slider->hide(); transport->hide(); tools->hide(); cropToggle->setChecked(false); rangePanel->hide();
}
void EventPlayer::load(const Cam::Source& src,const QList<Cam::Clip>& values,const QString& trigger) {
    clear(); source=src; clips=values; if(clips.isEmpty()) return;
    while(auto item=grid->takeAt(0)) {delete item->widget(); delete item;}
    slider->show(); transport->show(); tools->show();
    start=QDateTime::fromString(clips.first().stamp,"yyyy-MM-dd HH:mm:ss");
    for(const auto& c:clips) if(c.duration>0) durations.insert(c.relative,c.duration);
    QStringList cameras{"front","back","left_repeater","right_repeater"};
    for(const auto& c:clips) { if(!cameras.contains(c.camera)) cameras << c.camera; length=qMax(length,clipOffset(c)+durations.value(c.relative,60000)); }
    for(const auto& camera:cameras) {
        auto lane=new Lane; lane->camera=camera;
        auto box=new QWidget; box->setObjectName("cameraBox"); auto l=new QVBoxLayout(box); l->setContentsMargins(6,6,6,6); l->setSpacing(3);
        auto caption=new QLabel(Cam::cameraLabel(camera)); caption->setObjectName("cameraTitle"); l->addWidget(caption);
        lane->video=new QVideoWidget; lane->video->installEventFilter(this); lane->video->setMinimumSize(120,65); lane->video->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding); l->addWidget(lane->video,1);
        lane->status=new QLabel(tr("无此视角录像")); lane->status->setObjectName("cameraStatus"); lane->status->setWordWrap(true); l->addWidget(lane->status);
        lane->player=new QMediaPlayer(this); lane->player->setVideoOutput(lane->video);
        connect(lane->player,&QMediaPlayer::errorOccurred,this,[lane](QMediaPlayer::Error,const QString& message){lane->status->setText(EventPlayer::tr("无法播放")); lane->status->setToolTip(message); lane->status->setAccessibleDescription(message); lane->status->show();});
        connect(lane->player,&QMediaPlayer::durationChanged,this,[this,lane](qint64 duration){
            if(duration>0 && !lane->path.isEmpty()) { durations[lane->path]=duration; emit clipDurationKnown(lane->path,duration); updateLength(); if(lane->player->mediaStatus()==QMediaPlayer::LoadedMedia) sync(); }
        });
        connect(lane->player,&QMediaPlayer::mediaStatusChanged,this,[this,lane](QMediaPlayer::MediaStatus state){
            if(state==QMediaPlayer::LoadedMedia || state==QMediaPlayer::BufferedMedia) sync();
        });
        grid->addWidget(box,int(lanes.size())/2,int(lanes.size())%2); lanes << lane;
        if(std::any_of(clips.cbegin(),clips.cend(),[&camera](const Cam::Clip& c){return c.camera==camera;})) cameraChoice->addItem(Cam::cameraLabel(camera),camera);
    }
    slider->setRange(0,int(qMin(length,qint64(INT_MAX)))); from->setRange(0,int(length/1000)); to->setRange(0,int(length/1000)); to->setValue(int(length/1000));

    for(auto lane:lanes) connect(lane->video,&QVideoWidget::fullScreenChanged,this,[lane](bool full){if(!full) lane->video->show();});
    if(!trigger.isEmpty()) {
        auto timestamp=QDateTime::fromString(trigger,Qt::ISODate);
        if(!timestamp.isValid()) timestamp=QDateTime::fromString(trigger,"yyyy-MM-ddTHH-mm-ss");
        if(timestamp.isValid()) {
            const QDateTime local=timestamp.timeSpec()==Qt::LocalTime?timestamp:timestamp.toLocalTime();
            const qint64 offset=start.msecsTo(local);
            if(offset>=0 && offset<length) {marker->setText(tr("触发 %1").arg(clockText(offset))); marker->setToolTip(tr("哨兵事件元数据中的触发时间")); marker->setAccessibleName(tr("哨兵触发时间 %1").arg(clockText(offset))); marker->show();}
        }
    }
    sync(); refreshCoverage();
}
qint64 EventPlayer::clipOffset(const Cam::Clip& c) const { return start.msecsTo(QDateTime::fromString(c.stamp,"yyyy-MM-dd HH:mm:ss")); }
void EventPlayer::updateLength() {
    qint64 value=0;
    for(const auto& c:clips) value=qMax(value,clipOffset(c)+durations.value(c.relative,60000));
    if(value==length) { refreshCoverage(); return; }
    const bool entire=to->value()==to->maximum(); length=value;
    slider->setMaximum(int(qMin(length,qint64(INT_MAX))));
    from->setMaximum(int(length/1000)); to->setMaximum(int(length/1000)); if(entire) to->setValue(to->maximum());
    if(position>length) seek(length);
    refreshCoverage();
}
void EventPlayer::refreshCoverage() {
    QVector<SpanBar::Span> spans; const QString camera=cameraChoice->currentData().toString();
    for(const auto& clip:clips) {
        if(!camera.isEmpty() && clip.camera!=camera) continue;
        const qint64 startAt=clipOffset(clip); const bool known=durations.contains(clip.relative);
        spans.append({startAt,startAt+durations.value(clip.relative,60000),known});
    }
    static_cast<SpanBar*>(coverage)->setSpans(spans,length);
}
bool EventPlayer::eventFilter(QObject* watched,QEvent* event) {
    auto video=qobject_cast<QVideoWidget*>(watched);
    if(video && event->type()==QEvent::MouseButtonDblClick) {video->setFullScreen(!video->isFullScreen()); return true;}
    if(video && event->type()==QEvent::KeyPress) {
        auto key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Escape && video->isFullScreen()) {video->setFullScreen(false); return true;}
    }
    return QWidget::eventFilter(watched,event);
}
void EventPlayer::pause() {
    playing=false; play->setText(tr("播放")); for(auto lane:lanes) lane->player->pause();
}
void EventPlayer::seek(qint64 value) { position=qBound(qint64(0),value,length); anchor=position; elapsed.restart(); sync(); }
void EventPlayer::sync() {
    { QSignalBlocker blocker(slider); slider->setValue(int(position)); }
    bool estimated=false; for(const auto& clip:clips) if(!durations.contains(clip.relative)) estimated=true;
    time->setText(clockText(position)+" / "+(estimated?"≈":"")+clockText(length));
    for(auto lane:lanes) {
        const Cam::Clip* selected=nullptr;
        for(const auto& c:clips) if(c.camera==lane->camera && clipOffset(c)<=position) selected=&c;
        if(!selected || position>=clipOffset(*selected)+durations.value(selected->relative,60000)) {
            lane->player->pause(); lane->video->hide(); lane->status->setText(tr("无录像")); lane->status->show(); continue;
        }
        const auto& c=*selected;
        if(lane->path!=c.relative) {
            try {
                const QString path=Cam::verifiedPath(source,c);
                lane->path=c.relative; lane->status->setText(tr("载入中…")); lane->status->show(); lane->video->setToolTip(tr("%1\n双击全屏，Esc 返回").arg(QFileInfo(c.relative).fileName())); lane->video->setAccessibleName(tr("%1录像").arg(Cam::cameraLabel(c.camera))); lane->video->show();
                lane->player->setSource(QUrl::fromLocalFile(path));
            } catch(const std::exception& e) { lane->video->hide(); lane->status->setText(tr("文件不可用")); lane->status->setToolTip(QString::fromUtf8(e.what())); lane->status->setAccessibleDescription(QString::fromUtf8(e.what())); lane->status->show(); continue; }
        }
        if(lane->player->error()!=QMediaPlayer::NoError) continue;
        lane->video->show(); lane->status->hide();
        const qint64 local=position-clipOffset(c);
        if(qAbs(lane->player->position()-local)>350 || !playing) lane->player->setPosition(local);
        const double rates[]={0.5,1,2,4}; lane->player->setPlaybackRate(rates[speed->currentIndex()]);
        if(playing && lane->player->playbackState()!=QMediaPlayer::PlayingState) lane->player->play();
    }
}
QString EventPlayer::camera() const {return cameraChoice->currentData().toString();}
int EventPlayer::rangeStart() const {return from->value();}
int EventPlayer::rangeEnd() const {return to->value();}
void EventPlayer::saveScreenshot() {
    for(auto lane:lanes) if(lane->camera==camera()) {
        auto frame=lane->video->videoSink()->videoFrame().toImage();
        if(frame.isNull()) { QMessageBox::information(this,tr("截图"),tr("请先播放所选视角，等待画面出现。")); return; }
        const QString path=QFileDialog::getSaveFileName(this,tr("保存当前视角截图"),QDir::homePath()+"/TeslaCam-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+".png",tr("PNG 图片 (*.png)"));
        if(!path.isEmpty() && !frame.save(path,"PNG")) QMessageBox::warning(this,tr("截图失败"),tr("无法保存到所选位置。"));
        return;
    }
}
