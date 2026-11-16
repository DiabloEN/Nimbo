#include "interface_text.h"
#include "window.h"
#include "ui.h"
#include "key_binding.h"
#include <QMediaPlayer>

using namespace KeyBinding;

namespace {
UINT globalKey(QString code){
    if(code.size()==4&&code.startsWith("Key"))return code.back().unicode();
    if(code.size()==6&&code.startsWith("Digit"))return code.back().unicode();
    int f=code.mid(1).toInt();
    if(code.startsWith('F')&&f>=1&&f<=24)return VK_F1+f-1;
    static const QHash<QString,UINT> keys={{"ArrowDown",VK_DOWN},{"ArrowUp",VK_UP},{"ArrowLeft",VK_LEFT},
        {"ArrowRight",VK_RIGHT},{"Enter",VK_RETURN},{"Tab",VK_TAB},{"Space",VK_SPACE},{"Home",VK_HOME},
        {"End",VK_END},{"Insert",VK_INSERT},{"Delete",VK_DELETE},{"PageUp",VK_PRIOR},{"PageDown",VK_NEXT}};
    return keys.value(code);
}
}
void DesktopWindow::removeKeys(){
    for(int id:boundKeys)UnregisterHotKey(reinterpret_cast<HWND>(winId()),id);
    boundKeys.clear();
}
void DesktopWindow::bindMute(){
    removeKeys();
    if(pages->currentIndex()!=1||recorder||
       (isActiveWindow()&&KeyBinding::editing(QApplication::focusWidget())))return;
    const auto bindings=profile["hotkeys"].isObject()?profile["hotkeys"].toObject():defaults();
    for(int i=0;i<KeyBinding::actions.size();++i){
        if(i<3&&!inCall)continue;
        auto b=bindings[KeyBinding::actions[i]].toObject();
        const auto vk=globalKey(b["code"].toString());
        if(!vk)continue;
        UINT modifiers=0;
        if(b["ctrl"].toBool())modifiers|=MOD_CONTROL;if(b["alt"].toBool())modifiers|=MOD_ALT;
        if(b["shift"].toBool())modifiers|=MOD_SHIFT;if(b["meta"].toBool())modifiers|=MOD_WIN;
        if(!modifiers)continue;
        if(RegisterHotKey(reinterpret_cast<HWND>(winId()),i+1,modifiers|MOD_NOREPEAT,vk))boundKeys.insert(i+1);
    }
}
void DesktopWindow::shortcutAction(int i){
    switch(i){
    case 0:if(inCall)toggleMute();break;
    case 1:if(inCall)chooseVideo(false);break;
    case 2:if(inCall)chooseVideo(true);break;
    case 3:openView("room");setMemberPanel(!expandedMembers,true);break;
    case 4:{
        QSettings prefs(dataPath+"/desktop.ini",QSettings::IniFormat);
        updateCameraEffect(!prefs.value("blur",false).toBool(),prefs.value("blurRadius",12).toInt());
        if(view=="video")renderView();break;
    }
    case 5:openView("general");break;
    }
}
bool DesktopWindow::nativeEvent(const QByteArray& type,void* message,qintptr* result){
    auto* msg=static_cast<MSG*>(message);
    if(msg->message==WM_THEMECHANGED || (msg->message==WM_SETTINGCHANGE && msg->wParam==SPI_SETHIGHCONTRAST))
        QTimer::singleShot(0,this,[this]{styleCaption();});
    // The physical capture fixture needs the same counters while using real devices.
    if(msg->message==WM_APP+219 &&
       (qEnvironmentVariableIsSet("NIMBO_QA_CAPTURE_COUNTERS") ||
        (generatedMedia && (qEnvironmentVariableIsSet("NIMBO_QA_CHAT_CALL_CLOSE") ||
                            qEnvironmentVariableIsSet("NIMBO_QA_VERIFY_PLAYBACK"))))){
        QJsonObject sample{
            {"rows",messages->findChildren<QWidget*>("chatMessage").size()},
            {"messages",lastMessages.size()},{"thumbnailKiB",thumbnails.totalCost()},
            {"audioCards",messages->findChildren<QWidget*>("audioAttachment").size()},
            {"videoCards",messages->findChildren<QWidget*>("videoAttachment").size()},
            {"downloads",imageRequests.size()},{"imageFilesOnDisk",imageFiles.size()},
            {"videoFrames",trackFrames.size()},{"sounds",callSounds.size()},
            {"voiceClient",bool(media)},{"widgets",findChildren<QWidget*>().size()},
            {"settingsPages",findChildren<QWidget*>("settingsPage").size()},
            {"effectCards",findChildren<QWidget*>("effectCard").size()},
            {"audioChecks",findChildren<QWidget*>("audioCheck").size()}
        };
        const auto decoders=findChildren<QMediaPlayer*>("attachmentPlayback");
        sample.insert("attachmentDecoders",decoders.size());
        const auto embeds=findChildren<QWidget*>("youtubeSurface");
        sample.insert("youtubeSurfaces",embeds.size());
        sample.insert("youtubePlaying",std::any_of(embeds.begin(),embeds.end(),[](const auto* w){
            return w->isVisible() && w->accessibleName()==UiText::YoutubeVideoIsPlaying();
        }));
        sample.insert("playingAttachments",int(std::count_if(decoders.begin(),decoders.end(),[](const auto* decoder){
            return decoder->isPlaying();
        })));
        HEAP_SUMMARY heap{};
        heap.cb=sizeof(heap);
        if(HeapSummary(GetProcessHeap(),0,&heap)){
            sample.insert("defaultHeapAllocated",qint64(heap.cbAllocated));
            sample.insert("defaultHeapCommitted",qint64(heap.cbCommitted));
        }
        QSaveFile report(dataPath+"/qa-memory.json");
        if(report.open(QIODevice::WriteOnly)){
            report.write(QJsonDocument(sample).toJson(QJsonDocument::Compact));
            *result=report.commit();return true;
        }
        *result=0;return true;
    }
    if(msg->message==WM_HOTKEY&&boundKeys.contains(int(msg->wParam))&&!QApplication::activeModalWidget()&&!recorder){
        shortcutAction(int(msg->wParam)-1);*result=0;return true;
    }
    return QMainWindow::nativeEvent(type,message,result);
}
void DesktopWindow::endRecording(){
    if(recorder){
        recorder->setText(recorder->property("bindingLabel").toString());
        if(auto* cancel=recorder->parentWidget()->findChild<QPushButton*>("cancelBinding"))cancel->hide();
        recorder.clear();
    }
    pendingModifier={};
}
bool DesktopWindow::shortcutEvent(QObject* watched,QEvent* e){
    if(e->type()==QEvent::WindowDeactivate && watched==this){endRecording();return false;}
    if(e->type()==QEvent::FocusOut && recorder && watched==recorder){endRecording();return false;}
    const bool mousePress=e->type()==QEvent::MouseButtonPress;
    const bool keyboard=e->type()==QEvent::KeyPress || e->type()==QEvent::KeyRelease;
    if(!mousePress&&!keyboard&&e->type()!=QEvent::ShortcutOverride)return false;
    auto* w=qobject_cast<QWidget*>(watched);
    if(!w || w->window()!=this || !isActiveWindow() || pages->currentIndex()!=1 || QApplication::activeModalWidget())return false;
    if(e->type()==QEvent::ShortcutOverride){
        if(!recorder)return false;
        e->accept();return true;
    }
    if(!recorder && (KeyBinding::editing(w)||KeyBinding::editing(QApplication::focusWidget())))return false;
    QJsonObject next;
    if(mousePress){
        pendingModifier={};
        auto* event=static_cast<QMouseEvent*>(e);
        next=binding(KeyBinding::mouse(event->button()),event->modifiers());
    }else{
        auto* event=static_cast<QKeyEvent*>(e);
        if(event->isAutoRepeat())return bool(recorder);
        auto key=KeyBinding::keyboard(*event);
        if(e->type()==QEvent::KeyRelease){
            auto pending=pendingModifier;pendingModifier={};
            if(pending.isEmpty()||pending["code"]!=key)return false;
            next=pending;
        }else{
            pendingModifier={};
            next=binding(key,event->modifiers());
            if(modifier(key)){pendingModifier=next;return bool(recorder);}
        }
    }
    if(next.isEmpty())return false;
    auto bindings=profile["hotkeys"].isObject()?profile["hotkeys"].toObject():defaults();
    if(recorder){
        const QString id=recorder->property("actionId").toString();
        for(int i=0;i<KeyBinding::actions.size();++i)if(KeyBinding::actions[i]!=id && bindings[KeyBinding::actions[i]].toObject()==next){
            note(UiText::ThisShortcutIsAlreadyAssigned()+actionName(i));return true;
        }
        bindings[id]=next;endRecording();profile["hotkeys"]=bindings;saveLocal();bindMute();note({});renderView();return true;
    }
    for(int i=0;i<KeyBinding::actions.size();++i)if(bindings[KeyBinding::actions[i]].toObject()==next && !boundKeys.contains(i+1)){
        shortcutAction(i);return true;
    }
    return false;
}
void DesktopWindow::renderKeys(QVBoxLayout* body){
    const auto bindings=profile["hotkeys"].isObject()?profile["hotkeys"].toObject():defaults();
    body->setSpacing(6);body->addSpacing(8);
    auto* intro=Ui::text(UiText::ShortcutsAreSavedInYourProfileThey(),"font-size:12px;color:#aaa");
    intro->setWordWrap(true);intro->setTextFormat(Qt::RichText);intro->setText("<p style='margin:0;line-height:145%'>"+intro->text().toHtmlEscaped()+"</p>");body->addWidget(intro);body->addSpacing(8);
    for(int i=0;i<KeyBinding::actions.size();++i){
        const auto id=KeyBinding::actions[i];const auto b=bindings[id].toObject();
        auto* card=new QWidget;card->setObjectName("keyBinding");
        card->setStyleSheet("QWidget#keyBinding{background:#151515;border-radius:12px} QPushButton{background:#202020;border:0;border-radius:9px;padding:0 12px;font-size:11px} QPushButton:hover,QPushButton:focus{background:#303030}");
        auto* row=new QHBoxLayout(card);row->setContentsMargins(15,12,15,12);row->setSpacing(12);
        row->addWidget(Ui::text(actionName(i),"font-size:12px;font-weight:500"),1);
        auto* edit=new QPushButton(label(b));edit->setProperty("bindingLabel",edit->text());edit->setProperty("captureButton",true);edit->setProperty("actionId",id);
        edit->setAccessibleDescription(label(b));edit->setFixedHeight(38);edit->setAccessibleName(UiText::Shortcut()+actionName(i));row->addWidget(edit);
        auto* cancel=new QPushButton(UiText::Cancel());cancel->setObjectName("cancelBinding");cancel->setFixedHeight(34);row->addWidget(cancel);cancel->hide();
        auto* erase=Ui::action("close",UiText::RemoveShortcut()+actionName(i),34);row->addWidget(erase);erase->setEnabled(!b.isEmpty());body->addWidget(card);
        connect(edit,&QPushButton::clicked,edit,[this,edit,cancel]{
            endRecording();recorder=edit;removeKeys();edit->setFocus();edit->setText(UiText::PressAKeyOrButton());cancel->show();
        });
        connect(cancel,&QPushButton::clicked,cancel,[this]{endRecording();bindMute();});
        connect(erase,&QPushButton::clicked,erase,[this,id,bindings]()mutable{
            auto updated=bindings;updated.remove(id);profile["hotkeys"]=updated;saveLocal();bindMute();renderView();
        });
    }
    body->addSpacing(16-body->spacing());
    auto* reset=new QPushButton(UiText::RestoreDefaults());Ui::compactButton(reset);body->addWidget(reset,0,Qt::AlignLeft);
    connect(reset,&QPushButton::clicked,reset,[this]{profile["hotkeys"]=defaults();saveLocal();bindMute();renderView();});
}
