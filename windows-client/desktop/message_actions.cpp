#include "interface_text.h"
#include "window.h"
#include "ui.h"
#include "message_input.h"
#include "message_text.h"
#include "wrap_layout.h"

namespace {
class EditInput final : public MessageInput {
public:

    std::function<void()> save,cancel;
    std::function<void(int)> resized;
    void addResizeHandle(){
        grip=new QWidget(this);grip->setObjectName("editResizeHandle");grip->setCursor(Qt::SizeVerCursor);grip->setFixedSize(12,12);
        grip->setStyleSheet("background:transparent");grip->installEventFilter(this);placeGrip();grip->raise();
    }
protected:
    bool eventFilter(QObject* target,QEvent* event)override{
        if(target!=grip)return QPlainTextEdit::eventFilter(target,event);
        if(event->type()==QEvent::Paint){
            QPainter painter(grip);painter.setPen(QColor("#777"));
            painter.drawLine(4,10,10,4);painter.drawLine(8,10,10,8);return true;
        }
        if(event->type()==QEvent::MouseButtonPress){
            auto* mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()!=Qt::LeftButton)return false;
            dragY=mouse->globalPosition().y();startHeight=height();return true;
        }
        if(event->type()==QEvent::MouseMove && startHeight){
            const auto y=static_cast<QMouseEvent*>(event)->globalPosition().y();
            const int next=qBound(90,startHeight+qRound(y-dragY),300);
            if(height()!=next){setFixedHeight(next);resized(next);}return true;
        }
        if(event->type()==QEvent::MouseButtonRelease){startHeight=0;return true;}
        return false;
    }
    void resizeEvent(QResizeEvent* event)override{QPlainTextEdit::resizeEvent(event);placeGrip();}
    void placeGrip(){if(grip)grip->move(width()-grip->width(),height()-grip->height());}
    QWidget* grip=nullptr;
    qreal dragY=0;
    int startHeight=0;
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Escape){cancel();event->accept();return;}
        if((event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)&&event->modifiers()==Qt::ControlModifier){
            save();event->accept();return;
        }
        MessageInput::keyPressEvent(event);
    }
};
QString messagePath(const QString& id) {
    return "/api/messages/"+QString::fromLatin1(QUrl::toPercentEncoding(id));
}
}

void DesktopWindow::clearMessageActions(){
    mentionVisibleSince.clear();readingMentions.clear();mentionTimer.stop();mentionDot->hide();
    ++fileRequest;chatFileError->hide();dropOverlay->hide();
    replyId.clear();replyBanner->hide();
    editOriginal={};editDraft.clear();editError.clear();editBusy=false;
    deleteTarget.clear();deleteBusy=false;deleteBanner->hide();
    pendingReactions.clear();
}

