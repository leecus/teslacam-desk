#include "window.h"
#include "media.h"
#include "ui.h"
#include <stdexcept>

static QString errorText(const std::exception& e) {return QString::fromUtf8(e.what());}
static void failUi(const QString& s) {throw std::runtime_error(s.toUtf8().constData());}
static QString kindText(const QString& kind) {return kind=="recent"?QCoreApplication::translate("MainWindow","近期行车"):Cam::kindLabel(kind);}
MainWindow::MainWindow(const QString& initialSource) {
    setWindowTitle(tr("特斯拉影像管家 · TeslaCam Manager")); resize(1480,900); setMinimumSize(1160,760);
    dbPath=Cam::dataDirectory()+"/index.sqlite"; db=std::make_unique<Cam::Database>(dbPath);
    buildUi();
    connect(&refreshTimer,&QTimer::timeout,this,[this]{page=0; refresh();}); refreshTimer.setSingleShot(true);
    loadSources();
    if(!initialSource.isEmpty()) {
        auto s=Cam::identifySource(initialSource);
        for(const auto& old:db->sources()) if(old.id==s.id) {s.lastScan=old.lastScan; s.status=old.status;}
        db->saveSource(s); loadSources(s.id);
    }
    refresh();
}
QString MainWindow::ffmpeg() const {
    const QString local=QCoreApplication::applicationDirPath()+"/ffmpeg.exe";
    return QFileInfo::exists(local)?local:QStandardPaths::findExecutable("ffmpeg");
}
void MainWindow::buildUi() {
    auto central=new QWidget; setCentralWidget(central); auto outer=new QVBoxLayout(central); outer->setContentsMargins(20,18,20,14); outer->setSpacing(14);
    auto header=new QHBoxLayout;
    auto title=new QLabel(tr("特斯拉影像管家")); title->setObjectName("title"); header->addWidget(title); header->addStretch();
    themeButton=new QToolButton; themeButton->setObjectName("iconButton"); themeButton->setAutoRaise(true); themeButton->setFocusPolicy(Qt::StrongFocus);
    header->addWidget(themeButton);
    connect(themeButton,&QToolButton::clicked,this,[this]{applyTheme(Theme::mode()==Theme::Dark?Theme::Light:Theme::Dark,true);});
    header->addWidget(Ui::info(tr("关于录像索引"),tr("视频在本地处理，扫描由你主动启动。索引与缩略图保存在电脑；拔出 U 盘后仍可查询已有记录。"))); outer->addLayout(header);
    auto sourceRow=new QHBoxLayout; sources=new QComboBox; sources->setMinimumWidth(260); sources->setPlaceholderText(tr("未选择设备")); sources->setAccessibleName(tr("录像设备")); sourceRow->addWidget(Ui::label(tr("设备"),sources)); sourceRow->addWidget(sources,1);
    chooseButton=new QPushButton(tr("选择 U 盘")); chooseButton->setToolTip(tr("选择 U 盘根目录、TeslaCam 目录或电脑上的录像副本")); scanButton=new QPushButton(tr("扫描建索引"));
    auto more=new QToolButton; more->setText(tr("更多")); more->setAccessibleName(tr("更多操作")); more->setPopupMode(QToolButton::InstantPopup); auto menu=new QMenu(more);
    fullScanAction=menu->addAction(tr("完整重扫")); thumbnailAction=menu->addAction(tr("生成本页预览")); menu->addSeparator(); auto openIndex=menu->addAction(tr("打开索引目录")); menu->addSeparator(); languageMenu=menu->addMenu(tr("语言")); more->setMenu(menu);
    fullScanAction->setToolTip(tr("重新检查所有录像文件并重建记录，保留收藏与有效的导出状态"));
    connect(thumbnailAction,&QAction::triggered,this,&MainWindow::createThumbnails);
    connect(openIndex,&QAction::triggered,this,[]{QDesktopServices::openUrl(QUrl::fromLocalFile(Cam::dataDirectory()));});
    // Each language is named in its own language, so these labels are never translated.
    auto addLanguage=[this](const QString& code,const QString& label) {
        auto action=languageMenu->addAction(label); action->setCheckable(true);
        action->setChecked(I18n::name(I18n::language())==code);
        connect(action,&QAction::triggered,this,[this,code]{switchLanguage(I18n::parse(code));});
        return action;
    };
    auto* languageGroup=new QActionGroup(this);
    languageGroup->addAction(addLanguage("zh",QString::fromUtf8("中文")));
    languageGroup->addAction(addLanguage("en",QStringLiteral("English")));
    sourceRow->addWidget(chooseButton); sourceRow->addWidget(scanButton); sourceRow->addWidget(more); outer->addLayout(sourceRow);
    connect(chooseButton,&QPushButton::clicked,this,&MainWindow::chooseSource);
    connect(scanButton,&QPushButton::clicked,this,[this]{startScan(false);}); connect(fullScanAction,&QAction::triggered,this,[this]{startScan(true);});
    connect(sources,&QComboBox::currentIndexChanged,this,[this](int index){
        if(index<0 || index>=knownSources.size()) return;
        current=knownSources[index]; checked.clear(); currentEvent.clear(); player->clear(); page=0; refresh();
    });
    auto splitter=new QSplitter; outer->addWidget(splitter,1);
    auto side=new QWidget; side->setObjectName("sidebar"); auto sideLayout=new QVBoxLayout(side); sideLayout->setContentsMargins(15,15,15,15); sideLayout->setSpacing(9); sideLayout->setSizeConstraint(QLayout::SetMinimumSize);
    capacity=new QLabel; capacity->setWordWrap(true); sideLayout->addWidget(capacity);
    diskBar=new QProgressBar; diskBar->setRange(0,100); diskBar->setTextVisible(false); diskBar->setFixedHeight(7); sideLayout->addWidget(diskBar);
    scanInfo=new QLabel; scanInfo->setWordWrap(true); scanInfo->setObjectName("muted"); sideLayout->addWidget(scanInfo);
    calendar=new QCalendarWidget; calendar->setMinimumWidth(225); calendar->setFixedHeight(250); sideLayout->addWidget(calendar);
    Ui::styleCalendar(calendar);
    calendar->setAccessibleName(tr("录像日期")); calendar->setToolTip(tr("高亮日期有录像，点击日期筛选"));
    auto timeHeader=new QHBoxLayout; timeEnabled=new QCheckBox(tr("时间范围")); timeHeader->addWidget(timeEnabled); timeHeader->addStretch(); timeHeader->addWidget(Ui::info(tr("日期筛选说明"),tr("高亮日期有录像。点击日期筛选当天，也可勾选时间范围后设置起止时间。"))); sideLayout->addLayout(timeHeader);
    from=new QDateTimeEdit(QDateTime(QDate::currentDate(),QTime(0,0))); to=new QDateTimeEdit(QDateTime(QDate::currentDate(),QTime(23,59,59)));
    from->setAccessibleName(tr("筛选开始时间")); to->setAccessibleName(tr("筛选结束时间"));
    for(auto edit:{from,to}) {edit->setDisplayFormat("yyyy-MM-dd HH:mm:ss"); edit->setCalendarPopup(true); Ui::styleCalendar(edit->calendarWidget());}
    auto dates=new QFormLayout; dates->setHorizontalSpacing(8); dates->addRow(Ui::label(tr("开始"),from),from); dates->addRow(Ui::label(tr("结束"),to),to); sideLayout->addLayout(dates);
    allDates=new QPushButton(tr("全部日期")); allDates->hide(); sideLayout->addWidget(allDates);
    connect(allDates,&QPushButton::clicked,this,[this]{timeEnabled->setChecked(false);});
    connect(calendar,&QCalendarWidget::clicked,this,[this](QDate date){
        {QSignalBlocker a(from),b(to); from->setDateTime(QDateTime(date,QTime(0,0))); to->setDateTime(QDateTime(date,QTime(23,59,59)));}
        timeEnabled->setChecked(true); refreshTimer.start(100);
    });
    connect(timeEnabled,&QCheckBox::toggled,this,[this](bool enabled){from->setEnabled(enabled); to->setEnabled(enabled); allDates->setVisible(enabled); refreshTimer.start(100);}); from->setEnabled(false); to->setEnabled(false);
    connect(from,&QDateTimeEdit::dateTimeChanged,this,[this]{if(timeEnabled->isChecked()) refreshTimer.start(250);});
    connect(to,&QDateTimeEdit::dateTimeChanged,this,[this]{if(timeEnabled->isChecked()) refreshTimer.start(250);});
    sideLayout->addSpacing(10);
    kind=new QComboBox; kind->addItem(tr("全部录像"),""); kind->addItem(tr("哨兵录像"),"sentry"); kind->addItem(tr("手动保存"),"saved"); kind->addItem(tr("近期行车"),"recent"); kind->addItem(tr("其他录像"),"other"); kind->setAccessibleName(tr("录像类型"));
    state=new QComboBox; state->addItems({tr("全部状态"),tr("已收藏"),tr("已导出原片"),tr("未导出原片")}); state->setAccessibleName(tr("录像状态")); state->setToolTip(tr("收藏事件受删除保护；已导出表示完整原片已备份并校验。"));
    order=new QComboBox; order->addItems({tr("最新在前"),tr("最早在前")}); order->setAccessibleName(tr("时间排序"));
    auto filters=new QFormLayout; filters->setHorizontalSpacing(8); filters->addRow(Ui::label(tr("类型"),kind),kind); filters->addRow(Ui::label(tr("状态"),state),state); filters->addRow(Ui::label(tr("排序"),order),order); sideLayout->addLayout(filters);
    for(auto combo:{kind,state,order}) connect(combo,&QComboBox::currentIndexChanged,this,[this]{refreshTimer.start(80);});
    sideLayout->addStretch();
    auto sideScroll=new QScrollArea; sideScroll->setWidget(side); sideScroll->setWidgetResizable(true); sideScroll->setFrameShape(QFrame::NoFrame); sideScroll->setMinimumWidth(280); sideScroll->setMaximumWidth(310); sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); splitter->addWidget(sideScroll);
    auto middle=new QWidget; auto mid=new QVBoxLayout(middle); mid->setContentsMargins(12,0,12,0); mid->setSpacing(10);
    search=new QLineEdit; search->setPlaceholderText(tr("搜索日期或事件")); search->setAccessibleName(tr("搜索日期或事件目录")); search->setClearButtonEnabled(true); mid->addWidget(search); connect(search,&QLineEdit::textChanged,this,[this]{refreshTimer.start(250);});
    summary=new QLabel; summary->setObjectName("section"); mid->addWidget(summary);
    table=new QTableWidget; table->setColumnCount(4); table->setHorizontalHeaderLabels({tr("选择"),"",tr("时间 / 类型"),tr("大小")}); table->setAccessibleName(tr("录像事件列表")); table->verticalHeader()->hide(); table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection); table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setShowGrid(false); table->setAlternatingRowColors(true); table->setIconSize(QSize(100,56)); table->setColumnWidth(0,44); table->setColumnWidth(1,106); table->setColumnWidth(3,75); table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch); eventStack=new QStackedWidget; listEmpty=new QLabel(tr("暂无录像")); listEmpty->setAlignment(Qt::AlignCenter); listEmpty->setObjectName("emptyState"); eventStack->addWidget(listEmpty); eventStack->addWidget(table); mid->addWidget(eventStack,1);
    connect(table,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){if(!filling) loadEvent(row);});
    connect(table,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){
        if(filling || item->column()!=0) return;
        const QString key=item->data(Qt::UserRole).toString(); if(item->checkState()==Qt::Checked) checked.insert(key); else checked.remove(key); updateSelection();
    });
    listTools=new QWidget; auto pagination=new QHBoxLayout(listTools); pagination->setContentsMargins(0,0,0,0); auto selectPage=new QPushButton(tr("勾选本页")); auto selectAll=new QPushButton(tr("勾选全部")); auto clearSelection=new QPushButton(tr("清空勾选")); previous=new QPushButton(tr("上一页")); next=new QPushButton(tr("下一页")); pageInfo=new QLabel;
    selectAll->setToolTip(tr("勾选当前筛选结果中的全部事件，含其他页")); pagination->addWidget(selectPage); pagination->addWidget(selectAll); pagination->addWidget(clearSelection); pagination->addStretch(); pagination->addWidget(previous); pagination->addWidget(pageInfo); pagination->addWidget(next); mid->addWidget(listTools);
    connect(selectPage,&QPushButton::clicked,this,[this]{for(int i=0;i<table->rowCount();++i) table->item(i,0)->setCheckState(Qt::Checked);});
    connect(selectAll,&QPushButton::clicked,this,[this]{for(const auto& event:eventList) checked.insert(event.key); fillTable();});
    connect(clearSelection,&QPushButton::clicked,this,[this]{checked.clear(); fillTable();});
    connect(previous,&QPushButton::clicked,this,[this]{if(page>0){--page; fillTable();}}); connect(next,&QPushButton::clicked,this,[this]{++page; fillTable();});
    selectionInfo=new QLabel; selectionInfo->setWordWrap(true); mid->addWidget(selectionInfo);
    auto batch=new QHBoxLayout; originalButton=new QPushButton(tr("导出原片")); originalButton->setObjectName("primary"); originalButton->setToolTip(tr("导出所选事件的全部摄像头原片及事件信息，并校验备份")); backupButton=new QPushButton(tr("备份后删除")); deleteButton=new QPushButton(tr("永久删除")); deleteButton->setObjectName("danger"); batch->addWidget(originalButton); batch->addWidget(backupButton); batch->addWidget(deleteButton); mid->addLayout(batch);
    connect(originalButton,&QPushButton::clicked,this,&MainWindow::exportOriginals); connect(backupButton,&QPushButton::clicked,this,[this]{clean(true);}); connect(deleteButton,&QPushButton::clicked,this,[this]{clean(false);});
    splitter->addWidget(middle);
    auto right=new QWidget; auto rl=new QVBoxLayout(right); rl->setContentsMargins(0,0,0,0); rl->setSpacing(10);
    auto eventHeader=new QHBoxLayout; eventInfo=new QLabel(tr("预览")); eventInfo->setWordWrap(true); eventInfo->setObjectName("section"); eventHeader->addWidget(eventInfo,1); favoriteButton=new QPushButton(tr("☆ 收藏")); eventHeader->addWidget(favoriteButton); rl->addLayout(eventHeader);
    player=new EventPlayer; rl->addWidget(player,1);
    connect(player,&EventPlayer::clipDurationKnown,this,[this](const QString& relative,qint64 duration){if(!current.id.isEmpty() && duration>0) db->setClipDuration(current.id,relative,duration);});
    auto utility=new QHBoxLayout; revealButton=new QPushButton(tr("打开文件位置")); utility->addWidget(revealButton); utility->addStretch(); rl->addLayout(utility);
    connect(player,&EventPlayer::rangeExportRequested,this,&MainWindow::exportRange);
    connect(revealButton,&QPushButton::clicked,this,[this]{
        try {auto clips=db->clips(current.id,currentEvent); if(!clips.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(Cam::verifiedPath(current,clips.first())).absolutePath()));}
        catch(const std::exception& e){QMessageBox::warning(this,tr("无法打开"),errorText(e));}
    });
    connect(favoriteButton,&QPushButton::clicked,this,[this]{
        if(currentEvent.isEmpty() || worker) return;
        for(const auto& e:eventList) if(e.key==currentEvent){db->setFavorite(current.id,e.key,!e.favorite); break;} refresh();
    });
    splitter->addWidget(right);
    splitter->setStretchFactor(0,0); splitter->setStretchFactor(1,5); splitter->setStretchFactor(2,6); splitter->setSizes({270,550,610});
    auto bottom=new QHBoxLayout; status=new QLabel; status->setWordWrap(true); bottom->addWidget(status,1); progress=new QProgressBar; progress->setAccessibleName("当前任务进度"); progress->setMinimumWidth(190); progress->setMaximumWidth(260); progress->setValue(0); bottom->addWidget(progress); cancelButton=new QPushButton(tr("取消")); cancelButton->setEnabled(false); cancelButton->hide(); progress->hide(); bottom->addWidget(cancelButton); outer->addLayout(bottom);
    connect(cancelButton,&QPushButton::clicked,this,[this]{canceled=true; cancelButton->setEnabled(false); status->setText(tr("正在停止，已完成的索引或文件操作会保留…"));});
    operationControls={sources,chooseButton,scanButton,originalButton,backupButton,deleteButton,favoriteButton};
    updateThemeButton();
    sources->setFocus();
}

