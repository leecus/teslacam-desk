#include "window.h"
#include "media.h"
#include "ui.h"
#include <stdexcept>

static QString errorText(const std::exception& e) {return QString::fromUtf8(e.what());}
static void failUi(const QString& s) {throw std::runtime_error(s.toUtf8().constData());}
static QString kindText(const QString& kind) {return kind=="recent"?QString("近期行车"):Cam::kindLabel(kind);}
MainWindow::MainWindow(const QString& initialSource) {
    setWindowTitle("特斯拉影像管家 · TeslaCam Manager"); resize(1480,900); setMinimumSize(1160,760);
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
    auto title=new QLabel("特斯拉影像管家"); title->setObjectName("title"); header->addWidget(title); header->addStretch();
    header->addWidget(Ui::info("关于录像索引","视频在本地处理，扫描由你主动启动。索引与缩略图保存在电脑；拔出 U 盘后仍可查询已有记录。")); outer->addLayout(header);
    auto sourceRow=new QHBoxLayout; sources=new QComboBox; sources->setMinimumWidth(260); sources->setPlaceholderText("未选择设备"); sources->setAccessibleName("录像设备"); sourceRow->addWidget(Ui::label("设备",sources)); sourceRow->addWidget(sources,1);
    chooseButton=new QPushButton("选择 U 盘"); chooseButton->setToolTip("选择 U 盘根目录、TeslaCam 目录或电脑上的录像副本"); scanButton=new QPushButton("扫描建索引");
    auto more=new QToolButton; more->setText("更多"); more->setAccessibleName("更多操作"); more->setPopupMode(QToolButton::InstantPopup); auto menu=new QMenu(more);
    fullScanAction=menu->addAction("完整重扫"); thumbnailAction=menu->addAction("生成本页预览"); menu->addSeparator(); auto openIndex=menu->addAction("打开索引目录"); more->setMenu(menu);
    fullScanAction->setToolTip("重新检查所有录像文件并重建记录，保留收藏与有效的导出状态");
    connect(thumbnailAction,&QAction::triggered,this,&MainWindow::createThumbnails);
    connect(openIndex,&QAction::triggered,this,[]{QDesktopServices::openUrl(QUrl::fromLocalFile(Cam::dataDirectory()));});
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
    calendar=new QCalendarWidget; calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader); calendar->setGridVisible(false); calendar->setMinimumWidth(225); calendar->setFixedHeight(250); sideLayout->addWidget(calendar);
    calendar->setAccessibleName("录像日期"); calendar->setToolTip("高亮日期有录像，点击日期筛选");
    auto timeHeader=new QHBoxLayout; timeEnabled=new QCheckBox("时间范围"); timeHeader->addWidget(timeEnabled); timeHeader->addStretch(); timeHeader->addWidget(Ui::info("日期筛选说明","高亮日期有录像。点击日期筛选当天，也可勾选时间范围后设置起止时间。")); sideLayout->addLayout(timeHeader);
    from=new QDateTimeEdit(QDateTime(QDate::currentDate(),QTime(0,0))); to=new QDateTimeEdit(QDateTime(QDate::currentDate(),QTime(23,59,59)));
    from->setAccessibleName("筛选开始时间"); to->setAccessibleName("筛选结束时间");
    for(auto edit:{from,to}) {edit->setDisplayFormat("yyyy-MM-dd HH:mm:ss"); edit->setCalendarPopup(true);}
    auto dates=new QFormLayout; dates->setHorizontalSpacing(8); dates->addRow(Ui::label("开始",from),from); dates->addRow(Ui::label("结束",to),to); sideLayout->addLayout(dates);
    allDates=new QPushButton("全部日期"); allDates->hide(); sideLayout->addWidget(allDates);
    connect(allDates,&QPushButton::clicked,this,[this]{timeEnabled->setChecked(false);});
    connect(calendar,&QCalendarWidget::clicked,this,[this](QDate date){
        {QSignalBlocker a(from),b(to); from->setDateTime(QDateTime(date,QTime(0,0))); to->setDateTime(QDateTime(date,QTime(23,59,59)));}
        timeEnabled->setChecked(true); refreshTimer.start(100);
    });
    connect(timeEnabled,&QCheckBox::toggled,this,[this](bool enabled){from->setEnabled(enabled); to->setEnabled(enabled); allDates->setVisible(enabled); refreshTimer.start(100);}); from->setEnabled(false); to->setEnabled(false);
    connect(from,&QDateTimeEdit::dateTimeChanged,this,[this]{if(timeEnabled->isChecked()) refreshTimer.start(250);});
    connect(to,&QDateTimeEdit::dateTimeChanged,this,[this]{if(timeEnabled->isChecked()) refreshTimer.start(250);});
    sideLayout->addSpacing(10);
    kind=new QComboBox; kind->addItem("全部录像",""); kind->addItem("哨兵录像","sentry"); kind->addItem("手动保存","saved"); kind->addItem("近期行车","recent"); kind->addItem("其他录像","other"); kind->setAccessibleName("录像类型");
    state=new QComboBox; state->addItems({"全部状态","已收藏","已导出原片","未导出原片"}); state->setAccessibleName("录像状态"); state->setToolTip("收藏事件受删除保护；已导出表示完整原片已备份并校验。");
    order=new QComboBox; order->addItems({"最新在前","最早在前"}); order->setAccessibleName("时间排序");
    auto filters=new QFormLayout; filters->setHorizontalSpacing(8); filters->addRow(Ui::label("类型",kind),kind); filters->addRow(Ui::label("状态",state),state); filters->addRow(Ui::label("排序",order),order); sideLayout->addLayout(filters);
    for(auto combo:{kind,state,order}) connect(combo,&QComboBox::currentIndexChanged,this,[this]{refreshTimer.start(80);});
    sideLayout->addStretch();
    auto sideScroll=new QScrollArea; sideScroll->setWidget(side); sideScroll->setWidgetResizable(true); sideScroll->setFrameShape(QFrame::NoFrame); sideScroll->setMinimumWidth(280); sideScroll->setMaximumWidth(310); sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); splitter->addWidget(sideScroll);
    auto middle=new QWidget; auto mid=new QVBoxLayout(middle); mid->setContentsMargins(12,0,12,0); mid->setSpacing(10);
    search=new QLineEdit; search->setPlaceholderText("搜索日期或事件"); search->setAccessibleName("搜索日期或事件目录"); search->setClearButtonEnabled(true); mid->addWidget(search); connect(search,&QLineEdit::textChanged,this,[this]{refreshTimer.start(250);});
    summary=new QLabel; summary->setObjectName("section"); mid->addWidget(summary);
    table=new QTableWidget; table->setColumnCount(4); table->setHorizontalHeaderLabels({"选择","","时间 / 类型","大小"}); table->setAccessibleName("录像事件列表"); table->verticalHeader()->hide(); table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection); table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setShowGrid(false); table->setAlternatingRowColors(true); table->setIconSize(QSize(100,56)); table->setColumnWidth(0,44); table->setColumnWidth(1,106); table->setColumnWidth(3,75); table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch); eventStack=new QStackedWidget; listEmpty=new QLabel("暂无录像"); listEmpty->setAlignment(Qt::AlignCenter); listEmpty->setObjectName("emptyState"); eventStack->addWidget(listEmpty); eventStack->addWidget(table); mid->addWidget(eventStack,1);
    connect(table,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){if(!filling) loadEvent(row);});
    connect(table,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){
        if(filling || item->column()!=0) return;
        const QString key=item->data(Qt::UserRole).toString(); if(item->checkState()==Qt::Checked) checked.insert(key); else checked.remove(key); updateSelection();
    });
    listTools=new QWidget; auto pagination=new QHBoxLayout(listTools); pagination->setContentsMargins(0,0,0,0); auto selectPage=new QPushButton("勾选本页"); auto clearSelection=new QPushButton("清空勾选"); previous=new QPushButton("上一页"); next=new QPushButton("下一页"); pageInfo=new QLabel;
    pagination->addWidget(selectPage); pagination->addWidget(clearSelection); pagination->addStretch(); pagination->addWidget(previous); pagination->addWidget(pageInfo); pagination->addWidget(next); mid->addWidget(listTools);
    connect(selectPage,&QPushButton::clicked,this,[this]{for(int i=0;i<table->rowCount();++i) table->item(i,0)->setCheckState(Qt::Checked);});
    connect(clearSelection,&QPushButton::clicked,this,[this]{checked.clear(); fillTable();});
    connect(previous,&QPushButton::clicked,this,[this]{if(page>0){--page; fillTable();}}); connect(next,&QPushButton::clicked,this,[this]{++page; fillTable();});
    selectionInfo=new QLabel; selectionInfo->setWordWrap(true); mid->addWidget(selectionInfo);
    auto batch=new QHBoxLayout; originalButton=new QPushButton("导出原片"); originalButton->setObjectName("primary"); originalButton->setToolTip("导出所选事件的全部摄像头原片及事件信息，并校验备份"); backupButton=new QPushButton("备份后删除"); deleteButton=new QPushButton("永久删除"); deleteButton->setObjectName("danger"); batch->addWidget(originalButton); batch->addWidget(backupButton); batch->addWidget(deleteButton); mid->addLayout(batch);
    connect(originalButton,&QPushButton::clicked,this,&MainWindow::exportOriginals); connect(backupButton,&QPushButton::clicked,this,[this]{clean(true);}); connect(deleteButton,&QPushButton::clicked,this,[this]{clean(false);});
    splitter->addWidget(middle);
    auto right=new QWidget; auto rl=new QVBoxLayout(right); rl->setContentsMargins(0,0,0,0); rl->setSpacing(10);
    auto eventHeader=new QHBoxLayout; eventInfo=new QLabel("预览"); eventInfo->setWordWrap(true); eventInfo->setObjectName("section"); eventHeader->addWidget(eventInfo,1); favoriteButton=new QPushButton("☆ 收藏"); eventHeader->addWidget(favoriteButton); rl->addLayout(eventHeader);
    player=new EventPlayer; rl->addWidget(player,1);
    auto utility=new QHBoxLayout; revealButton=new QPushButton("打开文件位置"); utility->addWidget(revealButton); utility->addStretch(); rl->addLayout(utility);
    connect(player,&EventPlayer::rangeExportRequested,this,&MainWindow::exportRange);
    connect(revealButton,&QPushButton::clicked,this,[this]{
        try {auto clips=db->clips(current.id,currentEvent); if(!clips.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(Cam::verifiedPath(current,clips.first())).absolutePath()));}
        catch(const std::exception& e){QMessageBox::warning(this,"无法打开",errorText(e));}
    });
    connect(favoriteButton,&QPushButton::clicked,this,[this]{
        if(currentEvent.isEmpty() || worker) return;
        for(const auto& e:eventList) if(e.key==currentEvent){db->setFavorite(current.id,e.key,!e.favorite); break;} refresh();
    });
    splitter->addWidget(right);
    splitter->setStretchFactor(0,0); splitter->setStretchFactor(1,5); splitter->setStretchFactor(2,6); splitter->setSizes({270,550,610});
    auto bottom=new QHBoxLayout; status=new QLabel; status->setWordWrap(true); bottom->addWidget(status,1); progress=new QProgressBar; progress->setAccessibleName("当前任务进度"); progress->setMinimumWidth(190); progress->setMaximumWidth(260); progress->setValue(0); bottom->addWidget(progress); cancelButton=new QPushButton("取消"); cancelButton->setEnabled(false); cancelButton->hide(); progress->hide(); bottom->addWidget(cancelButton); outer->addLayout(bottom);
    connect(cancelButton,&QPushButton::clicked,this,[this]{canceled=true; cancelButton->setEnabled(false); status->setText("正在停止，已完成的索引或文件操作会保留…");});
    operationControls={sources,chooseButton,scanButton,originalButton,backupButton,deleteButton,favoriteButton};
    qApp->setStyleSheet(R"(
        QWidget { font-family: 'Microsoft YaHei UI'; font-size: 12px; color: #243447; }
        QMainWindow, QMainWindow > QWidget { background: #f4f7fa; }
        QLabel#title { font-size: 25px; font-weight: 700; color: #102a43; }
        QLabel#section { font-size: 13px; font-weight: 700; }
        QLabel#muted { color: #5c6d7e; font-size: 12px; }
        QLabel#emptyState { color: #5c6d7e; font-size: 14px; }
        QToolButton { background: white; border: 1px solid #d1dce5; border-radius: 6px; padding: 8px 11px; }
        QToolButton#infoButton { background: transparent; border: none; color: #728294; font-size: 17px; padding: 2px 5px; }
        QToolButton#infoButton:focus { background: #deeeeb; border: 2px solid #147d72; }
        QToolButton:focus, QPushButton:focus { border: 2px solid #147d72; }
        QWidget#sidebar { background: #e9f0f5; border-radius: 10px; }
        QPushButton { background: white; border: 1px solid #d1dce5; border-radius: 6px; padding: 8px 11px; }
        QPushButton:hover { background: #e6f1f5; border-color: #75b7b0; }
        QPushButton:disabled { color: #9ba8b5; background: #eef2f5; }
        QPushButton#primary { background: #147d72; color: white; border-color: #147d72; font-weight: 600; }
        QPushButton#primary:hover { background: #10685f; }
        QPushButton#primary:disabled { background: #dce7e7; color: #93a2af; border-color: #dce7e7; }
        QPushButton#danger { color: #b53e45; border-color: #e6c5c8; }
        QComboBox, QLineEdit, QDateTimeEdit, QSpinBox { background: white; border: 1px solid #d1dce5; border-radius: 5px; padding: 6px; min-height: 19px; }
        QTableWidget { background: white; alternate-background-color: #f6f9fb; border: 1px solid #e0e7ed; border-radius: 7px; selection-background-color: #d6eeea; selection-color: #173c38; }
        QHeaderView::section { background: #eaf0f4; border: none; padding: 9px 4px; color: #607589; }
        QTableWidget::item { padding: 3px; border-bottom: 1px solid #eef2f5; }
        QWidget#cameraBox { background: #142332; border-radius: 7px; }
        QLabel#cameraTitle { color: #d6e7f2; font-weight: 600; }
        QLabel#cameraStatus { color: #bbcbd7; }
        QWidget#rangePanel { background: #e9f0f5; border-radius: 7px; }
        QProgressBar { background: #dce5ec; border: none; border-radius: 3px; text-align: center; }
        QProgressBar::chunk { background: #198b7d; border-radius: 3px; }
        QCalendarWidget QWidget { font-size: 11px; }
        QCalendarWidget QToolButton { color: #243447; background: #dbe7ee; padding: 3px; }
        QSplitter::handle { background: transparent; width: 6px; }
    )");
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
    const QString path=QFileDialog::getExistingDirectory(this,"选择 U 盘根目录或 TeslaCam 录像目录",initial);
    if(path.isEmpty()) return;
    try {
        auto s=Cam::identifySource(path);
        for(const auto& old:db->sources()) if(old.id==s.id) {s.lastScan=old.lastScan; s.status=old.status;}
        db->saveSource(s); checked.clear(); currentEvent.clear(); player->clear(); loadSources(s.id); page=0; refresh();
        status->clear();
    } catch(const std::exception& e) {QMessageBox::warning(this,"无法选择目录",errorText(e));}
}
void MainWindow::refresh() {
    const bool hasSource=!current.id.isEmpty();
    capacity->setVisible(hasSource); diskBar->setVisible(hasSource); scanInfo->setVisible(hasSource);
    summary->setVisible(hasSource);
    if(!hasSource) {listEmpty->setText("暂无录像"); setBusy(worker!=nullptr); return;}
    for(const auto& s:db->sources()) if(s.id==current.id) current=s;
    bool online=false;
    try {online=Cam::identifySource(current.root).id==current.id;} catch(...) {}
    QStorageInfo storage(current.root);
    if(online && storage.isValid() && storage.bytesTotal()>0) {
        capacity->setText("可用 "+Cam::humanBytes(storage.bytesAvailable())+" / "+Cam::humanBytes(storage.bytesTotal()));
        diskBar->setValue(int(100.0*(storage.bytesTotal()-storage.bytesAvailable())/storage.bytesTotal()));
    } else {capacity->setText("设备离线"); diskBar->setValue(0);}
    capacity->setToolTip(QDir::toNativeSeparators(current.root));
    capacity->setAccessibleName("设备存储空间"); diskBar->setAccessibleName("设备已用空间百分比");
    scanInfo->setText(current.lastScan.isEmpty()?QString("未扫描"):"上次扫描 "+current.lastScan);
    scanInfo->setToolTip(current.status+(online?"":"\n重新选择目录可匹配盘符变化；离线仅可查看索引与缩略图。"));
    scanButton->setText(current.lastScan.isEmpty()?"扫描建索引":"更新索引");
    const QString begin=timeEnabled->isChecked()?from->dateTime().toString("yyyy-MM-dd HH:mm:ss"):QString();
    const QString end=timeEnabled->isChecked()?to->dateTime().toString("yyyy-MM-dd HH:mm:ss"):QString();
    eventList=db->events(current.id,kind->currentData().toString(),begin,end,state->currentIndex(),search->text(),order->currentIndex()==1);
    qint64 bytes=0; int files=0; QSet<QString> available;
    for(const auto& e:eventList) {bytes+=e.bytes; files+=e.files; available.insert(e.key);}
    // Do not retain hidden selections after a filter change.
    checked.intersect(available);
    summary->setText(QString("%1 条事件 · %2").arg(eventList.size()).arg(Cam::humanBytes(bytes)));
    summary->setToolTip(QString("%1 个视频文件").arg(files));
    calendar->setDateTextFormat(QDate(),QTextCharFormat());
    auto q=db->query("SELECT DISTINCT substr(stamp,1,10) FROM clips WHERE source=?",{current.id});
    QTextCharFormat format; format.setForeground(QColor("#087b68")); format.setBackground(QColor("#d0eee4")); format.setFontWeight(QFont::Bold);
    while(q.next()) calendar->setDateTextFormat(QDate::fromString(q.value(0).toString(),Qt::ISODate),format);
    fillTable(); setBusy(worker!=nullptr);
}
void MainWindow::fillTable() {
    filling=true; const int pageSize=100; page=qBound(0,page,qMax(0,(int(eventList.size())-1)/pageSize));
    const int first=page*pageSize, count=qMin(pageSize,int(eventList.size())-first); table->setRowCount(qMax(0,count));
    int selectedRow=-1;
    for(int row=0;row<count;++row) {
        const auto& e=eventList[first+row]; auto check=new QTableWidgetItem;
        check->setFlags(Qt::ItemIsEnabled|Qt::ItemIsUserCheckable|Qt::ItemIsSelectable); check->setCheckState(checked.contains(e.key)?Qt::Checked:Qt::Unchecked); check->setData(Qt::UserRole,e.key); check->setData(Qt::AccessibleTextRole,"选择事件 "+e.start); table->setItem(row,0,check);
        auto preview=new QTableWidgetItem; const auto clips=db->clips(current.id,e.key);
        preview->setData(Qt::AccessibleTextRole,"录像缩略图");
        if(!clips.isEmpty()) {const QString thumb=Cam::thumbnailPath(current,clips.first()); if(QFileInfo::exists(thumb)) preview->setIcon(QIcon(thumb)); else {preview->setIcon(style()->standardIcon(QStyle::SP_FileIcon)); preview->setToolTip("尚无缩略图，可在“更多”中生成预览");}}
        table->setItem(row,1,preview);
        const QString detail=(e.favorite?"★ ":"")+e.start+"\n"+kindText(e.kind)+" · "+QString::number(e.cameras)+" 视角 · "+QString::number(e.files)+" 段"+(e.exported?" · 已导出":"");
        auto text=new QTableWidgetItem(detail); text->setData(Qt::AccessibleTextRole,(e.favorite?QString("已收藏，删除保护，"):QString())+detail);
        text->setToolTip(e.key+"\n末段起始："+e.end+(e.kind=="recent"?"\n近期行车按固定十分钟分组，属于推断分组。":"")); table->setItem(row,2,text); table->setItem(row,3,new QTableWidgetItem(Cam::humanBytes(e.bytes))); table->setRowHeight(row,72);
        if(e.key==currentEvent) selectedRow=row;
    }
    pageInfo->setText(QString("%1 / %2").arg(page+1).arg(qMax(1,(int(eventList.size())+pageSize-1)/pageSize))); previous->setEnabled(page>0); next->setEnabled(first+count<eventList.size());
    if(selectedRow>=0) table->selectRow(selectedRow);
    filling=false;
    if(selectedRow<0 && !currentEvent.isEmpty()) {currentEvent.clear(); player->clear(); eventInfo->setText("预览");}
    else if(selectedRow>=0) for(const auto& e:eventList) if(e.key==currentEvent) favoriteButton->setText(e.favorite?"★ 已收藏":"☆ 收藏");
    eventStack->setCurrentWidget(eventList.isEmpty()?static_cast<QWidget*>(listEmpty):table);
    if(eventList.isEmpty()) listEmpty->setText(current.lastScan.isEmpty()?"尚未建立索引":"没有匹配的录像");
    previous->setVisible(eventList.size()>pageSize); next->setVisible(eventList.size()>pageSize); pageInfo->setVisible(eventList.size()>pageSize);
    updateSelection();
}
void MainWindow::loadEvent(int row) {
    if(row<0 || !table->item(row,0)) return;
    currentEvent=table->item(row,0)->data(Qt::UserRole).toString();
    const auto clips=db->clips(current.id,currentEvent); QString trigger;
    if(!clips.isEmpty()) {
        try {
            const QString folder=QFileInfo(Cam::verifiedPath(current,clips.first())).absolutePath();
            QFile metadata(folder+"/event.json");
            if(metadata.size()<1024*1024 && metadata.open(QIODevice::ReadOnly)) trigger=QJsonDocument::fromJson(metadata.readAll()).object().value("timestamp").toString();
        } catch(...) {}
    }
    player->load(current,clips,trigger);
    for(const auto& e:eventList) if(e.key==currentEvent) {eventInfo->setText(e.start); eventInfo->setToolTip(kindText(e.kind)+" · "+Cam::humanBytes(e.bytes)); favoriteButton->setText(e.favorite?"★ 已收藏":"☆ 收藏"); break;}
    updateSelection();
}
QStringList MainWindow::selectedKeys() const {
    if(!checked.isEmpty()) {QStringList keys=checked.values(); keys.sort(); return keys;}
    return currentEvent.isEmpty()?QStringList():QStringList{currentEvent};
}
void MainWindow::updateSelection() {
    const auto keys=selectedKeys(); qint64 bytes=0; int protectedCount=0;
    for(const auto& e:eventList) if(keys.contains(e.key)) {bytes+=e.bytes; if(e.favorite) ++protectedCount;}
    selectionInfo->setVisible(!checked.isEmpty());
    selectionInfo->setText(QString("已选 %1 条 · %2").arg(keys.size()).arg(Cam::humanBytes(bytes))+(protectedCount>0?QString(" · %1 条受保护").arg(protectedCount):QString()));
    originalButton->setEnabled(!worker && !keys.isEmpty());
    backupButton->setEnabled(!worker && keys.size()>protectedCount); deleteButton->setEnabled(backupButton->isEnabled());
    favoriteButton->setEnabled(!worker && !currentEvent.isEmpty());
    favoriteButton->setVisible(!currentEvent.isEmpty());
    favoriteButton->setAccessibleDescription("收藏的事件受到删除保护"); favoriteButton->setToolTip("收藏的事件受到删除保护");
    deleteButton->setToolTip(protectedCount==keys.size() && !keys.isEmpty()?"收藏事件受保护，需先取消收藏":"永久删除所选事件的全部摄像头录像，不进入回收站，无法撤销");
    backupButton->setToolTip("完整备份并校验成功后，永久删除所选事件的录像");
    revealButton->setVisible(!currentEvent.isEmpty());
}
void MainWindow::setBusy(bool value) {
    for(auto control:operationControls) control->setEnabled(!value);
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
void MainWindow::run(const QString& title,std::function<Cam::Result(std::atomic_bool&,const Cam::Progress&)> work,bool thumbsAfter) {
    if(worker) return;
    canceled=false; allowBrowse=!title.contains("清理"); thumbnailJob=title.contains("缩略图"); jobElapsed.restart(); setBusy(true); status->setText(title); progress->setRange(0,0);
    worker=QThread::create([this,title,work=std::move(work),thumbsAfter] {
        Cam::Result result; QString error;
        try {
            result=work(canceled,[this,title](int done,int total,const QString& message){
                QMetaObject::invokeMethod(this,[this,title,done,total,message]{
                    if(total>0){progress->setRange(0,total); progress->setValue(done);} else progress->setRange(0,0);
                    const qint64 seconds=jobElapsed.elapsed()/1000;
                    QString timing=QString(" · 已用 %1 秒").arg(seconds);
                    if(total>0 && done>0) timing+=QString(" · 预计剩余 %1 秒").arg(qMax(qint64(0),seconds*(total-done)/done));
                    status->setText(message.left(130)+timing);
                    if(title.contains("索引") || title.contains("扫描")) refreshTimer.start(400);
                },Qt::QueuedConnection);
            });
        } catch(const std::exception& e){error=errorText(e);}
        QMetaObject::invokeMethod(this,[this,title,result,error,thumbsAfter]{
            worker=nullptr; progress->setRange(0,100); progress->setValue(error.isEmpty()&&!result.canceled?100:0); loadSources(current.id); refresh();
            report(title,result,error);
            if(closing) {close(); return;}
            if(thumbsAfter && error.isEmpty() && !result.canceled) QTimer::singleShot(150,this,&MainWindow::createThumbnails);
        },Qt::QueuedConnection);
    });
    connect(worker,&QThread::finished,worker,&QObject::deleteLater); worker->start();
}
void MainWindow::report(const QString& title,const Cam::Result& r,const QString& error) {
    QFile log(Cam::dataDirectory()+"/operations.log");
    if(log.open(QIODevice::Append|QIODevice::Text)) log.write((QDateTime::currentDateTime().toString(Qt::ISODate)+" "+title+" "+QString::number(r.files)+" files\n"+error+"\n"+r.errors.join('\n')+"\n"+r.destination+"\n").toUtf8());
    if(!error.isEmpty()) {status->setText(title+"失败"); status->setToolTip(error); if(!closing) QMessageBox::warning(this,title+"失败",error); return;}
    status->setToolTip(QString("%1 个视频 · %2 条事件 · %3\n新增/更新 %4 · 移除失效 %5\n%6").arg(r.files).arg(r.events).arg(Cam::humanBytes(r.bytes)).arg(r.changed).arg(r.removed).arg(r.errors.join('\n')));
    status->setText(title+(r.canceled?"已取消":"完成")+(r.errors.isEmpty()?QString():QString(" · %1 项提示").arg(r.errors.size())));
    const QString message=status->text();
    QTimer::singleShot(8000,this,[this,message]{if(!worker && status->text()==message) status->clear();});
    if((!r.destination.isEmpty() || !r.errors.isEmpty()) && !closing && !thumbnailJob) {
        QMessageBox box(QMessageBox::Information,title,status->text()+(r.destination.isEmpty()?QString():"\n保存位置："+QDir::toNativeSeparators(r.destination)),QMessageBox::Ok,this);
        if(!r.errors.isEmpty()) box.setDetailedText(r.errors.join('\n')); box.exec();
    }
}
void MainWindow::startScan(bool rebuild) {
    if(worker || current.id.isEmpty()) return;
    try {if(Cam::identifySource(current.root).id!=current.id) failUi("设备身份变化，请重新选择 U 盘。");}
    catch(const std::exception& e){QMessageBox::warning(this,"无法扫描",errorText(e)); return;}
    player->clear(); currentEvent.clear(); checked.clear(); const auto s=current; const auto path=dbPath;
    run(rebuild?"完整扫描":"更新索引",[path,s,rebuild](auto& cancel,const auto& progress){return Cam::scan(path,s,rebuild,cancel,progress);},true);
}
void MainWindow::exportOriginals() {
    if(worker) return; const auto keys=selectedKeys(); if(keys.isEmpty()) {QMessageBox::information(this,"导出原片","请先选中或勾选事件。"); return;}
    const QString destination=QFileDialog::getExistingDirectory(this,"选择原始录像的备份目录"); if(destination.isEmpty()) return;
    player->pause(); const auto source=current; const auto path=dbPath;
    run("导出原片",[path,source,keys,destination](auto& cancel,const auto& progress){return Cam::exportOriginals(path,source,keys,destination,cancel,progress);});
}
void MainWindow::clean(bool backupFirst) {
    if(worker) return; auto keys=selectedKeys(); qint64 bytes=0; int protectedCount=0;
    for(const auto& e:eventList) if(keys.contains(e.key)) {
        if(e.favorite) {keys.removeAll(e.key); ++protectedCount;} else bytes+=e.bytes;
    }
    if(keys.isEmpty()) {QMessageBox::information(this,"清理录像","请选中需要清理的事件。收藏事件受到保护，需先取消收藏。"); return;}
    QString destination;
    if(backupFirst) {
        destination=QFileDialog::getExistingDirectory(this,"选择电脑或其他磁盘上的备份目录"); if(destination.isEmpty()) return;
        QStorageInfo sourceDisk(current.root),targetDisk(destination);
        if(sourceDisk.device()==targetDisk.device()) {QMessageBox::warning(this,"请选择其他磁盘","备份后删除需要把备份保存到另一个磁盘，才能释放当前设备的空间。"); return;}
    }
    QMessageBox confirm(QMessageBox::Warning,backupFirst?"确认备份后删除":"确认永久删除",
        QString("将%1 %2 条事件的所有摄像头录像，预计释放 %3。\n已排除 %4 条收藏事件。\n\n%5\n不会删除 U 盘目录或其他文件；事件元数据保留。").arg(backupFirst?"备份并校验，然后永久删除":"永久删除").arg(keys.size()).arg(Cam::humanBytes(bytes)).arg(protectedCount).arg(backupFirst?"只有全部原片与事件信息成功备份并校验后，才会开始删除。":"此操作不进入回收站，无法撤销。"),QMessageBox::Cancel,this);
    auto proceed=confirm.addButton(backupFirst?"备份成功后永久删除":"永久删除这些录像",QMessageBox::DestructiveRole); confirm.setDefaultButton(QMessageBox::Cancel); confirm.exec(); if(confirm.clickedButton()!=proceed) return;
    player->clear(); currentEvent.clear(); const auto source=current; const auto path=dbPath;
    run(backupFirst?"备份并清理":"清理录像",[path,source,keys,destination,backupFirst](auto& cancel,const auto& progress){
        QString saved;
        if(backupFirst) {
            auto backup=Cam::exportOriginals(path,source,keys,destination,cancel,progress); saved=backup.destination;
            if(cancel || !backup.errors.isEmpty() || backup.events!=keys.size()) {backup.errors.prepend("备份未全部完成，未执行删除。"); return backup;}
        }
        auto result=Cam::deleteEvents(path,source,keys,cancel,progress); result.destination=saved; return result;
    });
}
void MainWindow::createThumbnails() {
    if(worker || current.id.isEmpty() || ffmpeg().isEmpty()) return;
    QList<Cam::Clip> pending;
    const int first=page*100, last=qMin(first+100,int(eventList.size()));
    for(int i=first;i<last;++i) {auto values=db->clips(current.id,eventList[i].key); if(!values.isEmpty() && !QFileInfo::exists(Cam::thumbnailPath(current,values.first()))) pending << values.first();}
    if(pending.isEmpty()) return;
    const auto source=current; const auto executable=ffmpeg();
    run("生成本页缩略图",[source,pending,executable](auto& cancel,const auto& progress){
        Cam::Result result;
        for(const auto& clip:pending) {
            if(cancel) break;
            try {
                const QString path=Cam::verifiedPath(source,clip), target=Cam::thumbnailPath(source,clip), temporary=target+".tmp.jpg"; QString error;
                if(Cam::runFfmpeg(executable,{"-hide_banner","-loglevel","error","-y","-i",path,"-frames:v","1","-vf","scale=240:-2",temporary},cancel,&error,20)) {
                    if(!cancel && QFile::rename(temporary,target)) ++result.files; else QFile::remove(temporary);
                }
                else if(!cancel) result.errors << clip.relative+"："+error;
                QFile::remove(temporary);
            } catch(const std::exception& e){result.errors << errorText(e);}
            if(progress) progress(result.files+int(result.errors.size()),int(pending.size()),"生成缩略图 · "+clip.relative);
        }
        result.canceled=cancel; return result;
    });
}
void MainWindow::exportRange() {
    if(worker || currentEvent.isEmpty()) return;
    const auto executable=ffmpeg(); if(executable.isEmpty()){QMessageBox::warning(this,"缺少视频工具","未找到 ffmpeg.exe，请放到程序旁边再重试。"); return;}
    const int begin=player->rangeStart(),end=player->rangeEnd(); if(end<=begin){QMessageBox::warning(this,"时间范围无效","终点必须晚于起点。"); return;}
    const auto clips=db->clips(current.id,currentEvent); const QString camera=player->camera();
    const QString destination=QFileDialog::getExistingDirectory(this,"选择裁剪视频的保存目录"); if(destination.isEmpty()) return;
    const auto source=current; player->pause();
    run("导出观看版",[source,clips,camera,begin,end,destination,executable](auto& cancel,const auto& progress){
        return Cam::exportRange(source,clips,camera,begin,end,destination,executable,cancel,progress);
    });
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if(worker) {closing=true; canceled=true; status->setText("正在停止后台任务，完成后关闭…"); event->ignore(); return;}
    player->clear(); event->accept();
}