void DesktopWindow::buildMessageBanners(){
    deleteBanner=new QWidget(roomPage);deleteBanner->setObjectName("deletePrompt");deleteBanner->setAttribute(Qt::WA_StyledBackground);deleteBanner->setStyleSheet("#deletePrompt{background:#262626;border-radius:9px}");
    auto* confirm=new QHBoxLayout(deleteBanner);
    confirm->setContentsMargins(3,2,3,2);confirm->setSpacing(2);
    confirmDelete=new QPushButton("✓");cancelDelete=new QPushButton("×");
    confirmDelete->setAccessibleName(UiText::ConfirmDeletion());cancelDelete->setAccessibleName(UiText::CancelDeletion());
    for(auto* action:{confirmDelete,cancelDelete})action->setFixedSize(30,30);
    confirmDelete->setStyleSheet("QPushButton{color:#ed8989;background:transparent;border:0;border-radius:7px} QPushButton:hover{background:#533239}");
    cancelDelete->setStyleSheet("QPushButton{color:#bbb;background:transparent;border:0;border-radius:7px} QPushButton:hover{background:#393939}");
    confirm->addWidget(confirmDelete);confirm->addWidget(cancelDelete);
    auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),deleteBanner);
    connect(escape,&QShortcut::activated,cancelDelete,&QPushButton::click);
    connect(cancelDelete,&QPushButton::clicked,this,[this]{cancelDelete->clearFocus();deleteTarget.clear();deleteBanner->hide();});
    connect(confirmDelete,&QPushButton::clicked,this,[this]{
        if(deleteBusy||deleteTarget.isEmpty())return;
        deleteBusy=true;confirmDelete->setEnabled(false);cancelDelete->setEnabled(false);
        const auto id=deleteTarget;
        const auto route=messagePath(id)+"?channel="+QString::fromLatin1(QUrl::toPercentEncoding(selected));
        api.request("DELETE",route,{},[this,id,gen=channelGeneration](QJsonObject,QString err){
            if(gen!=channelGeneration || deleteTarget!=id)return;
            deleteBusy=false;confirmDelete->setEnabled(true);cancelDelete->setEnabled(true);
            if(!err.isEmpty()){note(err);return;}
            ++messageRevision;
            for(int i=lastMessages.size()-1;i>=0;--i){
                auto message=lastMessages[i].toObject();
                if(message["id"]==id)lastMessages.removeAt(i);
                else if(message["replyTo"]==id){
                    message["reply"]=QJsonObject{{"id",id},{"name",""},{"text",""},{"deleted",true}};
                    lastMessages[i]=message;
                }
            }
            deleteTarget.clear();deleteBanner->hide();updateMessageRows();loadMessages();
        });
    });
    deleteBanner->hide();
    replyBanner=new QWidget;
    replyBanner->setObjectName("replyDraft");replyBanner->setAttribute(Qt::WA_StyledBackground);
    replyBanner->setStyleSheet("#replyDraft{background:#191c1b;border-radius:12px}");
    auto* layout=new QHBoxLayout(replyBanner);layout->setContentsMargins(14,10,14,10);
    auto* lines=new QVBoxLayout;lines->setSpacing(4);
    replyName=Ui::text("","font-size:12px;font-weight:500");
    replyPreview=Ui::text("","font-size:12px;color:#888");replyPreview->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    lines->addWidget(replyName);lines->addWidget(replyPreview);layout->addLayout(lines,1);
    auto* cancel=Ui::action("close",UiText::CancelReply());
    connect(cancel,&QPushButton::clicked,this,[this,cancel]{cancel->clearFocus();replyId.clear();replyBanner->hide();});
    layout->addWidget(cancel);roomLayout->addWidget(replyBanner);replyBanner->hide();
}

void DesktopWindow::showReply(QJsonObject message){
    replyId=message["id"].toString();
    replyName->setText(UiText::Reply()+message["name"].toString());
    replyPreview->setText(message["deleted"].toBool()?UiText::MessageDeleted():message["text"].toString().simplified());
    replyBanner->show();
}

void DesktopWindow::toggleReaction(QString id,QString emoji){
    const auto msg=Ui::byId(lastMessages,id);
    if(msg.isEmpty()||msg["deleted"].toBool()||self["chatMutedUntil"].toDouble()>QDateTime::currentMSecsSinceEpoch())return;
    const QString key=id+"/"+emoji;
    if(pendingReactions.contains(key))return;
    pendingReactions.insert(key);
    const bool active=!msg["reactions"].toObject()[emoji].toArray().contains(selfId);
    api.request("PUT",messagePath(id)+"/reactions",{{"channel",selected},{"emoji",emoji},{"active",active}},
        [this,key,gen=channelGeneration](QJsonObject,QString err){
            if(gen!=channelGeneration)return;
            pendingReactions.remove(key);if(!err.isEmpty())note(err);
            loadMessages();
        });
}