void MainWindow::applyTheme(Theme::Mode mode, bool remember) {
    Theme::apply(mode);
    Ui::styleCalendar(calendar);
    for(auto edit:{from,to}) Ui::styleCalendar(edit->calendarWidget());
    if(remember) Theme::save(mode);
    updateThemeButton();
    refresh();
}
void MainWindow::updateThemeButton() {
    const bool dark=Theme::mode()==Theme::Dark;
    themeButton->setText(dark?QString("☀"):QString("☾"));
    themeButton->setToolTip(dark?tr("切换到浅色外观"):tr("切换到深色外观"));
    themeButton->setAccessibleName(tr("外观"));
    themeButton->setAccessibleDescription(dark?tr("当前为深色，点击切换为浅色"):tr("当前为浅色，点击切换为深色"));
}

// The UI is built in code, so retranslating means rebuilding it and putting the
// browsing state back. Running jobs are excluded because their worker reports into
// the widgets they were started from.
void MainWindow::switchLanguage(I18n::Language language) {
    if(worker || language==I18n::language()) return;
    I18n::save(language);
    // Rebuild on the next event loop pass: the menu action that got us here lives in
    // the widget tree that is about to be replaced.
    QTimer::singleShot(0,this,[this,language]{rebuildForLanguage(language);});
}
void MainWindow::rebuildForLanguage(I18n::Language language) {
    if(worker || language==I18n::language()) return;
    const QString sourceId=current.id, event=currentEvent, query=search->text();
    const int kindIndex=kind->currentIndex(), stateIndex=state->currentIndex(), orderIndex=order->currentIndex();
    const bool timeOn=timeEnabled->isChecked();
    const QDateTime fromValue=from->dateTime(), toValue=to->dateTime();
    const QSet<QString> selection=checked; const int currentPage=page;
    player->clear();
    I18n::install(language);
    if(auto* previous=takeCentralWidget()) previous->deleteLater();
    buildUi();
    loadSources(sourceId);
    {
        QSignalBlocker a(kind),b(state),c(order),d(search),e(timeEnabled),f(from),g(to);
        kind->setCurrentIndex(kindIndex); state->setCurrentIndex(stateIndex); order->setCurrentIndex(orderIndex);
        search->setText(query); timeEnabled->setChecked(timeOn); from->setDateTime(fromValue); to->setDateTime(toValue);
    }
    from->setEnabled(timeOn); to->setEnabled(timeOn); allDates->setVisible(timeOn);
    checked=selection; currentEvent=event; page=currentPage;
    refresh();
}