void DesktopWindow::beginMessageEdit(QJsonObject message){
    if(editBusy)return;
    editHeight=90;editOriginal=message;editFormat=message["format"].toString();editDraft=message["text"].toString();editError.clear();
    QTimer::singleShot(0,this,[this]{
        updateMessageRows();
        for(int i=0;i<messages->count();++i)
            if(auto* row=messages->itemWidget(messages->item(i)))
                if(auto* editor=row->findChild<QPlainTextEdit*>("editMessageText")){
                    editor->setFocus();editor->moveCursor(QTextCursor::End);break;
                }
    });
}

void DesktopWindow::renderMessageEdit(QVBoxLayout* layout){
    auto* editor=new EditInput;editor->setDraft(editDraft,editFormat);
    editor->setObjectName("editMessageText");editor->setAccessibleName(UiText::EditMessage());editor->setFixedHeight(editHeight);editor->addResizeHandle();
    editor->resized=[this](int height){editHeight=height;sizeMessageRows();};
    editor->setStyleSheet("QPlainTextEdit{background:#202020;color:#ededed;border:0;border-radius:10px;padding:14px 15px}QPlainTextEdit:focus{background:#2c302f}");
    editor->document()->setDocumentMargin(0);
    editor->setTabChangesFocus(true);
    layout->addSpacing(6);layout->addWidget(editor);layout->addSpacing(4);
    auto* buttons=new QHBoxLayout;buttons->setSpacing(8);
    auto* save=new QPushButton(UiText::Save());auto* cancel=new QPushButton(UiText::Cancel());
    save->setFixedHeight(38);cancel->setFixedHeight(34);
    save->setStyleSheet("QPushButton{background:#202020;border:0;border-radius:11px;padding:0 16px;font-size:12px} QPushButton:hover{background:#303030}");
    cancel->setStyleSheet("QPushButton{background:transparent;color:#aaa;border:0;border-radius:11px;padding:0 10px;font-size:12px} QPushButton:hover{background:#303030}");
    buttons->addWidget(save);buttons->addWidget(cancel);buttons->addWidget(Ui::text(UiText::CtrlEnterToSave(),"color:#999;font-size:11px"));buttons->addStretch();layout->addLayout(buttons);
    auto* error=Ui::text(editError,"font-size:11px;color:#ddd");layout->addWidget(error);error->setVisible(!editError.isEmpty());
    connect(editor,&QPlainTextEdit::textChanged,editor,[this,editor]{
        if(editor->document()->characterCount()>8001){
            QTextCursor cursor(editor->document());cursor.setPosition(8000);cursor.movePosition(QTextCursor::End,QTextCursor::KeepAnchor);cursor.removeSelectedText();
        }
        editDraft=editor->draftText();editFormat=editor->formatMask();
    });
    editor->setEnabled(!editBusy);save->setEnabled(!editBusy);cancel->setEnabled(!editBusy);
    connect(cancel,&QPushButton::clicked,this,[this]{editOriginal={};QTimer::singleShot(0,this,&DesktopWindow::updateMessageRows);});
    connect(save,&QPushButton::clicked,this,[this,save,cancel,editor]{
        if(editBusy||editOriginal.isEmpty())return;
        editBusy=true;editor->setEnabled(false);save->setEnabled(false);cancel->setEnabled(false);
        const auto id=editOriginal["id"].toString(),original=editOriginal["text"].toString();
        auto tags=MessageText::moveMentions(original,editDraft,editOriginal["mentions"].toArray());
        api.request("PATCH",messagePath(id),{{"channel",selected},{"text",editDraft},{"format",editFormat},{"previousFormat",editOriginal["format"].toString()},{"previousText",original},{"mentions",tags}},
            [this,id,gen=channelGeneration](QJsonObject,QString err){
                if(gen!=channelGeneration||id!=editOriginal["id"].toString())return;
                editBusy=false;editError=err;
                if(err.isEmpty()){++messageRevision;editOriginal={};loadMessages();}
                updateMessageRows();
            });
    });
    editor->save=[save]{save->click();};
    editor->cancel=[cancel]{cancel->click();};
}

namespace {
QString fileSelectionError(const QStringList& files,qint64 limit=1536LL*1048576,qint64 batch=1536LL*1048576){
    if(files.size()>10)return UiText::YouCanAttachUpTo10Files();
    qint64 total=0;
    for(const auto& path:files){
        QFileInfo file(path);
        if(!file.isFile()||file.size()<=0)return UiText::SelectNonEmptyFiles();
        if(file.size()>limit)return QString(UiText::MaximumFileSize1Mb()).arg(limit/1048576);
        total+=file.size();
    }
    if(total>batch)return QString(UiText::TotalAttachmentSizeUpTo1Mb()).arg(batch/1048576);
    return {};
}
QString fileSize(qint64 size){
    if(size<1024)return QString::number(size)+UiText::B2();
    if(size<1024*1024)return QString::number(size/1024.,'f',1)+UiText::Kb3();
    return QString::number(size/(1024.*1024.),'f',2)+UiText::Mb();
}
}
void DesktopWindow::attachFiles(QStringList paths){
    if(paths.isEmpty()||!compose->isEnabled()||sending||kind!="text")return;
    auto error=fileSelectionError(fileDrafts.value(selected)+paths);
    chatFileError->setText(error);chatFileError->setVisible(!error.isEmpty());
    if(!error.isEmpty())return;
    const auto request=++fileRequest;
    api.request("GET","/api/channels/"+QString::fromLatin1(QUrl::toPercentEncoding(selected))+"/files",{},
        [this,paths,request,gen=channelGeneration](QJsonObject policy,QString err){
            if(gen!=channelGeneration || fileRequest!=request || kind!="text" || !compose->isEnabled() || sending)return;
            if(!err.isEmpty())err=UiText::CouldNotCheckFilePermissionsTryAttaching();
            else if(!policy["policy"].toObject()["enabled"].toBool())err=UiText::FilesAreDisabledInThisChannelThe();
            else {const auto limit=policy["maxBytes"].toInteger(20*1048576);err=fileSelectionError(fileDrafts.value(selected)+paths,limit,policy["maxBatchBytes"].toInteger(limit));}
            chatFileError->setText(err);chatFileError->setVisible(!err.isEmpty());
            if(err.isEmpty()){fileDrafts[selected].append(paths);updateComposer();}
        });
}

void DesktopWindow::refreshFileDrafts(){
    const auto files=fileDrafts.value(selected);
    draftPanel->setVisible(kind=="text"&&!files.isEmpty());
    if(displayedFiles==files)return;
    displayedFiles=files;Ui::clear(draftPanel->layout());
    for(int i=0;i<files.size();++i){
        const QFileInfo file(files[i]);auto* chip=new QWidget;
        chip->setObjectName("fileDraft");chip->setAttribute(Qt::WA_StyledBackground);
        chip->setStyleSheet("#fileDraft{background:#202020;border-radius:10px}");
        auto* layout=new QHBoxLayout(chip);layout->setContentsMargins(8,6,8,6);layout->setSpacing(8);
        auto* mark=Ui::text("");auto preview=Ui::icon("paperclip").pixmap(22,22);
        QImageReader reader(file.filePath());const auto size=reader.size();
        if(size.isValid() && qint64(size.width())*size.height()<=24000000){
            reader.setScaledSize(size.scaled(48,48,Qt::KeepAspectRatio));auto image=reader.read();
            if(!image.isNull())preview=QPixmap::fromImage(image);
        }
        mark->setPixmap(preview);layout->addWidget(mark);
        auto* labels=new QVBoxLayout;labels->setSpacing(1);
        labels->addWidget(Ui::text(file.fileName()));labels->addWidget(Ui::text(fileSize(file.size()),"font-size:10px;color:#aaa"));
        layout->addLayout(labels);auto* remove=Ui::action("close",UiText::Remove()+file.fileName(),26);layout->addWidget(remove);
        connect(remove,&QPushButton::clicked,this,[this,i,files]{
            if(!sending && fileDrafts.value(selected)==files){fileDrafts[selected].removeAt(i);updateComposer();}
        });
        draftPanel->layout()->addWidget(chip);
    }
}