void MainWindow::loadSources(const QString& preferred) {
    QSignalBlocker blocked(sources); knownSources=db->sources(); sources->clear();
    for(const auto& s:knownSources) sources->addItem(s.name,s.id);
    int index=0;
    for(int i=0;i<knownSources.size();++i) if(knownSources[i].id==(preferred.isEmpty()?current.id:preferred)) index=i;
    sources->setCurrentIndex(knownSources.isEmpty()?-1:index);
    if(!knownSources.isEmpty()) current=knownSources[index];
}
void MainWindow::chooseSource() {
    QString initial=current.root;
    if(initial.isEmpty() || !QFileInfo::exists(initial)) for(const auto& volume:QStorageInfo::mountedVolumes()) if(QDir(volume.rootPath()).exists("TeslaCam")) {initial=volume.rootPath(); break;}
    const QString path=QFileDialog::getExistingDirectory(this,tr("选择 U 盘根目录或 TeslaCam 录像目录"),initial);
    if(path.isEmpty()) return;
    try {
        auto s=Cam::identifySource(path);
        for(const auto& old:db->sources()) if(old.id==s.id) {s.lastScan=old.lastScan; s.status=old.status;}
        db->saveSource(s); checked.clear(); currentEvent.clear(); player->clear(); loadSources(s.id); page=0; refresh();
        status->clear();
    } catch(const std::exception& e) {QMessageBox::warning(this,tr("无法选择目录"),errorText(e));}
}
void MainWindow::refresh() {
    const bool hasSource=!current.id.isEmpty();
    capacity->setVisible(hasSource); diskBar->setVisible(hasSource); scanInfo->setVisible(hasSource);
    summary->setVisible(hasSource);
    if(!hasSource) {listEmpty->setText(tr("暂无录像")); setBusy(worker!=nullptr); return;}
    for(const auto& s:db->sources()) if(s.id==current.id) current=s;
    bool online=false;
    try {online=Cam::identifySource(current.root).id==current.id;} catch(...) {}
    QStorageInfo storage(current.root);
    if(online && storage.isValid() && storage.bytesTotal()>0) {
        capacity->setText(tr("可用 %1 / %2").arg(Cam::humanBytes(storage.bytesAvailable()),Cam::humanBytes(storage.bytesTotal())));
        diskBar->setValue(int(100.0*(storage.bytesTotal()-storage.bytesAvailable())/storage.bytesTotal()));
    } else {capacity->setText(tr("设备离线")); diskBar->setValue(0);}
    capacity->setToolTip(QDir::toNativeSeparators(current.root));
    capacity->setAccessibleName(tr("设备存储空间")); diskBar->setAccessibleName(tr("设备已用空间百分比"));
    scanInfo->setText(current.lastScan.isEmpty()?tr("未扫描"):tr("上次扫描 %1").arg(current.lastScan));
    scanInfo->setToolTip(QCoreApplication::translate("Cam",qUtf8Printable(current.status))+(online?QString():"\n"+tr("重新选择目录可匹配盘符变化；离线仅可查看索引与缩略图。")));
    scanButton->setText(current.lastScan.isEmpty()?tr("扫描建索引"):tr("更新索引"));
    const QString begin=timeEnabled->isChecked()?from->dateTime().toString("yyyy-MM-dd HH:mm:ss"):QString();
    const QString end=timeEnabled->isChecked()?to->dateTime().toString("yyyy-MM-dd HH:mm:ss"):QString();
    eventList=db->events(current.id,kind->currentData().toString(),begin,end,state->currentIndex(),search->text(),order->currentIndex()==1);
    qint64 bytes=0; int files=0; QSet<QString> available;
    for(const auto& e:eventList) {bytes+=e.bytes; files+=e.files; available.insert(e.key);}
    // Do not retain hidden selections after a filter change.
    checked.intersect(available);
    summary->setText(tr("%1 条事件 · %2").arg(eventList.size()).arg(Cam::humanBytes(bytes)));
    summary->setToolTip(tr("%1 个视频文件").arg(files));
    calendar->setDateTextFormat(QDate(),QTextCharFormat());
    auto q=db->query("SELECT DISTINCT substr(stamp,1,10) FROM clips WHERE source=?",{current.id});
    QTextCharFormat format; format.setForeground(Theme::color("highlightText")); format.setBackground(Theme::color("highlightBg")); format.setFontWeight(QFont::Bold);
    while(q.next()) calendar->setDateTextFormat(QDate::fromString(q.value(0).toString(),Qt::ISODate),format);
    fillTable(); setBusy(worker!=nullptr);
}
void MainWindow::fillTable() {
    filling=true; const int pageSize=100; page=qBound(0,page,qMax(0,(int(eventList.size())-1)/pageSize));
    const int first=page*pageSize, count=qMin(pageSize,int(eventList.size())-first); table->setRowCount(qMax(0,count));
    int selectedRow=-1;
    for(int row=0;row<count;++row) {
        const auto& e=eventList[first+row]; auto check=new QTableWidgetItem;
        check->setFlags(Qt::ItemIsEnabled|Qt::ItemIsUserCheckable|Qt::ItemIsSelectable); check->setCheckState(checked.contains(e.key)?Qt::Checked:Qt::Unchecked); check->setData(Qt::UserRole,e.key); check->setData(Qt::AccessibleTextRole,tr("选择事件 %1").arg(e.start)); table->setItem(row,0,check);
        auto preview=new QTableWidgetItem; const auto clips=db->clips(current.id,e.key); const auto target=Cam::previewTarget(clips,e.trigger);
        preview->setData(Qt::AccessibleTextRole,tr("录像缩略图"));
        if(!target.clip.relative.isEmpty()) {const QString thumb=Cam::thumbnailPath(current,target.clip,target.offsetMs); if(QFileInfo::exists(thumb)) preview->setIcon(QIcon(thumb)); else {preview->setIcon(style()->standardIcon(QStyle::SP_FileIcon)); preview->setToolTip(tr("尚无缩略图，翻到本页后会自动生成"));}}
        table->setItem(row,1,preview);
        const QString reason=Cam::reasonLabel(e.reason);
        QString detail=(e.favorite?"★ ":"")+e.start+"\n"+tr("%1 · %2 视角 · %3 段").arg(kindText(e.kind)).arg(e.cameras).arg(e.files);
        if(e.exported) detail+=" · "+tr("已导出");
        if(!reason.isEmpty()) detail+=" · "+reason;
        auto text=new QTableWidgetItem(detail); text->setData(Qt::AccessibleTextRole,(e.favorite?tr("已收藏，删除保护，"):QString())+detail);
        QString tip=e.key+"\n"+tr("末段起始：%1").arg(e.end); if(!e.trigger.isEmpty()) tip+="\n"+tr("触发 %1").arg(e.trigger); if(!e.city.isEmpty()) tip+="\n"+e.city; if(e.kind=="recent") tip+="\n"+tr("近期行车按固定十分钟分组，属于推断分组。"); text->setToolTip(tip); table->setItem(row,2,text); table->setItem(row,3,new QTableWidgetItem(Cam::humanBytes(e.bytes))); table->setRowHeight(row,72);
        if(e.key==currentEvent) selectedRow=row;
    }
    pageInfo->setText(QString("%1 / %2").arg(page+1).arg(qMax(1,(int(eventList.size())+pageSize-1)/pageSize))); previous->setEnabled(page>0); next->setEnabled(first+count<eventList.size());
    if(selectedRow>=0) table->selectRow(selectedRow);
    filling=false;
    if(selectedRow<0 && !currentEvent.isEmpty()) {currentEvent.clear(); player->clear(); eventInfo->setText(tr("预览"));}
    else if(selectedRow>=0) for(const auto& e:eventList) if(e.key==currentEvent) favoriteButton->setText(e.favorite?tr("★ 已收藏"):tr("☆ 收藏"));
    eventStack->setCurrentWidget(eventList.isEmpty()?static_cast<QWidget*>(listEmpty):table);
    if(eventList.isEmpty()) listEmpty->setText(current.lastScan.isEmpty()?tr("尚未建立索引"):tr("没有匹配的录像"));
    previous->setVisible(eventList.size()>pageSize); next->setVisible(eventList.size()>pageSize); pageInfo->setVisible(eventList.size()>pageSize);
    updateSelection(); scheduleThumbnails();
}
void MainWindow::scheduleThumbnails() {
    if(worker || current.id.isEmpty() || ffmpeg().isEmpty()) return;
    bool online=false; try { online=Cam::identifySource(current.root).id==current.id; } catch(...) {}
    if(!online) return;
    const int first=page*100, last=qMin(first+100,int(eventList.size())); QStringList missing;
    for(int i=first;i<last;++i) {
        const auto target=Cam::previewTarget(db->clips(current.id,eventList[i].key),eventList[i].trigger);
        if(!target.clip.relative.isEmpty() && !QFileInfo::exists(Cam::thumbnailPath(current,target.clip,target.offsetMs))) missing << eventList[i].key;
    }
    if(missing.isEmpty()) return;
    const QString key=current.id+"#"+QString::number(page)+"#"+missing.join(",");
    if(key==autoThumbKey) return;
    autoThumbKey=key; createThumbnails();
}
void MainWindow::loadEvent(int row) {
    if(row<0 || !table->item(row,0)) return;
    currentEvent=table->item(row,0)->data(Qt::UserRole).toString();
    const auto clips=db->clips(current.id,currentEvent); QString trigger; QString infoTip;
    for(const auto& e:eventList) if(e.key==currentEvent) {
        trigger=e.trigger; eventInfo->setText(e.start); favoriteButton->setText(e.favorite?tr("★ 已收藏"):tr("☆ 收藏"));
        QStringList lines{kindText(e.kind)+" · "+Cam::humanBytes(e.bytes)}; if(!e.reason.isEmpty()) lines << Cam::reasonLabel(e.reason); if(!e.city.isEmpty()) lines << e.city; if(!e.trigger.isEmpty()) lines << tr("触发 %1").arg(e.trigger); infoTip=lines.join("\n"); break;
    }
    if(trigger.isEmpty() && !clips.isEmpty()) {
        try {
            const QString folder=QFileInfo(Cam::verifiedPath(current,clips.first())).absolutePath();
            QFile metadata(folder+"/event.json");
            if(metadata.size()<1024*1024 && metadata.open(QIODevice::ReadOnly)) trigger=QJsonDocument::fromJson(metadata.readAll()).object().value("timestamp").toString();
        } catch(...) {}
    }
    eventInfo->setToolTip(infoTip); player->load(current,clips,trigger); updateSelection();
}
QStringList MainWindow::selectedKeys() const {
    if(!checked.isEmpty()) {QStringList keys=checked.values(); keys.sort(); return keys;}
    return currentEvent.isEmpty()?QStringList():QStringList{currentEvent};
}
void MainWindow::updateSelection() {
    const auto keys=selectedKeys(); qint64 bytes=0; int protectedCount=0;
    for(const auto& e:eventList) if(keys.contains(e.key)) {bytes+=e.bytes; if(e.favorite) ++protectedCount;}
    selectionInfo->setVisible(!checked.isEmpty());
    selectionInfo->setText(tr("已选 %1 条 · %2").arg(keys.size()).arg(Cam::humanBytes(bytes))+(protectedCount>0?tr(" · %1 条受保护").arg(protectedCount):QString()));
    originalButton->setEnabled(!worker && !keys.isEmpty());
    backupButton->setEnabled(!worker && keys.size()>protectedCount); deleteButton->setEnabled(backupButton->isEnabled());
    favoriteButton->setEnabled(!worker && !currentEvent.isEmpty());
    favoriteButton->setVisible(!currentEvent.isEmpty());
    favoriteButton->setAccessibleDescription(tr("收藏的事件受到删除保护")); favoriteButton->setToolTip(tr("收藏的事件受到删除保护"));
    deleteButton->setToolTip(protectedCount==keys.size() && !keys.isEmpty()?tr("收藏事件受保护，需先取消收藏"):tr("永久删除所选事件的全部摄像头录像，不进入回收站，无法撤销"));
    backupButton->setToolTip(tr("完整备份并校验成功后，永久删除所选事件的录像"));
    revealButton->setVisible(!currentEvent.isEmpty());
}
void MainWindow::setBusy(bool value) {
    for(auto control:operationControls) control->setEnabled(!value);
    languageMenu->setEnabled(!value);
    scanButton->setEnabled(!value && !current.id.isEmpty()); fullScanAction->setEnabled(scanButton->isEnabled()); thumbnailAction->setEnabled(!value && !eventList.isEmpty()); cancelButton->setEnabled(value && !canceled);
    cancelButton->setVisible(value); progress->setVisible(value);
    Ui::primary(chooseButton,current.id.isEmpty()); Ui::primary(scanButton,!current.id.isEmpty() && current.lastScan.isEmpty());
    const auto keys=selectedKeys(); bool deletable=false;
    for(const auto& event:eventList) if(keys.contains(event.key) && !event.favorite) {deletable=true; break;}
    originalButton->setEnabled(!value && !keys.isEmpty()); backupButton->setEnabled(!value && deletable); deleteButton->setEnabled(backupButton->isEnabled()); favoriteButton->setEnabled(!value && !currentEvent.isEmpty());
    revealButton->setVisible(!currentEvent.isEmpty()); selectionInfo->setVisible(!checked.isEmpty());
    listTools->setVisible(!eventList.isEmpty());
    for(auto button:{originalButton,backupButton,deleteButton}) button->setVisible(!eventList.isEmpty());
    favoriteButton->setVisible(!currentEvent.isEmpty());
    const bool browse=(!value || allowBrowse) && !current.id.isEmpty();
    table->setEnabled(browse); player->setEnabled(browse); kind->setEnabled(browse); state->setEnabled(browse); order->setEnabled(browse); search->setEnabled(browse); calendar->setEnabled(browse); timeEnabled->setEnabled(browse);
    from->setEnabled(browse && timeEnabled->isChecked()); to->setEnabled(browse && timeEnabled->isChecked());
}
void MainWindow::run(const JobSpec& spec,std::function<Cam::Result(std::atomic_bool&,const Cam::Progress&)> work) {
    if(worker) return;
    canceled=false; allowBrowse=spec.browseDuring; thumbnailJob=spec.thumbnailJob; jobElapsed.restart(); setBusy(true); status->setText(spec.title); progress->setRange(0,0);
    worker=QThread::create([this,spec,work=std::move(work)] {
        Cam::Result result; QString error;
        try {
            result=work(canceled,[this,spec](int done,int total,const QString& message){
                QMetaObject::invokeMethod(this,[this,spec,done,total,message]{
                    if(total>0){progress->setRange(0,total); progress->setValue(done);} else progress->setRange(0,0);
                    const qint64 seconds=jobElapsed.elapsed()/1000;
                    QString timing=tr(" · 已用 %1 秒").arg(seconds);
                    if(total>0 && done>0) timing+=tr(" · 预计剩余 %1 秒").arg(qMax(qint64(0),seconds*(total-done)/done));
                    status->setText(message.left(130)+timing);
                    if(spec.refreshDuring) refreshTimer.start(400);
                },Qt::QueuedConnection);
            });
        } catch(const std::exception& e){error=errorText(e);}
        QMetaObject::invokeMethod(this,[this,spec,result,error]{
            worker=nullptr; progress->setRange(0,100); progress->setValue(error.isEmpty()&&!result.canceled?100:0); loadSources(current.id); refresh();
            report(spec.title,result,error);
            if(closing) {close(); return;}
            if(spec.thumbsAfter && error.isEmpty() && !result.canceled) QTimer::singleShot(150,this,&MainWindow::createThumbnails);
        },Qt::QueuedConnection);
    });
    connect(worker,&QThread::finished,worker,&QObject::deleteLater); worker->start();
}
void MainWindow::report(const QString& title,const Cam::Result& r,const QString& error) {
    QFile log(Cam::dataDirectory()+"/operations.log");
    if(log.open(QIODevice::Append|QIODevice::Text)) log.write((QDateTime::currentDateTime().toString(Qt::ISODate)+" "+title+" "+QString::number(r.files)+" files\n"+error+"\n"+r.errors.join('\n')+"\n"+r.destination+"\n").toUtf8());
    if(!error.isEmpty()) {status->setText(tr("%1 失败").arg(title)); status->setToolTip(error); if(!closing) QMessageBox::warning(this,tr("%1 失败").arg(title),error); return;}
    status->setToolTip(tr("%1 个视频 · %2 条事件 · %3\n新增/更新 %4 · 移除失效 %5\n%6").arg(r.files).arg(r.events).arg(Cam::humanBytes(r.bytes)).arg(r.changed).arg(r.removed).arg(r.errors.join('\n')));
    status->setText((r.canceled?tr("%1已取消"):tr("%1完成")).arg(title)+(r.errors.isEmpty()?QString():tr(" · %1 项提示").arg(r.errors.size())));
    const QString message=status->text();
    QTimer::singleShot(8000,this,[this,message]{if(!worker && status->text()==message) status->clear();});
    if((!r.destination.isEmpty() || !r.errors.isEmpty()) && !closing && !thumbnailJob) {
        QMessageBox box(QMessageBox::Information,title,status->text()+(r.destination.isEmpty()?QString():"\n"+tr("保存位置：%1").arg(QDir::toNativeSeparators(r.destination))),QMessageBox::Ok,this);
        if(!r.errors.isEmpty()) box.setDetailedText(r.errors.join('\n')); box.exec();
    }
}
void MainWindow::startScan(bool rebuild) {
    if(worker || current.id.isEmpty()) return;
    try {if(Cam::identifySource(current.root).id!=current.id) failUi(tr("设备身份变化，请重新选择 U 盘。"));}
    catch(const std::exception& e){QMessageBox::warning(this,tr("无法扫描"),errorText(e)); return;}
    player->clear(); currentEvent.clear(); checked.clear(); const auto s=current; const auto path=dbPath;
    run({rebuild?tr("完整扫描"):tr("更新索引"),true,true,false,true},[path,s,rebuild](auto& cancel,const auto& progress){return Cam::scan(path,s,rebuild,cancel,progress);});
}
void MainWindow::exportOriginals() {
    if(worker) return; const auto keys=selectedKeys(); if(keys.isEmpty()) {QMessageBox::information(this,tr("导出原片"),tr("请先选中或勾选事件。")); return;}
    const QString destination=QFileDialog::getExistingDirectory(this,tr("选择原始录像的备份目录")); if(destination.isEmpty()) return;
    player->pause(); const auto source=current; const auto path=dbPath;
    run({tr("导出原片")},[path,source,keys,destination](auto& cancel,const auto& progress){return Cam::exportOriginals(path,source,keys,destination,cancel,progress);});
}
void MainWindow::clean(bool backupFirst) {
    if(worker) return; auto keys=selectedKeys(); qint64 bytes=0; int protectedCount=0;
    for(const auto& e:eventList) if(keys.contains(e.key)) {
        if(e.favorite) {keys.removeAll(e.key); ++protectedCount;} else bytes+=e.bytes;
    }
    if(keys.isEmpty()) {QMessageBox::information(this,tr("清理录像"),tr("请选中需要清理的事件。收藏事件受到保护，需先取消收藏。")); return;}
    QString destination;
    if(backupFirst) {
        destination=QFileDialog::getExistingDirectory(this,tr("选择电脑或其他磁盘上的备份目录")); if(destination.isEmpty()) return;
        QStorageInfo sourceDisk(current.root),targetDisk(destination);
        if(sourceDisk.device()==targetDisk.device()) {QMessageBox::warning(this,tr("请选择其他磁盘"),tr("备份后删除需要把备份保存到另一个磁盘，才能释放当前设备的空间。")); return;}
    }
    QMessageBox confirm(QMessageBox::Warning,backupFirst?tr("确认备份后删除"):tr("确认永久删除"),
        tr("将%1 %2 条事件的所有摄像头录像，预计释放 %3。\n已排除 %4 条收藏事件。\n\n%5\n不勾选时保留事件目录里的 json、jpg、png。不会删除 RecentClips。").arg(backupFirst?tr("备份并校验，然后永久删除"):tr("永久删除")).arg(keys.size()).arg(Cam::humanBytes(bytes)).arg(protectedCount).arg(backupFirst?tr("只有全部原片与事件信息成功备份并校验后，才会开始删除。"):tr("此操作不进入回收站，无法撤销。")),QMessageBox::Cancel,this);
    auto leftovers=new QCheckBox(tr("同时删除事件目录中的 json、jpg、png，并移除因此变空的目录")); confirm.setCheckBox(leftovers);
    auto proceed=confirm.addButton(backupFirst?tr("备份成功后永久删除"):tr("永久删除这些录像"),QMessageBox::DestructiveRole); confirm.setDefaultButton(QMessageBox::Cancel); confirm.exec(); if(confirm.clickedButton()!=proceed) return;
    player->clear(); currentEvent.clear(); const auto source=current; const auto path=dbPath; const bool removeLeftovers=leftovers->isChecked();
    run({backupFirst?tr("备份并清理"):tr("清理录像"),false,false},[path,source,keys,destination,backupFirst,removeLeftovers](auto& cancel,const auto& progress){
        QString saved;
        if(backupFirst) {
            auto backup=Cam::exportOriginals(path,source,keys,destination,cancel,progress); saved=backup.destination;
            if(cancel || !backup.errors.isEmpty() || backup.events!=keys.size()) {backup.errors.prepend(tr("备份未全部完成，未执行删除。")); return backup;}
        }
        auto result=Cam::deleteEvents(path,source,keys,cancel,progress,removeLeftovers); result.destination=saved; return result;
    });
}
struct ThumbJob { Cam::Clip clip; int offsetMs=0; };
void MainWindow::createThumbnails() {
    if(worker || current.id.isEmpty() || ffmpeg().isEmpty()) return;
    QList<ThumbJob> pending; const int first=page*100, last=qMin(first+100,int(eventList.size()));
    for(int i=first;i<last;++i) {
        const auto target=Cam::previewTarget(db->clips(current.id,eventList[i].key),eventList[i].trigger);
        if(target.clip.relative.isEmpty() || QFileInfo::exists(Cam::thumbnailPath(current,target.clip,target.offsetMs))) continue;
        pending.append(ThumbJob{target.clip,target.offsetMs});
    }
    if(pending.isEmpty()) return;
    const auto source=current; const auto executable=ffmpeg(); const auto path=dbPath;
    run({tr("生成本页缩略图"),false,true,true},[source,pending,executable,path](auto& cancel,const auto& progress){
        Cam::Result result; Cam::Database database(path);
        for(const auto& job:pending) {
            if(cancel) break;
            try {
                const QString input=Cam::verifiedPath(source,job.clip), temporary=Cam::thumbnailPath(source,job.clip,job.offsetMs)+".tmp.jpg"; QString log;
                auto grab=[&](int offset){
                    QStringList args{"-hide_banner","-y"}; if(offset>0) args << "-ss" << QString::number(offset/1000.0,'f',3);
                    args << "-i" << input << "-frames:v" << "1" << "-vf" << "scale=240:-2" << temporary; log.clear();
                    return Cam::runFfmpeg(executable,args,cancel,&log,20);
                };
                bool ok=grab(job.offsetMs);
                if(!ok && job.offsetMs>0 && !cancel) { QFile::remove(temporary); ok=grab(0); }
                const qint64 duration=Cam::ffmpegDuration(log);
                if(duration>0) database.setClipDuration(source.id,job.clip.relative,duration);
                const QString target=Cam::thumbnailPath(source,job.clip,job.offsetMs);
                if(ok && !cancel && QFile::rename(temporary,target)) ++result.files;
                else { QFile::remove(temporary); if(!cancel) result.errors << tr("%1：%2").arg(job.clip.relative,log.right(400)); }
            } catch(const std::exception& e){result.errors << errorText(e);}
            if(progress) progress(result.files+int(result.errors.size()),int(pending.size()),tr("生成缩略图 · %1").arg(job.clip.relative));
        }
        result.canceled=cancel; return result;
    });
}
void MainWindow::exportRange() {
    if(worker || currentEvent.isEmpty()) return;
    const auto executable=ffmpeg(); if(executable.isEmpty()){QMessageBox::warning(this,tr("缺少视频工具"),tr("未找到 ffmpeg.exe，请放到程序旁边再重试。")); return;}
    const int begin=player->rangeStart(),end=player->rangeEnd(); if(end<=begin){QMessageBox::warning(this,tr("时间范围无效"),tr("终点必须晚于起点。")); return;}
    const auto clips=db->clips(current.id,currentEvent); const QString camera=player->camera();
    const QString destination=QFileDialog::getExistingDirectory(this,tr("选择裁剪视频的保存目录")); if(destination.isEmpty()) return;
    const auto source=current; player->pause();
    run({tr("导出观看版")},[source,clips,camera,begin,end,destination,executable](auto& cancel,const auto& progress){
        return Cam::exportRange(source,clips,camera,begin,end,destination,executable,cancel,progress);
    });
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if(worker) {closing=true; canceled=true; status->setText(tr("正在停止后台任务，完成后关闭…")); event->ignore(); return;}
    player->clear(); event->accept();
}