void DesktopWindow::uploadPending(QString key){
    if(sending || !outbox.contains(key) || !compose->isEnabled() || outbox[key].room!=selected)return;
    auto& pending=outbox[key];pending.percent=0;pending.error.clear();sending=true;
    updateComposer();updateMessageRows();messages->scrollToBottom();
    const QString channel=pending.room;
    api.upload("/api/channels/"+QString::fromLatin1(QUrl::toPercentEncoding(channel))+"/files",
        pending.files,pending.fields,[this,key,channel](QJsonObject accepted,QString err){
            sending=false;
            if(!outbox.contains(key))return;
            if(err.isEmpty()){
                ++messageRevision;
                outbox.remove(key);outboxOrder.removeAll(key);
                if(channel==selected && !accepted["id"].toString().isEmpty() && Ui::byId(lastMessages,accepted["id"].toString()).isEmpty())
                    lastMessages.append(accepted);
            }else outbox[key].error=err;
            updateComposer();updateMessageRows();if(channel==selected)loadMessages();
        },[this,key](int percent){
            auto item=outbox.find(key);if(item==outbox.end())return;
            item->percent=percent;
            if(auto* status=messages->findChild<QLabel*>("upload-"+key))
                status->setText(percent==100?UiText::Scanning():QString(UiText::Uploading1()).arg(percent));
        });
}

void DesktopWindow::appendPendingRows(){
    for(const auto& key:outboxOrder){
        const auto pending=outbox.value(key);
        if(pending.room!=selected)continue;
        bool delivered=false;
        for(auto value:lastMessages)if(value.toObject()["clientId"]==key){delivered=true;break;}
        if(delivered)continue;
        auto* item=new QListWidgetItem(messages);auto* row=new QWidget;
        auto* line=new QHBoxLayout(row);line->setContentsMargins(6,10,6,14);line->setSpacing(12);
        auto* avatar=Ui::text("");avatar->setPixmap(Ui::avatar(self["name"].toString(),avatars.value(selfId),34));line->addWidget(avatar,0,Qt::AlignTop);
        auto* stack=new QVBoxLayout;line->addLayout(stack,1);auto* meta=new QHBoxLayout;
        meta->addWidget(Ui::text(self["name"].toString(),"font-weight:500"));meta->addWidget(Ui::text(UiText::OnlyVisibleToYou(),"font-size:11px;color:#999"));meta->addStretch();stack->addLayout(meta);
        const auto text=pending.fields["text"].toString();if(!text.isEmpty())stack->addWidget(Ui::text(text));
        for(const auto& path:pending.files){QFileInfo file(path);stack->addWidget(Ui::text(file.fileName()+"   "+fileSize(file.size())));}
        auto* status=Ui::text(pending.error.isEmpty()?(pending.percent==100?UiText::Scanning():QString(UiText::Uploading1()).arg(pending.percent)):UiText::NotSent());
        status->setObjectName("upload-"+key);stack->addWidget(status);
        if(pending.error.isEmpty() && sending){
            auto* cancel=new QPushButton(UiText::Cancel());stack->addWidget(cancel,0,Qt::AlignLeft);
            connect(cancel,&QPushButton::clicked,this,[this,key]{QTimer::singleShot(0,this,[this,key]{api.cancelUpload(key);});});
        }
        if(!pending.error.isEmpty()){
            auto* actions=new QHBoxLayout;
            auto* retry=new QPushButton(UiText::Retry());retry->setEnabled(!sending&&compose->isEnabled());
            connect(retry,&QPushButton::clicked,this,[this,key]{QTimer::singleShot(0,this,[this,key]{uploadPending(key);});});
            auto* remove=new QPushButton(UiText::Remove2());
            connect(remove,&QPushButton::clicked,this,[this,key]{outbox.remove(key);outboxOrder.removeAll(key);QTimer::singleShot(0,this,&DesktopWindow::updateMessageRows);});
            actions->addWidget(retry);actions->addWidget(remove);actions->addStretch();stack->addLayout(actions);
            stack->addWidget(Ui::text(pending.error,"font-size:11px;color:#999"));
        }
        messages->setItemWidget(item,row);
    }
}

void DesktopWindow::updateChatSound(bool initialize){
    if(initialize){seenNotices.clear();knownActivity={};latestNotice=0;chatSoundCooldown.invalidate();}
    bool fresh=false;
    QSet<QString> current;
    auto previousLatest=latestNotice;
    for(auto value:workspace["notifications"].toArray()){
        const auto notice=value.toObject();
        const auto id=notice["id"].toString();
        const auto created=notice["createdAt"].toInteger();
        if(!seenNotices.contains(id) && created>=previousLatest)fresh=true;
        current.insert(id);latestNotice=qMax(latestNotice,created);
    }
    seenNotices.unite(current);
    if(seenNotices.size()>1000)seenNotices=current;
    QSettings prefs(dataPath+"/desktop.ini",QSettings::IniFormat);
    const auto activity=workspace["chatActivity"].toObject();
    for(auto it=activity.begin();it!=activity.end();++it){
        const auto entry=it.value().toObject(),old=knownActivity[it.key()].toObject();
        if(entry["createdAt"].toInteger()<old["createdAt"].toInteger())continue;
        if(entry["id"]!=old["id"] && prefs.value("notify/"+api.server.host()+"/"+selfId+"/"+it.key(),false).toBool())fresh=true;
        knownActivity.insert(it.key(),entry);
    }
    if(fresh && !initialize && (!chatSoundCooldown.isValid() || chatSoundCooldown.elapsed()>=1500)){
        chatSoundCooldown.start();playSound("chat");QApplication::alert(this);
    }
}
void DesktopWindow::updateMentionIndicators(){
    QSet<QString> unread;
    for(auto value:workspace["notifications"].toArray())unread.insert(value.toObject()["channel"].toString());
    const auto activity=workspace["chatActivity"].toObject();
    QSettings prefs(dataPath+"/desktop.ini",QSettings::IniFormat);
    const QString prefix="unread/"+QString::fromLatin1(api.server.toEncoded().toHex())+"/"+selfId+"/";
    const bool first=!prefs.contains(prefix+"initialized");
    QSet<QString> fresh;
    if(!selfId.isEmpty() && !workspace.isEmpty()){
        for(auto it=activity.begin();it!=activity.end();++it){
            const auto latest=it.value().toObject();const auto key=prefix+it.key();
            if(first)prefs.setValue(key,latest.toVariantMap());
            const auto last=prefs.value(key).toMap();
            if(latest["id"].toString()!=last.value("id").toString() && latest["createdAt"].toInteger()>=last.value("createdAt").toLongLong())fresh.insert(it.key());
        }
        if(first)prefs.setValue(prefix+"initialized",true);
    }
    for(int i=0;i<channels->count();++i){
        auto* item=channels->item(i);auto room=item->data(Qt::UserRole).toJsonObject();
        if(room["kind"]!="text")continue;
        const auto channel=room["id"].toString();
        const int active=(unread.contains(channel)?2:0)|(fresh.contains(channel)?1:0);
        if(item->data(Qt::UserRole+1).toInt()==active)continue;
        item->setData(Qt::UserRole+1,active);
        QStringList notices;
        if(active&1)notices<<UiText::UnreadMessages();
        if(active&2)notices<<UiText::UnreadMentionsOrReplies();
        item->setData(Qt::AccessibleDescriptionRole,notices.join(". "));
    }
    const bool active=kind=="text" && view=="room" && unread.contains(selected);
    mentionDot->setVisible(active);
    if(active || (view=="room" && kind=="text" && fresh.contains(selected))){if(!mentionTimer.isActive())mentionTimer.start();}
    else{mentionTimer.stop();mentionVisibleSince.clear();}
}

void DesktopWindow::acknowledgeVisibleMentions(){
    if(!isActiveWindow()||!messages->isVisible()||view!="room"){mentionVisibleSince.clear();return;}
    QSet<QString> unread;
    for(auto value:workspace["notifications"].toArray()){
        auto notice=value.toObject();if(notice["channel"]==selected)unread.insert(notice["id"].toString());
    }
    const auto latest=workspace["chatActivity"].toObject()[selected].toObject();
    const QString latestId=latest["id"].toString();
    bool readChannel=false;
    QHash<QString,qint64> visible;QJsonArray ready;
    const auto now=mentionClock.elapsed();
    for(int i=0;i<messages->count();++i){
        auto* item=messages->item(i);const auto message=item->data(Qt::UserRole).toJsonObject();
        const QString id=message["id"].toString();if(id!=latestId && (!unread.contains(id)||readingMentions.contains(id)))continue;
        QRect area=messages->visualItemRect(item);
        if(!area.intersects(messages->viewport()->rect()))continue;
        const auto mentions=message["mentions"].toArray();
        auto* row=messages->itemWidget(item);
        auto* body=row?row->findChild<QLabel*>("messageBody"):nullptr;
        for(auto entry:mentions){
            const auto tag=entry.toObject();
            if(!body||!MessageText::addressed(tag,self))continue;
            QTextDocument doc;doc.setDefaultFont(body->font());doc.setDocumentMargin(0);
            doc.setHtml(body->text());doc.setTextWidth(body->width());doc.size();
            const auto block=doc.findBlock(tag["start"].toInt());
            if(!block.isValid())break;
            const auto line=block.layout()->lineForTextPosition(tag["start"].toInt()-block.position());
            if(!line.isValid())break;
            const auto pos=body->mapTo(messages->viewport(),QPoint());
            area=QRect(pos.x(),pos.y()+qFloor(block.layout()->position().y()+line.y()),body->width(),qCeil(line.height()));
            break;
        }
        const auto intersection=area.intersected(messages->viewport()->rect());
        if(intersection.height()*2<area.height())continue;
        const auto since=mentionVisibleSince.value(id,now);visible.insert(id,since);
        if(now-since>=700){
            if(id==latestId)readChannel=true;
            if(unread.contains(id)&&!readingMentions.contains(id)){ready.append(id);readingMentions.insert(id);}
        }
    }
    mentionVisibleSince=visible;
    if(readChannel){
        QSettings prefs(dataPath+"/desktop.ini",QSettings::IniFormat);
        const QString key="unread/"+QString::fromLatin1(api.server.toEncoded().toHex())+"/"+selfId+"/"+selected;
        if(prefs.value(key).toMap()!=latest.toVariantMap()){prefs.setValue(key,latest.toVariantMap());updateMentionIndicators();}
    }
    if(ready.isEmpty())return;
    api.request("POST","/api/mentions/read",{{"ids",ready}},[this,ready,gen=channelGeneration](QJsonObject,QString err){
        if(gen!=channelGeneration)return;
        for(auto id:ready)readingMentions.remove(id.toString());
        if(!err.isEmpty()){mentionVisibleSince.clear();return;}
        auto notices=workspace["notifications"].toArray();
        for(int i=notices.size()-1;i>=0;--i)if(ready.contains(notices[i].toObject()["id"]))notices.removeAt(i);
        workspace["notifications"]=notices;updateMentionIndicators();
    });
}
