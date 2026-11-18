#include "window.h"
#include "version.h"
#include "media_session.h"
#include "ui.h"
#include "profile.h"
#include "message_text.h"
#include "message_input.h"
#include "desktop_updates.h"
#include "server_updates_panel.h"
#include "link_card.h"
#include <QTcpServer>
#include <QTcpSocket>
#include "invitation_settings.h"
#include "background.h"
#include "noise.h"
#include "audio_pcm.h"
#include "voice_card.h"
#include <random>
#include <QtWidgets>
#include <QtTest>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QSoundEffect>
#include <QWindowCapture>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSink>
#include <QCameraDevice>
#include <cstdio>
#include "windows_audio_meter.h"
#include "sound_check_phrase.h"
#include <psapi.h>

class NativeDesktopTests final : public QObject {
    Q_OBJECT
private:
    static void voiceMemoryProbe(DesktopWindow& win,const QString& mode){
        // Optional diagnostic on the disposable localhost server. The entry-point
        // test already verified both joining through the UI and cold SDK restart.
        QVERIFY(win.api.server.host()=="127.0.0.1");
        QVERIFY(mode=="window" || mode=="engine");
        win.timer.stop();
        auto sample=[&](int cycle){
            PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
            QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)));
            qInfo()<<"voice-memory"<<mode<<cycle<<qulonglong(memory.WorkingSetSize)<<qulonglong(memory.PrivateUsage);
        };
        sample(0);
        for(int cycle=1;cycle<=12;++cycle){
            // The real API limits bursts of join requests. Let its rate window expire.
            if(cycle==7)QTest::qWait(61000);
            if(mode=="window"){
                win.join();QTRY_VERIFY_WITH_TIMEOUT(win.inCall,12000);QTest::qWait(250);
                win.leaveCall();QTRY_VERIFY_WITH_TIMEOUT(!win.media,6000);
            }else{
                QJsonObject invitation;QString error;bool done=false;
                win.api.request("POST","/api/join",{{"room",win.selected},{"name","Тестер"}},[&](QJsonObject data,QString issue){
                    invitation=data;error=issue;done=true;
                });
                QTRY_VERIFY_WITH_TIMEOUT(done,4000);QVERIFY2(error.isEmpty(),qPrintable(error));
                MediaOptions options;options.synthetic=true;options.url=invitation["url"].toString();options.token=invitation["token"].toString();
                MediaSession session;
                QSignalSpy state(&session,&VoiceEvents::connectedChanged);session.start(options);
                QTRY_VERIFY_WITH_TIMEOUT(!state.isEmpty() && state.last().first().toBool(),12000);QTest::qWait(250);
                session.stop();QTRY_VERIFY_WITH_TIMEOUT(!state.last().first().toBool(),4000);
            }
            QTest::qWait(400);
            sample(cycle);
        }
    }
    static void messageActions(DesktopWindow& win){
        auto* messages=win.messages;
        auto findRow=[&]{
            for(int i=0;i<messages->count();++i){
                auto* item=messages->item(i);
                if(item->data(Qt::UserRole).toJsonObject()["id"]==win.lastMessages.first().toObject()["id"])
                    return messages->itemWidget(item);
            }
            return static_cast<QWidget*>(nullptr);
        };
        const auto id=win.lastMessages.first().toObject()["id"].toString();
        auto button=[&](QString name)->QPushButton*{
            auto* row=findRow();if(!row)return nullptr;
            QEnterEvent hover(QPointF(4,4),QPointF(4,4),QPointF(row->mapToGlobal(QPoint(4,4))));
            QApplication::sendEvent(row,&hover);
            for(auto* b:row->findChildren<QPushButton*>())
                if(b->accessibleName()==name && b->isVisibleTo(row))return b;
            return nullptr;
        };
        auto lookup=[&]{return win.lastMessages.first().toObject();};
        auto* replyAction=button("Ответить на сообщение");QVERIFY(replyAction);
        auto* row=findRow();row->setFocus(Qt::MouseFocusReason);
        QEvent leaveMessage(QEvent::Leave);QApplication::sendEvent(row,&leaveMessage);
        QVERIFY(!replyAction->isVisibleTo(row));QVERIFY(!messages->verticalScrollBar()->isVisible());
        button("Ответить на сообщение")->click();
        QCOMPARE(win.replyId,id);QVERIFY(win.replyBanner->isVisible());
        win.replyBanner->findChild<QPushButton*>()->click();QVERIFY(win.replyId.isEmpty());
        QVERIFY(button("Добавить реакцию"));button("Добавить реакцию")->click();
        auto* picker=win.findChild<QFrame*>("emojiPicker");QVERIFY(picker);
        auto* search=picker->findChild<QLineEdit*>();QVERIFY(search);search->setText("кофе");
        QCoreApplication::processEvents();auto choose=picker->findChildren<QPushButton*>();
        auto coffee=std::find_if(choose.begin(),choose.end(),[&](auto* b){return b->text()=="☕"&&b->isVisibleTo(picker);});
        QVERIFY(coffee!=choose.end());(*coffee)->click();
        QTRY_VERIFY_WITH_TIMEOUT(lookup()["reactions"].toObject()["☕"].toArray().contains(win.selfId),7000);
        QVERIFY(button("☕: 1. Убрать реакцию"));button("☕: 1. Убрать реакцию")->click();
        QTRY_VERIFY_WITH_TIMEOUT(lookup()["reactions"].toObject()["☕"].toArray().isEmpty(),7000);
        QVERIFY(button("Редактировать сообщение"));button("Редактировать сообщение")->click();
        QTRY_VERIFY(findRow()->findChild<QPlainTextEdit*>("editMessageText"));
        auto* editor=findRow()->findChild<QPlainTextEdit*>("editMessageText");editor->setPlainText("Отменено");
        auto* grip=editor->findChild<QWidget*>("editResizeHandle");QVERIFY(grip);QCOMPARE(editor->height(),90);
        QTest::mousePress(grip,Qt::LeftButton,Qt::NoModifier,{5,5});QTest::mouseMove(grip,{5,37});QTest::mouseRelease(grip,Qt::LeftButton);
        QCOMPARE(editor->height(),122);
        QTest::keyClick(editor,Qt::Key_Escape);
        QTRY_VERIFY(win.editOriginal.isEmpty());
        QTRY_VERIFY(!findRow()->findChild<QPlainTextEdit*>("editMessageText"));
        QTRY_VERIFY(button("Редактировать сообщение"));button("Редактировать сообщение")->click();
        QTRY_VERIFY(findRow()->findChild<QPlainTextEdit*>("editMessageText"));editor=findRow()->findChild<QPlainTextEdit*>("editMessageText");
        QCOMPARE(editor->toPlainText(),lookup()["text"].toString());editor->setPlainText("Исправленный текст");
        editor->selectAll();QTest::keyClick(editor,Qt::Key_I,Qt::ControlModifier);
        QTest::keyClick(editor,Qt::Key_Return,Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(lookup()["text"].toString(),QString("Исправленный текст"),7000);
        QCOMPARE(lookup()["format"].toString(),QString(QStringLiteral("Исправленный текст").size(), '3'));
        QString remove="Удалить сообщение от "+win.self["name"].toString();
        QVERIFY(button(remove));button(remove)->click();QVERIFY(win.deleteBanner->isVisible());win.cancelDelete->click();
        QVERIFY(!lookup()["deleted"].toBool());QVERIFY(button(remove));button(remove)->click();win.confirmDelete->click();
        QTRY_VERIFY_WITH_TIMEOUT(win.lastMessages.isEmpty(),7000);
    }
    static void fileActions(DesktopWindow& win){
        bool ready=false;QString error;
        win.api.request("PATCH","/api/channels/"+win.selected+"/files",{{"enabled",true},{"scan",false}},[&](auto,QString result){error=result;ready=true;});
        QTRY_VERIFY_WITH_TIMEOUT(ready,5000);QVERIFY2(error.isEmpty(),qPrintable(error));
        // Cancel before the server can commit a large attachment; the outbox keeps the draft.
        QTemporaryDir largeDir;QFile large(largeDir.filePath("cancel-upload.bin"));
        QVERIFY(large.open(QIODevice::WriteOnly));QVERIFY(large.resize(32LL*1048576));large.close();
        win.attachFiles({large.fileName()});QTRY_COMPARE(win.fileDrafts[win.selected].size(),1);
        win.sendButton->click();QVERIFY(win.outbox.size()==1);
        auto key=win.outbox.firstKey();QPushButton* cancel=nullptr;
        for(auto* b:win.messages->findChildren<QPushButton*>())if(b->text()=="Отмена")cancel=b;
        QVERIFY(cancel);cancel->click();QTRY_VERIFY(!win.sending);
        QVERIFY(win.outbox.contains(key));QCOMPARE(win.outbox[key].error,QString("Загрузка отменена."));
        QCOMPARE(win.outbox[key].files,QStringList{large.fileName()});
        for(auto* b:win.messages->findChildren<QPushButton*>())if(b->text()=="Убрать"){b->click();break;}
        QTRY_VERIFY(win.outbox.isEmpty());
        const auto path=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../artifacts/oct30-qa/preview-flower.mp4");
        QVERIFY(QFileInfo::exists(path));
        win.attachFiles({path,path});QTRY_COMPARE_WITH_TIMEOUT(win.fileDrafts[win.selected].size(),2,6000);
        auto* remove=win.draftPanel->findChild<QPushButton*>();QVERIFY(remove);remove->click();
        QCOMPARE(win.fileDrafts[win.selected].size(),1);
        win.sendButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(win.outbox.isEmpty(),15000);
        QTRY_VERIFY_WITH_TIMEOUT(win.messages->findChild<QWidget*>("videoAttachment"),8000);
        auto currentCard=[&]()->QWidget*{
            for(int i=0;i<win.messages->count();++i)
                if(auto* row=win.messages->itemWidget(win.messages->item(i)))
                    if(auto* video=row->findChild<QWidget*>("videoAttachment"))return video;
            return nullptr;
        };
        auto decoder=[&]()->QMediaPlayer*{auto* card=currentCard();return card?card->findChild<QMediaPlayer*>("attachmentPlayback"):nullptr;};
        win.messages->scrollToBottom();
        QTRY_VERIFY_WITH_TIMEOUT(currentCard() && currentCard()->property("posterReady").toBool() && !decoder(),8000);
        const auto controls=currentCard()->findChildren<QPushButton*>();
        auto start=std::find_if(controls.begin(),controls.end(),[](auto* b){return b->accessibleName()=="Воспроизвести";});
        QVERIFY(start!=controls.end());QTest::mouseClick(*start,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(decoder() && currentCard()->findChild<QSlider*>()->maximum()>1000 && currentCard()->findChild<QSlider*>()->value()>100,8000);
        auto* sink=decoder()->videoSink();QVERIFY(sink);
        QTRY_VERIFY(sink->videoFrame().isValid());
        auto firstTime=sink->videoFrame().startTime();
        QTRY_VERIFY(sink->videoFrame().isValid() && sink->videoFrame().startTime()>firstTime);
        const auto duration=currentCard()->findChild<QSlider*>()->maximum();
        QTest::mouseClick(currentCard(),Qt::LeftButton);QTRY_VERIFY(!decoder());
        auto* seek=currentCard()->findChild<QSlider*>();QVERIFY(seek);
        seek->sliderMoved(int(duration-600));
        QTest::mouseClick(currentCard(),Qt::LeftButton);QTRY_VERIFY(decoder() && seek->value()>=duration-650);
        QTRY_VERIFY_WITH_TIMEOUT(!decoder() && seek->value()==0,5000);
        QTemporaryDir files;QVERIFY(files.isValid());
        QScopedValueRollback<QString> downloads(win.downloadDirectory,files.path());
        QFile existing(files.filePath("preview-flower.mp4"));QVERIFY(existing.open(QIODevice::WriteOnly));existing.write("keep");existing.close();
        auto directDownload=[](QWidget* parent)->QPushButton*{
            for(auto* b:parent->findChildren<QPushButton*>())if(b->accessibleName()=="Скачать видео")return b;
            return nullptr;
        };
        auto* direct=directDownload(currentCard());QVERIFY(direct && !direct->menu());
        const auto screenshot=qEnvironmentVariable("NIMBO_NATIVE_TEST_SCREENSHOT");
        if(!screenshot.isEmpty()){
            const auto path=QFileInfo(screenshot).dir().filePath("video-download-controls.png");
            QVERIFY(currentCard()->grab().save(path));
        }
        direct->click();
        QPushButton* expand=nullptr;
        for(auto* button:controls)if(button->accessibleName()=="Открыть видео крупно")expand=button;
        QVERIFY(expand);
        expand->click();
        auto* videoViewer=win.attachmentViewer.data();
        QVERIFY(videoViewer && videoViewer->window()==&win && !QApplication::activeModalWidget());
        auto* expandedCard=videoViewer->findChild<QWidget*>("videoAttachment");QVERIFY(expandedCard);
        QTest::mouseClick(expandedCard,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(expandedCard->findChild<QMediaPlayer*>() && expandedCard->findChild<QMediaPlayer*>()->position()>150,8000);
        auto* downloadVideo=directDownload(videoViewer);QVERIFY(downloadVideo && !downloadVideo->menu());downloadVideo->click();
        QTest::keyClick(videoViewer,Qt::Key_Escape);
        QTRY_VERIFY(!win.attachmentViewer);
        QFile original(path);QVERIFY(original.open(QIODevice::ReadOnly));const auto expectedVideo=original.readAll();
        for(int number:{1,2}){
            QFile saved(files.filePath(QString("preview-flower (%1).mp4").arg(number)));
            QTRY_VERIFY2_WITH_TIMEOUT(saved.exists(),qPrintable(win.status->text()+" | "+QDir(files.path()).entryList().join(", ")),8000);QVERIFY(saved.open(QIODevice::ReadOnly));
            QCOMPARE(saved.readAll(),expectedVideo);
        }
        auto filesInChat=win.lastMessages.last().toObject()["attachments"].toArray();
        if(filesInChat.isEmpty())filesInChat.append(win.lastMessages.last().toObject()["attachment"]);
        QCOMPARE(filesInChat.size(),1);QVERIFY(filesInChat.first().toObject()["name"].toString().endsWith(".mp4"));
        filesInChat.append(filesInChat.first());win.viewAttachments(filesInChat,0);
        QTRY_VERIFY_WITH_TIMEOUT(win.attachmentViewer && win.attachmentViewer->findChild<QMediaPlayer*>() &&
            win.attachmentViewer->findChild<QMediaPlayer*>()->position()>150,8000);
        QPushButton* nextFile=nullptr;
        for(auto* button:win.attachmentViewer->findChildren<QPushButton*>())
            if(button->accessibleName()=="Следующее вложение")nextFile=button;
        QVERIFY(nextFile && nextFile->isVisible());QTest::mouseClick(nextFile,Qt::LeftButton);
        auto* nextCard=win.attachmentViewer->findChild<QWidget*>("videoAttachment");QVERIFY(nextCard);
        QTRY_VERIFY(nextCard->property("posterReady").toBool());QVERIFY(!nextCard->findChild<QMediaPlayer*>("attachmentPlayback"));
        QTest::keyClick(win.attachmentViewer,Qt::Key_Escape);QTRY_VERIFY(!win.attachmentViewer);
        QVERIFY(existing.open(QIODevice::ReadOnly));QCOMPARE(existing.readAll(),QByteArray("keep"));

        QStringList imagePaths;
        for(const auto& color:{QColor(Qt::red),QColor(Qt::blue)}){
            QImage image(64,48,QImage::Format_RGB32);image.fill(color);
            auto file=files.filePath(QString::number(imagePaths.size())+".png");QVERIFY(image.save(file));imagePaths<<file;
        }
        QJsonArray uploaded;QString photoMessage;ready=false;
        win.api.upload("/api/channels/"+win.selected+"/files",imagePaths,{{"clientId",QString::fromLatin1(DesktopProfile::randomKey())}},[&](QJsonObject msg,QString err){
            uploaded=msg["attachments"].toArray();photoMessage=msg["id"].toString();error=err;ready=true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(ready,8000);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(uploaded.size(),2);
        for(int i=0;i<2;++i){
            const auto route="/api/files/"+uploaded[i].toObject()["id"].toString();
            QFile file(imagePaths[i]);QVERIFY(file.open(QIODevice::ReadOnly));const auto expected=file.readAll();
            int completed=0;
            auto check=[&](const QString& path){QFile cached(path);QVERIFY(cached.open(QIODevice::ReadOnly));QCOMPARE(cached.readAll(),expected);++completed;};
            win.loadImage(route,check);win.loadImage(route,check);
            QTRY_COMPARE_WITH_TIMEOUT(completed,2,5000);
            win.loadImage(route,check);QCOMPARE(completed,3);
        }
        win.viewAttachments(uploaded,0);
        {
            auto* viewer=win.attachmentViewer.data();
            QVERIFY(viewer && viewer->window()==&win && !QApplication::activeModalWidget());
            const auto oldPosition=win.pos();
            const auto oldSize=win.size();
            win.move(oldPosition+QPoint(32,24));win.resize(oldSize-QSize(80,40));
            QCoreApplication::processEvents();
            QCOMPARE(viewer->geometry(),win.pages->contentsRect());
            QVERIFY(win.isEnabled());QCOMPARE(win.pos(),oldPosition+QPoint(32,24));
            win.move(oldPosition);win.resize(oldSize);
            auto visibleColor=[&]{
                for(auto* label:viewer->findChildren<QLabel*>()){
                    const auto pix=label->pixmap();
                    if(!pix.isNull())return pix.toImage().pixelColor(pix.width()/2,pix.height()/2);
                }
                return QColor();
            };
            QCOMPARE(visibleColor(),QColor(Qt::red));
            for(auto* button:viewer->findChildren<QPushButton*>())if(button->accessibleName()=="Следующее вложение")button->click();
            QCOMPARE(visibleColor(),QColor(Qt::blue));
            for(auto* button:viewer->findChildren<QPushButton*>())if(button->accessibleName()=="Предыдущее вложение")button->click();
            QCOMPARE(visibleColor(),QColor(Qt::red));
            for(auto* button:viewer->findChildren<QPushButton*>())if(button->accessibleName()=="Закрыть просмотр"){QVERIFY(button->isVisibleTo(viewer));button->click();break;}
        }
        QVERIFY(!win.attachmentViewer);
        for(const auto& photo:uploaded){
            const auto id=photo.toObject()["id"].toString();
            const QString key=win.api.server.toString()+"/"+win.selfId+"/api/files/"+id;
            const auto* cached=win.imageFiles.object(key);QVERIFY(cached && !(*cached)->isOpen() && QFileInfo::exists((*cached)->fileName()));
        }
        // A new API instance simulates a cold client with only the account's disk cache.
        const auto photoId=uploaded.first().toObject()["id"].toString();
        const auto route="/api/files/"+photoId;
        ServerApi cold;
        cold.token=win.api.token;cold.server=win.api.server;
        cold.syncMedia(win.dataPath+"/media-cache-v1",win.selfId);
        QTRY_VERIFY_WITH_TIMEOUT(!cold.posterPath(route).isEmpty(),3000);
        bool hit=false;
        cold.download(route,files.filePath("cold.png"),[&](QString err){hit=err.isEmpty();});
        QVERIFY(hit); // Synchronous disk hit proves no HTTP transfer.
        QCOMPARE(QImage(files.filePath("cold.png")).pixelColor(0,0),QColor(Qt::red));
        QTemporaryDir offlineStore;
        QDirIterator diskFiles(win.dataPath+"/media-cache-v1",{"*.bin"},QDir::Files,QDirIterator::Subdirectories);
        QString offlinePath;
        const auto leaf=QByteArray::fromBase64(photoId.toLatin1(),QByteArray::Base64UrlEncoding).toHex()+".bin";
        while(diskFiles.hasNext()){
            diskFiles.next();if(diskFiles.fileName()!=leaf)continue;
            auto folder=offlineStore.path()+"/"+diskFiles.fileInfo().dir().dirName();
            QVERIFY(QDir().mkpath(folder));offlinePath=folder+"/"+leaf;
            QVERIFY(QFile::copy(diskFiles.filePath(),offlinePath));break;
        }
        QVERIFY(!offlinePath.isEmpty());
        cold.reset(); // Offline during deletion. The other client has the gallery open.
        win.viewAttachments(uploaded,0);
        QVERIFY(win.attachmentViewer);
        bool deleted=false;
        ServerApi actor;actor.server=win.api.server;actor.token=win.api.token;
        actor.request("DELETE","/api/messages/"+photoMessage+"?channel="+win.selected,{},[&](QJsonObject,QString err){deleted=err.isEmpty();});
        QTRY_VERIFY_WITH_TIMEOUT(deleted,2000);
        QTRY_VERIFY_WITH_TIMEOUT(!win.attachmentViewer && win.api.posterPath(route).isEmpty(),1000);
        cold.server=win.api.server;cold.token=win.api.token;
        cold.syncMedia(offlineStore.path(),win.selfId);
        bool rejected=false;
        cold.download(route,files.filePath("stale.png"),[&](QString err){rejected=!err.isEmpty();});
        QTRY_VERIFY_WITH_TIMEOUT(rejected && !QFile::exists(offlinePath),2000);
        QVERIFY(!QFile::exists(files.filePath("stale.png")));
        QFile sound(files.filePath("audio-test.wav"));QVERIFY(sound.open(QIODevice::WriteOnly));
        QDataStream wave(&sound);wave.setByteOrder(QDataStream::LittleEndian);
        const QByteArray silence(48000,0); // Three seconds of mono 8 kHz PCM.
        wave.writeRawData("RIFF",4);wave<<quint32(36+silence.size());wave.writeRawData("WAVEfmt ",8);
        wave<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(8000)<<quint32(16000)<<quint16(2)<<quint16(16);
        wave.writeRawData("data",4);wave<<quint32(silence.size());sound.write(silence);sound.close();
        win.attachFiles({sound.fileName()});QTRY_COMPARE_WITH_TIMEOUT(win.fileDrafts[win.selected].size(),1,6000);
        win.sendButton->click();QTRY_VERIFY_WITH_TIMEOUT(win.outbox.isEmpty(),10000);win.messages->scrollToBottom();
        QWidget* audio=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((audio=win.messages->findChild<QWidget*>("audioAttachment")),7000);
        audio->grab(); // Trigger the same lazy load as the first paint of a visible message.
        auto* timeline=audio->findChild<QSlider*>();QVERIFY(timeline);
        QPushButton* play=nullptr;
        for(auto* b:audio->findChildren<QPushButton*>())if(b->accessibleName().startsWith("Воспроизвести"))play=b;
        QVERIFY(play);
        auto audioDecoder=[audio]{return audio->findChild<QMediaPlayer*>("attachmentPlayback");};
        QTRY_VERIFY_WITH_TIMEOUT(timeline->maximum()==3000 && !audioDecoder(),6000);
        play->click();QTRY_VERIFY_WITH_TIMEOUT(audioDecoder() && timeline->value()>150,5000);
        play->click();QVERIFY(!audioDecoder());QVERIFY(timeline->value()>0);
        timeline->setValue(2200);play->click();
        QTRY_VERIFY_WITH_TIMEOUT(audioDecoder() && timeline->value()>=2200,3000);
        QTRY_VERIFY_WITH_TIMEOUT(!audioDecoder() && timeline->value()==0,5000);
        QCOMPARE(timeline->maximum(),3000);
        QList<QPointer<QWidget>> previousRows;
        for(auto* widget:win.messages->findChildren<QWidget*>()){
            if(widget->objectName()=="chatMessage" || widget->objectName()=="videoAttachment" || widget->objectName()=="audioAttachment")
                previousRows.append(widget);
        }
        QVERIFY(!previousRows.isEmpty());
        const int chatRow=win.channels->currentRow();
        for(int i=0;i<win.channels->count();++i){
            if(win.channels->item(i)->data(Qt::UserRole).toJsonObject()["kind"]=="voice"){
                win.channels->setCurrentRow(i);
                break;
            }
        }
        QVERIFY(win.channels->currentRow()!=chatRow);
        QTRY_VERIFY(std::all_of(previousRows.cbegin(),previousRows.cend(),[](const auto& row){return row.isNull();}));
        QVERIFY(win.thumbnails.isEmpty());
        QVERIFY(win.messages->findChildren<QMediaPlayer*>().isEmpty());
        win.channels->setCurrentRow(chatRow);
        QTRY_VERIFY(!win.lastMessages.isEmpty());
    }
    static void channelChanges(DesktopWindow& win){
        win.openView("channels");
        auto button=[&](const QString& key){
            for(auto* b:win.findChildren<QPushButton*>())if(b->isVisibleTo(&win)&&(b->accessibleName()==key || b->text()==key))return b;
            return static_cast<QPushButton*>(nullptr);
        };
        auto field=[&](const QString& key){
            for(auto* e:win.findChildren<QLineEdit*>())if(e->isVisibleTo(&win)&&e->accessibleName()==key)return e;
            return static_cast<QLineEdit*>(nullptr);
        };
        auto click=[&](QString key){QTRY_VERIFY_WITH_TIMEOUT(button(key),5000);button(key)->click();QCoreApplication::processEvents();};
        auto testMove=[&](QString source,QString destination,bool section){
            auto readOrder=[&]{
                QStringList result;
                for(auto v:win.workspace[section?"sectionOrder":"channels"].toArray())
                    result.append(section?v.toString():v.toObject()["id"].toString());
                return result;
            };
            auto* handle=button(source);auto* target=button(destination);QVERIFY(handle);QVERIFY(target);
            const auto original=readOrder();
            auto drag=[&]{
                handle=button(source);target=button(destination);QVERIFY(handle);QVERIFY(target);
                const auto global=target->mapToGlobal(QPoint(target->width()/2,2));
                QTest::mousePress(handle,Qt::LeftButton);
                QMouseEvent motion(QEvent::MouseMove,QPointF(handle->mapFromGlobal(global)),QPointF(global),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(handle,&motion);
                auto* ghost=win.findChild<QFrame*>("channelDragGhost");QVERIFY(ghost);QVERIFY(ghost->isVisibleTo(&win));
            };
            drag();QTest::keyClick(handle,Qt::Key_Escape);QTest::mouseRelease(handle,Qt::LeftButton);
            QVERIFY(!win.findChild<QFrame*>("channelDragGhost"));QCOMPARE(readOrder(),original);
            drag();QTest::mouseRelease(handle,Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(readOrder()!=original,4000);
            handle=button(source);QVERIFY(handle);QTest::keyClick(handle,Qt::Key_Down);
            QTRY_COMPARE_WITH_TIMEOUT(readOrder(),original,4000);
        };
        click("Добавить секцию");
        QComboBox* sectionType=nullptr;
        for(auto* box:win.findChildren<QComboBox*>())if(box->isVisibleTo(&win)&&box->accessibleName()=="Тип секции")sectionType=box;
        QVERIFY(sectionType);sectionType->setCurrentIndex(sectionType->findData("text"));
        QVERIFY(field("Название новой секции"));field("Название новой секции")->setText("Пробная секция");click("Создать");
        QTRY_VERIFY_WITH_TIMEOUT(button("Добавить канал: Пробная секция"),5000);
        click("Добавить канал: Пробная секция");QVERIFY(field("Название нового канала"));field("Название нового канала")->setText("Пробный канал");click("Создать");
        QTRY_VERIFY_WITH_TIMEOUT(button("Изменить Пробный канал"),5000);click("Изменить Пробный канал");
        auto* picker=button("Выбрать иконку канала");QVERIFY(picker);
        const auto originalIcon=picker->property("iconId");
        click("Выбрать иконку канала");
        QEvent deactivate(QEvent::WindowDeactivate);
        QCoreApplication::sendEvent(&win,&deactivate);
        auto* coffee=button("Кофе");QVERIFY(coffee);QVERIFY(!coffee->rect().isEmpty());
        QTest::mouseClick(coffee,Qt::LeftButton);
        QCOMPARE(picker->property("iconId").toString(),QString("coffee"));QVERIFY(!button("Кофе"));
        click("Отменить изменения канала");click("Изменить Пробный канал");
        picker=button("Выбрать иконку канала");QVERIFY(picker);QCOMPARE(picker->property("iconId"),originalIcon);
        click("Выбрать иконку канала");click("Кофе");
        auto* posting=button("Кто может писать");QVERIFY(posting);QCOMPARE(posting->text(),QString("Пишут все"));
        click("Кто может писать");
        auto visibleCheck=[&](QString role){
            for(auto* box:win.findChildren<QCheckBox*>())if(box->isVisibleTo(&win)&&box->property("roleId").isValid()&&box->property("roleId").toString()==role)return box;
            return static_cast<QCheckBox*>(nullptr);
        };
        auto* everyone=visibleCheck({});QVERIFY(everyone);QVERIFY(everyone->isChecked());
        everyone->click();QCOMPARE(posting->text(),QString("Только владелец"));
        auto* admin=visibleCheck("admin");QVERIFY(admin);admin->click();QCOMPARE(posting->text(),QString("Пишут роли · 1"));
        QTest::keyClick(admin,Qt::Key_Escape);QVERIFY(!admin->isVisibleTo(&win));
        QVERIFY(field("Название канала Пробный канал"));field("Название канала Пробный канал")->setText("Новое имя");click("Сохранить канал");
        click("Изменить Новое имя");picker=button("Выбрать иконку канала");QVERIFY(picker);
        QCOMPARE(picker->property("iconId").toString(),QString("coffee"));
        QCOMPARE(button("Кто может писать")->text(),QString("Пишут роли · 1"));click("Кто может писать");
        admin=visibleCheck("admin");QVERIFY(admin);QVERIFY(admin->isChecked());
        everyone=visibleCheck({});QVERIFY(everyone);everyone->click();QVERIFY(!admin->isChecked());click("Отменить изменения канала");
        QTRY_VERIFY_WITH_TIMEOUT(button("Удалить Новое имя"),5000);click("Удалить Новое имя");click("Отмена");QVERIFY(button("Изменить Новое имя"));
        click("Удалить Новое имя");click("Удалить канал");QTRY_VERIFY_WITH_TIMEOUT(button("Удалить пустую секцию Пробная секция"),5000);
        click("Удалить пустую секцию Пробная секция");QTRY_VERIFY_WITH_TIMEOUT(!button("Изменить секцию Пробная секция"),5000);
        const auto groups=win.workspace["sectionOrder"].toArray();
        QVERIFY(groups.size()>=2);
        auto groupName=[&](int n){return QString("Переместить секцию ")+Ui::byId(win.workspace["sections"].toArray(),groups[n].toString())["name"].toString();};
        testMove(groupName(1),groupName(0),true);
        QStringList voices;
        for(auto v:win.workspace["channels"].toArray()){
            auto channel=v.toObject();
            if(channel["kind"]=="voice")voices.append("Переместить канал "+channel["name"].toString());
        }
        QVERIFY(voices.size()>=2);testMove(voices[1],voices[0],false);
        win.openView("room");
    }
    static void longChatReactions(DesktopWindow& win){
        constexpr int count=45;
        int pending=count;QString error;
        for(int i=0;i<count;++i)
            win.api.request("POST","/api/messages",{{"channel",win.selected},{"clientId",QString::fromLatin1(DesktopProfile::randomKey())},{"text",QString("Прокрутка %1").arg(i)}},
                [&](QJsonObject,QString err){if(!err.isEmpty())error=err;--pending;});
        QTRY_COMPARE_WITH_TIMEOUT(pending,0,10000);QVERIFY2(error.isEmpty(),qPrintable(error));
        win.loadMessages();QTRY_VERIFY_WITH_TIMEOUT(win.messages->count()>=count,7000);
        auto* list=win.messages;auto* bar=list->verticalScrollBar();bar->setValue(bar->maximum()/2+5);
        auto* first=list->itemAt(1,0);QVERIFY(first);
        const int row=list->row(first);QVERIFY(row>0);
        const auto id=list->item(row+2)->data(Qt::UserRole).toJsonObject()["id"].toString();
        const auto pinned=first->data(Qt::UserRole).toJsonObject()["id"].toString();
        const int y=list->visualItemRect(first).top();
        auto check=[&]{
            const auto* now=list->itemAt(1,0);QVERIFY(now);QCOMPARE(now->data(Qt::UserRole).toJsonObject()["id"].toString(),pinned);
            QCOMPARE(list->visualItemRect(now).top(),y);
        };
        win.toggleReaction(id,"☕");
        QTRY_VERIFY_WITH_TIMEOUT(Ui::byId(win.lastMessages,id)["reactions"].toObject()["☕"].toArray().contains(win.selfId),7000);check();
        auto* target=list->itemWidget(list->item(row+2));QVERIFY(target);
        QPushButton* undo=nullptr;
        for(auto* b:target->findChildren<QPushButton*>())if(b->accessibleName().contains("Убрать реакцию"))undo=b;
        QVERIFY(undo);QTest::mouseClick(undo,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(Ui::byId(win.lastMessages,id)["reactions"].toObject()["☕"].toArray().isEmpty(),7000);check();
        win.refresh();QTest::qWait(250);check();
    }
    static void invitations(DesktopWindow& win){
        win.openView("roles");
        auto* section=win.detailLayout->itemAt(0)->widget()->findChild<QWidget*>("invitationSettings");
        QVERIFY(section);QTRY_VERIFY(section->isVisible());
        auto visibleButton=[&](const QString& name){
            for(auto* b:section->findChildren<QPushButton*>())
                if(b->isVisibleTo(section) && b->text()==name)return b;
            return static_cast<QPushButton*>(nullptr);
        };
        auto* type=section->findChild<QComboBox*>();QVERIFY(type);
        QVERIFY(!type->isVisible());QVERIFY(visibleButton("Создать приглашение"));
        visibleButton("Создать приглашение")->click();
        QTRY_VERIFY(type->isVisible());QCOMPARE(type->count(),6);
        auto* label=section->findChild<QLineEdit*>();QVERIFY(label);QCOMPARE(label->maxLength(),80);
        label->setText("Черновик");
        type->setCurrentIndex(1);visibleButton("Отмена")->click();QVERIFY(!type->isVisible());
        auto* open=visibleButton("Создать приглашение");QVERIFY(open);open->click();QCOMPARE(type->currentData().toInt(),3600);
        auto* submit=section->findChild<QPushButton*>("inviteSubmit");QVERIFY(submit);
        auto ids=[&]{
            QSet<QString> result;
            for(const auto v:win.workspace["invitations"].toArray())result.insert(v.toObject()["id"].toString());
            return result;
        };
        QVERIFY(label->text().isEmpty());label->setText("  Для команды  ");
        const auto before=ids();submit->click();QTRY_COMPARE(ids().size(),before.size()+1);QVERIFY(!type->isVisible());
        const auto added=ids()-before;const auto item=Ui::byId(win.workspace["invitations"].toArray(),*added.begin());
        QCOMPARE(item["singleUse"].toBool(),false);QVERIFY(item["expiresAt"].toInteger()>item["createdAt"].toInteger());
        QCOMPARE(item["label"].toString(),QString("Для команды"));
        auto* list=section->findChild<QScrollArea*>();QVERIFY(list);
        auto* firstCard=list->widget()->layout()->itemAt(0)->widget();QVERIFY(firstCard);
        QTRY_VERIFY(list->viewport()->height()>=qMin(400,firstCard->sizeHint().height()));
        QTRY_VERIFY(firstCard->visibleRegion().contains(firstCard->rect().center()));
        QStringList labels;for(auto* caption:firstCard->findChildren<QLabel*>())labels.append(caption->text());
        QVERIFY(labels.contains("Для команды"));QVERIFY(labels.contains("Многоразовое"));QVERIFY(labels.contains("Только что создано"));
        QVERIFY(labels.join(" ").contains("Создано "));QVERIFY(labels.join(" ").contains("До "));
        bool matches=false;
        for(auto* text:section->findChildren<QPlainTextEdit*>()){
            const auto lines=text->toPlainText().split(QChar::LineFeed);
            if(lines.size()==4 && lines.last().endsWith(item["code"].toString()))matches=text->isReadOnly() && !text->toPlainText().contains(item["label"].toString());
        }
        QVERIFY(matches);
        QPushButton* revoke=nullptr;
        for(auto* b:section->findChildren<QPushButton*>())if(b->accessibleName()=="Отозвать приглашение "+*added.begin())revoke=b;
        QVERIFY(revoke);revoke->click();QTRY_COMPARE(ids(),before);
        win.openView("room");
    }
    static void memberSettings(DesktopWindow& owner,const QString& memberId){
        owner.refresh();
        QTRY_VERIFY(!Ui::byId(owner.workspace["members"].toArray(),memberId).isEmpty());
        owner.openView("roles");
        auto* section=owner.detailLayout->itemAt(0)->widget()->findChild<QWidget*>("memberSettings");
        QVERIFY(section);QTRY_VERIFY(section->isVisible());
        auto* search=section->findChild<QLineEdit*>();QVERIFY(search);
        auto findButton=[&](const QString& text){
            for(auto* b:section->findChildren<QPushButton*>())if(b->isVisibleTo(section)&&b->text()==text)return b;
            return static_cast<QPushButton*>(nullptr);
        };
        auto hasText=[&](const QString& text){
            for(auto* l:section->findChildren<QLabel*>())if(l->isVisibleTo(section)&&l->text()==text)return true;
            return false;
        };
        search->setText("неизвестный участник");
        QVERIFY(hasText("Найдено: 0"));QVERIFY(hasText("Никого не нашли. Попробуйте другое имя."));
        search->setText(owner.self["name"].toString());
        QVERIFY(hasText("Найдено: 1"));QVERIFY(!findButton("Роли"));QVERIFY(!findButton("Заблокировать"));
        search->setText("  сОБеСеД  ");QVERIFY(hasText("Найдено: 1"));
        QTRY_VERIFY(findButton("Заблокировать"));findButton("Заблокировать")->click();
        QTRY_VERIFY(findButton("Отмена"));findButton("Отмена")->click();
        QVERIFY(!findButton("Подтвердить"));
        QTRY_VERIFY(findButton("Роли"));findButton("Роли")->click();
        auto* panel=section->findChild<QFrame*>("rolePopup");QVERIFY(panel);
        auto* role=panel->findChild<QCheckBox*>();QVERIFY(role);QVERIFY(role->isEnabled());
        const auto label=role->text();QString key;
        for(const auto item:owner.workspace["roles"].toArray())
            if(item.toObject()["name"]==label)key=item.toObject()["id"].toString();
        QVERIFY(!key.isEmpty());
        const bool original=role->isChecked();
        auto assigned=[&]{return Ui::byId(owner.workspace["members"].toArray(),memberId)["roleIds"].toArray().contains(key);};
        role->click();QTRY_COMPARE(assigned(),!original);QTRY_VERIFY(role->isEnabled());
        role->click();QTRY_COMPARE(assigned(),original);panel->close();
        QTRY_VERIFY(findButton("Заблокировать"));findButton("Заблокировать")->click();
        QTRY_VERIFY(findButton("Подтвердить"));
        for(auto* box:section->findChildren<QComboBox*>())if(box->isVisibleTo(section)){
            QCOMPARE(box->count(),6);box->setCurrentIndex(1);
        }
        findButton("Подтвердить")->click();
        QTRY_VERIFY(Ui::byId(owner.workspace["members"].toArray(),memberId)["blocked"].toBool());
        QVERIFY(Ui::byId(owner.workspace["members"].toArray(),memberId)["blockedUntil"].toInteger()>0);
        QTRY_VERIFY(findButton("Разблокировать"));findButton("Разблокировать")->click();
        QTRY_VERIFY(findButton("Подтвердить"));findButton("Подтвердить")->click();
        QTRY_VERIFY(!Ui::byId(owner.workspace["members"].toArray(),memberId)["blocked"].toBool());
        owner.openView("room");
    }
    static void stageBadLocalRelease(DesktopWindow& win){
        // Exercise the real download and integrity checks without a published release or restarting the test runner.
        QCOMPARE(win.api.server.host(),QString("127.0.0.1"));auto* updater=win.updates;
        QVERIFY(!updater->working);updater->asset=win.api.server.resolved(QUrl("/healthz"));
        updater->expected=8192;updater->digest=QString(64,'0');updater->version="9.9.9";updater->refresh();
    }
    static void generalSettings(DesktopWindow& win){
        win.openView("general");
        QLineEdit *quota=nullptr,*file=nullptr;QPushButton* save=nullptr;
        for(auto* field:win.detailPage->findChildren<QLineEdit*>()){
            if(field->accessibleName().startsWith("Лимит хранения"))quota=field;
            if(field->accessibleName().startsWith("Макс. размер файла"))file=field;
        }
        for(auto* b:win.detailPage->findChildren<QPushButton*>())if(b->accessibleName()=="Обновить лимиты")save=b;
        QVERIFY(quota && file && save);QTRY_VERIFY(file->isEnabled());
        QCOMPARE(file->size(),QSize(68,38));QCOMPARE(quota->size(),file->size());
        const auto edit=[](QLineEdit* field,QString value){field->selectAll();QTest::keyClicks(field,value);};
        edit(quota,"1");edit(file,"820");save->click();QVERIFY(save->isEnabled());
        edit(file,"819");save->click();QTRY_VERIFY(file->isEnabled() && !save->isEnabled());
        bool checkedLimits=false;win.api.request("GET","/api/storage",{},[&](QJsonObject data,QString issue){
            const auto p=data["policy"].toObject();checkedLimits=issue.isEmpty()&&p["limitBytes"].toInteger()==1073741824LL&&p["maxFileBytes"].toInteger()==819LL*1048576;
        });QTRY_VERIFY(checkedLimits);
        const auto output=qEnvironmentVariable("NIMBO_SETTINGS_SHOTS");
        if(!output.isEmpty()){
            QDir().mkpath(output);auto size=win.size();
            for(int width:{1264,730}){
                win.resize(width,784);
                for(auto view:{"general","audio","keys"}){
                    win.openView(view);QTest::qWait(300);
                    auto* section=win.detailLayout->itemAt(0)->widget()->findChild<QWidget*>("settingsSection");QVERIFY(section);
                    QVERIFY(section->grab().save(output+QString("/%1-%2.png").arg(view).arg(width)));
                }
            }
            win.resize(size);win.openView("general");
        }
        const QString inbox=qEnvironmentVariable("NIMBO_QA_UPDATE_INBOX");
        if(!inbox.isEmpty()){
            QVERIFY(win.api.server.host()=="127.0.0.1");
            auto* panel=win.detailPage->findChild<QWidget*>("serverUpdates");QVERIFY(panel);
            QPushButton *check=nullptr,*install=nullptr;
            for(auto* b:panel->findChildren<QPushButton*>()){
                if(b->text()=="Проверить обновления")check=b;
                if(b->text()=="Обновить")install=b;
            }
            QVERIFY(check && install);QTRY_VERIFY(install->isVisibleTo(panel));
            auto* scroll=win.detailPage->findChild<QScrollArea*>("settingsScroll");
            auto* nav=win.detailPage->findChild<QWidget*>("settingsNavPanel");QVERIFY(scroll && nav);
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            const auto navAt=nav->mapTo(&win,QPoint());
            check->click();
            const QString marker=inbox+"/check";QTRY_VERIFY(QFile::exists(marker));
            QVERIFY(!QFile::exists(inbox+"/install"));QTRY_VERIFY(!install->isVisibleTo(panel));
            QVERIFY(QFile::remove(marker));QTRY_VERIFY_WITH_TIMEOUT(install->isVisibleTo(panel),3000);
            QCOMPARE(nav->mapTo(&win,QPoint()),navAt);QVERIFY(!nav->visibleRegion().isEmpty());
            stageBadLocalRelease(win);
            install->click();QTRY_VERIFY(QFile::exists(inbox+"/install"));
            QTRY_VERIFY(win.updates->status().contains("Контрольная сумма"));win.updates->asset.clear();win.updates->refresh();
            QFile request(inbox+"/install");QVERIFY(request.open(QIODevice::ReadOnly));QCOMPARE(request.readAll(),QByteArray("server-v1.2.3"));request.close();
            QVERIFY(QFile::remove(inbox+"/install"));
            QPushButton* rollback=nullptr;for(auto* b:panel->findChildren<QPushButton*>())if(b->text().startsWith("Откатиться на server-v1.2.2"))rollback=b;
            QVERIFY(rollback);QTRY_VERIFY_WITH_TIMEOUT(rollback->isEnabled(),6500);rollback->click();
            QTRY_VERIFY(request.exists());QVERIFY(request.open(QIODevice::ReadOnly));QCOMPARE(request.readAll(),QByteArray("rollback:server-v1.2.2"));request.close();QVERIFY(QFile::remove(inbox+"/install"));
        }
        auto* card=win.detailPage->findChild<QWidget*>("antivirusSettings");QVERIFY(card);
        auto* policy=card->findChild<QCheckBox*>();QTRY_VERIFY(policy && policy->isVisible());
        auto has=[&](QString value){for(auto* label:card->findChildren<QLabel*>())if(label->isVisible()&&label->text().contains(value))return true;return false;};
        QVERIFY(has("Версия ClamAV · нет данных"));QVERIFY(policy->isChecked());
        policy->click();QTRY_VERIFY(has("Изменение сохранено"));QVERIFY(!policy->isChecked());
        policy->click();QTRY_VERIFY(has("Настройки не изменены"));QVERIFY(!policy->isChecked());
        auto* update=card->findChild<QPushButton*>();QVERIFY(update);QVERIFY(!update->isEnabled());
        win.openView("room");
    }
public:
    static bool fontSamples(const QString& directory){
        QTemporaryDir profileDir;
        if(!profileDir.isValid() || !QDir().mkpath(directory))return false;
        QApplication::setStyle("Fusion");
        DesktopWindow win(profileDir.path());
        win.setAttribute(Qt::WA_DontShowOnScreen);win.resize(1264,784);win.show();
        win.pages->setCurrentIndex(1);win.messages->show();
        win.lastMessages=QJsonArray{QJsonObject{{"id","sample"},{"name","Автор"},{"text","Проверка переписки"}}};
        win.updateMessageRows();QApplication::processEvents();
        auto* body=win.messages->findChild<QLabel*>("messageBody");
        if(!body)return false;
        for(const auto& sample:{QStringLiteral("latin"),QStringLiteral("cyrillic"),QStringLiteral("chat")}){
            auto* widget=sample=="chat"?body:win.profileName;
            if(sample!="chat")widget->setText(sample=="latin"?"he he":"Тестер");
            const auto original=widget->font();
            auto resolved=QRawFont::fromFont(original);
            QRawFont bundled(QCoreApplication::applicationDirPath()+QString("/assets/Roboto-%1.ttf").arg(sample=="chat"?400:500),original.pixelSize());
            qInfo()<<sample<<resolved.familyName()<<resolved.styleName()<<original.pixelSize()
                   <<"same outlines"<<(resolved.isValid() && bundled.isValid() && resolved.fontTable("glyf")==bundled.fontTable("glyf"));
            for(auto hint:{QFont::PreferDefaultHinting,QFont::PreferNoHinting,QFont::PreferVerticalHinting,QFont::PreferFullHinting}){
                auto font=original;font.setHintingPreference(hint);widget->setFont(font);
                const QRect bounds(widget->mapTo(&win,QPoint(-2,0)),QSize(sample=="chat"?157:47,widget->height()));
                if(!win.grab(bounds).save(directory+"/"+sample+"-"+QString::number(hint)+".png"))return false;
            }
            for(const auto strategy:{QFont::PreferAntialias,QFont::NoSubpixelAntialias,QFont::PreferQuality}){
                auto font=original;font.setStyleStrategy(strategy);widget->setFont(font);
                const QRect bounds(widget->mapTo(&win,QPoint(-2,0)),QSize(sample=="chat"?157:47,widget->height()));
                if(!win.grab(bounds).save(directory+"/"+sample+"-"+QString::number(int(strategy)+10)+".png"))return false;
            }
            widget->setFont(original);
        }
        return true;
    }
private slots:
    void updateSurvivesSettingsAndCall_data(){
        QTest::addColumn<bool>("disconnectFirst");
        QTest::newRow("continuous")<<false;
        QTest::newRow("connection-interrupted")<<true;
    }
    void updateSurvivesSettingsAndCall(){
        QFETCH(bool,disconnectFirst);
        QTcpServer source;QVERIFY(source.listen(QHostAddress::LocalHost));
        int requests=0;
        connect(&source,&QTcpServer::newConnection,&source,[&]{
            auto* socket=source.nextPendingConnection();
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                if(socket->property("started").toBool()){socket->readAll();return;}
                socket->readAll();socket->setProperty("started",true);++requests;
                socket->setProperty("disconnectFirst",disconnectFirst && requests==1);
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 1048576\r\nConnection: close\r\n\r\n");
                auto* chunks=new QTimer(socket);chunks->setInterval(100);
                connect(chunks,&QTimer::timeout,socket,[socket,chunks]{
                    socket->write(QByteArray(65536,'x'));
                    const int n=socket->property("chunks").toInt()+1;socket->setProperty("chunks",n);
                    if(n==3 && socket->property("disconnectFirst").toBool()){chunks->stop();socket->disconnectFromHost();return;}
                    if(n==16){chunks->stop();socket->disconnectFromHost();}
                });chunks->start();
            });
        });
        QTemporaryDir profile;DesktopWindow win(profile.path(),true);win.inCall=true;
        auto* update=win.updates;update->asset=QUrl(QString("http://127.0.0.1:%1/client.zip").arg(source.serverPort()));
        update->version="9.9.9";update->expected=1048576;update->digest=QString(64,'0');
        QWidget viewport;
        auto panel=[&]{return new ServerUpdatesPanel(win.api,*update,false,&viewport,{},[](const QJsonObject&){return QString();},[](const QString&,const QString&){});};
        auto* first=panel();update->install();QVERIFY(update->active());
        QTRY_VERIFY(update->downloadedBytes()>0);
        QVERIFY(first->findChild<QProgressBar*>("desktopUpdateProgressBar")->value()>0);
        delete first;
        const auto before=update->downloadedBytes();QTRY_VERIFY(update->downloadedBytes()>before);
        auto* reopened=panel();QVERIFY(!reopened->findChild<QWidget*>("desktopUpdateProgress")->isHidden());
        QVERIFY(reopened->findChild<QProgressBar*>("desktopUpdateProgressBar")->value()>0);
        if(const auto out=qEnvironmentVariable("NIMBO_UPDATE_SHOTS");!out.isEmpty() && !disconnectFirst){
            reopened->setStyleSheet(win.styleSheet());reopened->resize(620,210);reopened->show();QTest::qWait(100);
            QVERIFY(reopened->grab().save(out+"/update-progress-ru.png"));
            delete reopened;UiText::setLanguage("en");reopened=panel();reopened->setStyleSheet(win.styleSheet());reopened->resize(620,210);reopened->show();
            QVERIFY(reopened->grab().save(out+"/update-progress-en.png"));UiText::setLanguage("ru");
        }
        win.api.reset();win.api.server=QUrl("http://127.0.0.1:1");
        QTRY_VERIFY_WITH_TIMEOUT(!update->active(),7000);QCOMPARE(requests,disconnectFirst?2:1);
        QCOMPARE(update->received,qint64(1048576));QCOMPARE(update->message,UiText::Id::TheReleaseChecksumDoesNotMatch);
        QVERIFY(reopened->findChild<QWidget*>("desktopUpdateProgress")->isHidden());
        delete reopened;win.inCall=false;
    }
    void initTestCase() { QLocale::setDefault(QLocale(QLocale::Russian)); }

    void interfaceLanguage() {
        QCOMPARE(UiText::resolveLanguage({}, {"ru-RU"}), "ru");
        QCOMPARE(UiText::resolveLanguage({}, {"de-DE","en-GB"}), "en");
        QCOMPARE(UiText::resolveLanguage("en", {"ru-RU"}), "en");
        QCOMPARE(UiText::resolveLanguage("invalid", {"de-DE"}), "en");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString name=QStringLiteral("Общие");
        {
            DesktopWindow win(directory.path());
            win.profile={{"format","nimbo-profile"},{"version",1},{"name",name},{"servers",QJsonArray{}}};
            win.nickname->setText(name);
            win.pages->setCurrentIndex(1);
            win.openView("general");
            auto* selector=win.findChild<QComboBox*>("profileLanguage");
            QVERIFY(selector);
            selector->setCurrentIndex(selector->findData("en"));
            QTRY_COMPARE(win.backButton->text(),QStringLiteral("Back to room"));
            QTRY_VERIFY(win.title->text().contains("General"));
            QCOMPARE(win.nickname->text(),name);
            QCOMPARE(win.profile["language"].toString(),"en");
            const int initialWidgets=win.findChildren<QWidget*>().size();
            for(int round=0;round<10;++round){
                for(const QString code:{"ru","en"}){
                    QPointer<QComboBox> previous=win.findChild<QComboBox*>("profileLanguage");
                    QVERIFY(previous);
                    previous->setCurrentIndex(previous->findData(code));
                    QCOMPARE(win.findChild<QComboBox*>("profileLanguage"),previous.data());
                    QCOMPARE(UiText::locale().language(),code=="ru"?QLocale::Russian:QLocale::English);
                    QCOMPARE(win.nickname->text(),name);
                }
            }
            QVERIFY(win.findChildren<QWidget*>().size()<=initialWidgets);
            const auto recovered=DesktopProfile::open(DesktopProfile::seal(win.profile,"language-qa"),"language-qa");
            QCOMPARE(recovered["language"].toString(),"en");
            QCOMPARE(recovered["name"].toString(),name);
            QCOMPARE(win.compose->placeholderText(),QStringLiteral("Write a message… @ to mention"));
            win.self["role"]="owner";
            for(const auto& tab:QList<QPair<QString,QString>>{
                    {"audio","Sound"},{"video","Video"},{"channels","Channels"},
                    {"roles","Members and roles"},{"keys","Keyboard shortcuts"},{"general","General"}}){
                win.openView(tab.first);
                QVERIFY2(win.title->accessibleName().contains(tab.second),qPrintable(tab.first));
                if(tab.first=="keys"){
                    bool microphone=false;
                    for(auto* label:win.detailPage->findChildren<QLabel*>())
                        if(label->isVisibleTo(win.detailPage) && label->text()=="Microphone")microphone=true;
                    QVERIFY(microphone);
                }
            }
        }
        {
            DesktopWindow win(directory.path());
            QCOMPARE(win.profile["language"].toString(),"en");
            QCOMPARE(win.loginButton->text(),"Sign in to Nimbo");
            win.pages->setCurrentIndex(1);
            win.openView("general");
            auto* selector=win.findChild<QComboBox*>("profileLanguage");
            QVERIFY(selector);
            QCOMPARE(selector->currentData().toString(),"en");
            selector->setCurrentIndex(selector->findData("system"));
            QTRY_VERIFY(win.title->text().contains(QStringLiteral("Общие")));
            QCOMPARE(win.nickname->text(),name);
            QCOMPARE(win.backButton->text(),QStringLiteral("К комнате"));
        }
    }

    void unreadUpdates(){
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QUrl origin("http://127.0.0.1:1");
        {
            DesktopWindow win(directory.path());
            win.profile={{"format","nimbo-profile"},{"version",1},{"name","Tester"},{"servers",QJsonArray{}}};
            win.saveLocal();
            win.self["role"]="owner";win.api.server=origin;
            win.syncUpdateContext();
            win.recordServerOffer({{"phase","available"},{"available","server-v2"},{"installed","server-v1"}});
            win.updates->version="1.0.0";win.updates->asset=QUrl("https://example.invalid/one");
            win.updates->refresh();
            QVERIFY(!win.settingsUpdateDot->isHidden());
            win.pages->setCurrentIndex(1);
            win.resize(1060,600);win.show();win.activateWindow();
            win.openView("general");
            auto* dot=win.detailPage->findChild<QLabel*>("availableUpdateDot");
            auto* scroll=win.detailPage->findChild<QScrollArea*>("settingsScroll");
            QVERIFY(dot && scroll);
            auto* section=win.detailPage->findChild<QWidget*>("settingsSection");
            auto* layout=qobject_cast<QBoxLayout*>(section->layout());
            // The unconfigured fixture has no storage stats. Fill the space occupied on a live owner server.
            layout->insertSpacing(0,700);
            scroll->verticalScrollBar()->setValue(0);
            QTest::qWait(100);
            QVERIFY(!scroll->viewport()->rect().contains(dot->mapTo(scroll->viewport(),dot->rect().center())));
            QVERIFY(!dot->isHidden());
            QVERIFY(!win.settingsUpdateDot->isHidden());
            auto colorAt=[](QWidget* widget,QPoint point){
                const auto image=widget->grab().toImage();
                return image.pixelColor(point*image.devicePixelRatio());
            };
            const QPoint badgeCenter=win.settingsUpdateDot->geometry().center();
            QCOMPARE(colorAt(win.settingsButton,badgeCenter),QColor("#64aaf0"));
            scroll->ensureWidgetVisible(dot);
            QTRY_VERIFY(win.settingsUpdateDot->isHidden());
            QVERIFY(!dot->isHidden());
            auto* heading=dot->parentWidget();
            QCOMPARE(colorAt(heading,dot->geometry().center()),QColor("#64aaf0"));
            QVERIFY(colorAt(win.settingsButton,badgeCenter)!=QColor("#64aaf0"));
            win.openView("room");
            win.updates->version="1.0.1";win.updates->refresh();
            QVERIFY(!win.settingsUpdateDot->isHidden());
        }
        {
            DesktopWindow win(directory.path());
            win.self["role"]="owner";win.api.server=origin;win.syncUpdateContext();
            win.recordServerOffer({{"phase","available"},{"available","server-v2"}});
            win.updates->version="1.0.0";win.updates->asset=origin;win.updates->refresh();
            QVERIFY(win.settingsUpdateDot->isHidden());
            win.recordServerOffer({{"phase","available"},{"available","server-v3"}});
            QVERIFY(!win.settingsUpdateDot->isHidden());
            win.recordServerOffer({{"phase","current"},{"installed","server-v3"}});
            QVERIFY(win.serverOffer.isEmpty());
            QVERIFY(win.settingsUpdateDot->isHidden());
            win.recordServerOffer({{"phase","available"},{"available","server-v4"}});
            win.self["role"]="member";win.syncUpdateContext();
            QVERIFY(win.settingsUpdateDot->isHidden());
            win.updates->asset.clear();win.updates->refresh();
            win.pages->setCurrentIndex(1);win.openView("general");
            QVERIFY(win.detailPage->findChild<QLabel*>("availableUpdateDot")->isHidden());
        }
    }

    void releaseManifest(){
        DesktopRelease release;
        QJsonObject valid{{"schema",1},{"platform","windows-x64"},{"version","0.3.24"},{"size",1234},{"sha256",QString(64,'a')}};
        QVERIFY(release.read(valid));
        QCOMPARE(release.archive().toString(),QStringLiteral("https://github.com/DiabloEN/Nimbo/releases/download/desktop-v0.3.24/Nimbo-Desktop-0.3.24-portable.zip"));
        for(const auto& version:{"web-0.4.4-67","../test","0.3.24-beta","0.3.24.1"}){
            auto bad=valid;bad["version"]=version;QVERIFY(!release.read(bad));
        }
        for(const auto& key:{"schema","platform","size","sha256"}){auto bad=valid;bad.remove(key);QVERIFY(!release.read(bad));}
    }
    void voicePlaybackLevels(){
        const int16_t left[]={12000,0},right[]={0,12000};
        QCOMPARE(AudioPcm::monoSample(left,2),int16_t(6000));
        QCOMPARE(AudioPcm::monoSample(right,2),int16_t(6000));
        for(auto [percent,gain]:{std::pair{0,0.f},{50,1.f},{100,2.f},{150,4.f},{200,8.f}}){
            AudioPcm::VoiceLevel level;
            QCOMPARE(AudioPcm::voiceGain(percent),gain);
            QCOMPARE(level.apply(.01f,gain),.01f*gain);
        }
        AudioPcm::VoiceLevel loud;
        float out=0;for(int i=0;i<4800;++i)out=loud.apply(.9f,8);
        QVERIFY(out>.70f && out<.81f);
    }
    void richChatDraft(){
        MessageInput editor;
        const QString text=QStringLiteral("ж😀я\n* пункт");
        editor.setDraft(text,"1332044");
        QCOMPARE(editor.formatMask(),QString("1332044"));
        editor.selectAll();editor.copy();
        MessageInput pasted;pasted.paste();
        QCOMPARE(pasted.draftText(),text);QCOMPARE(pasted.formatMask(),editor.formatMask());
        pasted.setDraft("plain",{});QVERIFY(pasted.formatMask().isEmpty());
        pasted.setDraft(QString(8100,'a'),QString(8100,'1'));QCOMPARE(pasted.formatMask().size(),8000);
        pasted.setDraft("*",{});QTest::keyClick(&pasted,Qt::Key_Space);QTest::keyClicks(&pasted,"one");
        QTest::keyClick(&pasted,Qt::Key_Return,Qt::ShiftModifier);QTest::keyClicks(&pasted,"two");
        QCOMPARE(pasted.toPlainText(),QString("• one\n• two"));QCOMPARE(pasted.draftText(),QString("* one\n* two"));QCOMPARE(pasted.formatMask(),QString("44000044"));
        pasted.setDraft("1.",{});QTest::keyClick(&pasted,Qt::Key_Space);QTest::keyClicks(&pasted,"one");
        QTest::keyClick(&pasted,Qt::Key_Return,Qt::ShiftModifier);
        QCOMPARE(pasted.draftText(),QString("1. one\n2. "));
        QTest::keyClick(&pasted,Qt::Key_Return,Qt::ShiftModifier);
        QCOMPARE(pasted.draftText(),QString("1. one\n"));
    }
    void chatSelectionSurvivesRefresh(){
        QTemporaryDir profile;
        DesktopWindow win(profile.path());win.resize(1000,720);win.show();
        win.pages->setCurrentIndex(1);win.messages->show();
        const QString url="https://example.com/long-link";
        win.lastMessages={QJsonObject{{"id","one"},{"name","User"},{"text",url}}};
        win.updateMessageRows();QCoreApplication::processEvents();
        QPointer<QLabel> body=win.messages->findChild<QLabel*>("messageBody");QVERIFY(body);
        const QPoint start(4,body->height()/2),end(80,start.y());
        QTest::mousePress(body,Qt::LeftButton,{},start);QTest::mouseMove(body,end);
        win.lastMessages.append(QJsonObject{{"id","two"},{"name","User"},{"text","next"}});
        win.updateMessageRows();QVERIFY(body);QTest::mouseRelease(body,Qt::LeftButton,{},end);
        const auto selection=body->selectedText();QVERIFY(!selection.isEmpty() && selection!=url && url.contains(selection));
        win.updateMessageRows();QCOMPARE(body->selectedText(),selection);
        QTest::keyClick(body,Qt::Key_C,Qt::ControlModifier);QCOMPARE(QApplication::clipboard()->text(),selection);
        for(int i=0;i<35;++i)win.lastMessages.append(QJsonObject{{"id",QString::number(i)},{"text","hello"}});
        win.updateMessageRows();win.messages->scrollToBottom();auto* bar=win.messages->verticalScrollBar();
        QVERIFY(bar->maximum()>0);
        auto* last=win.messages->itemWidget(win.messages->item(win.messages->count()-1));
        last->layout()->addWidget(new QLabel(QString("more\n").repeated(10)));win.relayoutChat();
        QCOMPARE(bar->value(),bar->maximum());
    }
    void linkCover(){
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        QImage image(320,180,QImage::Format_RGB32);image.fill(QColor("#2468ae"));
        QBuffer png;png.open(QIODevice::WriteOnly);QVERIFY(image.save(&png,"PNG"));
        const QByteArray payload=QJsonDocument(QJsonObject{{"title","Проверка обложки"},{"image","data:image/png;base64,"+QString::fromLatin1(png.data().toBase64())}}).toJson();
        int requests=0;
        connect(&server,&QTcpServer::newConnection,&server,[&]{
            auto* socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                socket->readAll();++requests;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\n\r\n"+payload);socket->disconnectFromHost();
            });
        });
        ServerApi api;api.server=QUrl("http://127.0.0.1:"+QString::number(server.serverPort()));
        QWidget host;auto* layout=new QVBoxLayout(&host);host.resize(500,280);
        auto* card=new LinkCard(api,"room","message",LinkCard::firstLink("https://youtu.be/umogLehRxgc"),[]{});
        card->attach(layout);host.show();
        QTRY_VERIFY(card->accessibleName().contains("Проверка обложки"));
        QCOMPARE(card->size(),QSize(420,236));auto shot=card->grab().toImage().scaled(card->size());
        QCOMPARE(shot.pixelColor(60,170),image.pixelColor(30,100));
        QVERIFY(shot.pixelColor(195,118)!=image.pixelColor(30,100));
        QWidget staging;card->detach(&staging);card->attach(layout);QCoreApplication::processEvents();
        QCOMPARE(requests,1);QVERIFY(!card->findChild<QWidget*>());
        delete card;
        card=new LinkCard(api,"room","message",LinkCard::firstLink("https://youtu.be/umogLehRxgc"),[]{});
        card->attach(layout);
        QVERIFY(card->accessibleName().contains("Проверка обложки"));
        QCOMPARE(card->grab().toImage().scaled(card->size()).pixelColor(60,170),image.pixelColor(30,100));
        QCOMPARE(requests,1);
        if(qEnvironmentVariableIsSet("NIMBO_QA_ONLINE_YOUTUBE")){
            qInfo()<<"Online YouTube check begins";
            QTest::mouseClick(card,Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(card->findChild<QWidget*>(),4000);
            auto* player=static_cast<YoutubeSurface*>(card->findChild<QWidget*>());
            QPointer<YoutubeSurface> playing(player);
            QTRY_VERIFY_WITH_TIMEOUT(playing && playing->accessibleName()=="Видео YouTube воспроизводится",35000);
            HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,playing->browserProcessId());QVERIFY(process);
            host.hide();QVERIFY(playing.isNull());
            QTRY_COMPARE_WITH_TIMEOUT(WaitForSingleObject(process,0),DWORD(WAIT_OBJECT_0),12000);CloseHandle(process);
        }
    }
    void youtubePauseReleasesRuntime(){
        if(QGuiApplication::platformName()!="windows")QSKIP("Requires Windows compositor");
        QTemporaryDir fixture;QVERIFY(fixture.isValid());
        QFile shipped(QCoreApplication::applicationDirPath()+"/assets/player/video.html");
        QVERIFY(shipped.open(QIODevice::ReadOnly));QByteArray html=shipped.readAll();
        const QByteArray script="https://www.youtube.com/iframe_api";QVERIFY(html.contains(script));html.replace(script,"fixture.js");
        html.replace("'https://www.youtube-nocookie.com/embed/' + id + '?' + query","'about:blank'");
        QFile page(fixture.filePath("video.html"));QVERIFY(page.open(QIODevice::WriteOnly));page.write(html);page.close();
        QFile stub(fixture.filePath("fixture.js"));QVERIFY(stub.open(QIODevice::WriteOnly));
        stub.write("window.YT={PlayerState:{PLAYING:1,PAUSED:2},Player:class{constructor(_,o){"
                   "const frame=document.getElementById('player');"
                   "if(document.documentElement.scrollWidth!==innerWidth || document.documentElement.scrollHeight!==innerHeight || frame.offsetWidth!==innerWidth || frame.offsetHeight!==innerHeight || !frame.allow.includes('autoplay')){o.events.onError();return;}"
                   "document.body.style.background='#2468ae';frame.style.opacity=0;"
                   "o.events.onReady({target:{seekTo(t){if(t!==5)o.events.onError()},playVideo(){o.events.onStateChange({data:1});setTimeout(()=>o.events.onStateChange({data:2,target:{getCurrentTime:()=>12.8}}),3300)}}});"
                   "}}};onYouTubeIframeAPIReady();");stub.close();
        QScrollArea area;area.resize(560,375);auto* content=new QWidget;content->resize(500,1300);
        area.setWidget(content);QWidget card(content);card.setGeometry(12,70,420,236);area.show();
        auto* player=new YoutubeSurface(&card,"umogLehRxgc",5000,fixture.path());
        player->setGeometry(card.rect());int position=-1;QString error;QImage paused;
        player->finished=[&](int ms,QString e,QImage frame){position=ms;error=e;paused=frame;};
        QTRY_VERIFY_WITH_TIMEOUT(player->browserProcessId() || !error.isEmpty(),12000);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,player->browserProcessId());QVERIFY(process);
        auto* host=area.findChild<QWidget*>("youtubeViewportHost",Qt::FindDirectChildrenOnly);QVERIFY(host);
        QTRY_VERIFY_WITH_TIMEOUT(host->isVisible(),8000);
        const HWND hwnd=reinterpret_cast<HWND>(host->winId());
        auto checkBounds=[&](int offset){
            area.verticalScrollBar()->setValue(offset);
            const QRect expected=QRect(player->mapTo(area.viewport(),QPoint()),player->size()).intersected(area.viewport()->rect());
            QCOMPARE(bool(IsWindowVisible(hwnd)),!expected.isEmpty());
            if(!expected.isEmpty()){
                RECT native{};QVERIFY(GetWindowRect(hwnd,&native));
                const auto global=area.viewport()->mapToGlobal(expected.topLeft());
                const auto root=area.viewport()->window();
                const auto relative=root->mapFromGlobal(global);
                POINT base{};ClientToScreen(reinterpret_cast<HWND>(root->winId()),&base);
                const auto dpr=host->devicePixelRatioF();
                QVERIFY(qAbs(native.left-base.x-qRound(relative.x()*dpr))<=1);
                QVERIFY(qAbs(native.top-base.y-qRound(relative.y()*dpr))<=1);
                QVERIFY(qAbs(native.bottom-native.top-qRound(expected.height()*dpr))<=1);
            }
        };
        checkBounds(0);checkBounds(145);checkBounds(550);checkBounds(0);
        QElapsedTimer elapsed;elapsed.start();QList<qint64> gaps;QTimer tick;tick.setTimerType(Qt::PreciseTimer);
        connect(&tick,&QTimer::timeout,&area,[&]{gaps<<elapsed.restart();area.verticalScrollBar()->setValue((gaps.size()*4)%290);});
        QEventLoop loop;tick.start(16);QTimer::singleShot(1200,&loop,&QEventLoop::quit);loop.exec();tick.stop();
        QVERIFY(gaps.size()>5);std::sort(gaps.begin(),gaps.end());qInfo()<<"Inline WebView scroll: timer intervals (ms) p95/max"<<gaps[gaps.size()*95/100]<<gaps.back()<<"screen Hz (Qt)"<<area.screen()->refreshRate();
        checkBounds(0);
        QTRY_COMPARE_WITH_TIMEOUT(position,12800,10000);QVERIFY(!paused.isNull());
        QCOMPARE(paused.pixelColor(paused.rect().center()),QColor("#2468ae"));delete player;
        QTRY_COMPARE_WITH_TIMEOUT(WaitForSingleObject(process,0),DWORD(WAIT_OBJECT_0),12000);CloseHandle(process);
    }
    void invitationListOrder(){
        ServerApi api;
        const auto entry=[](QString id,qint64 created,qint64 expires=0){
            return QJsonObject{{"id",id},{"code",id},{"createdAt",created},{"expiresAt",expires}};
        };
        const QJsonArray data={entry("legacy",0),entry("first",1000),entry("second",1000),
                               entry("newest",2000),entry("expired",3000,4000)};
        InvitationSettings settings(api,data,[]{});
        auto* list=settings.findChild<QScrollArea*>()->widget()->layout();
        const QStringList expected={"newest","second","first","legacy"};
        QCOMPARE(list->count(),expected.size());
        for(int i=0;i<expected.size();++i){
            auto* card=list->itemAt(i)->widget();
            QVERIFY(card->findChild<QPlainTextEdit*>()->toPlainText().endsWith("Код приглашения: "+expected[i]));
            QStringList labels;
            for(auto* field:card->findChildren<QLabel*>())labels<<field->text();
            QVERIFY(labels.join(" ").contains("Без срока"));
            QCOMPARE(labels.join(" ").contains("Создано "),expected[i]!="legacy");
        }
    }
    void speakingIndicator(){
        for(bool camera:{false,true}){
            VoiceCard card(230,"Тестер",true,false,false,{},camera);
            auto sample=[&]{auto pix=card.grab();return pix.toImage().scaled(card.size());};
            if(camera){
                QImage asymmetric(320,180,QImage::Format_RGB32);asymmetric.fill(Qt::white);
                QPainter painter(&asymmetric);painter.fillRect(0,0,160,180,Qt::blue);painter.end();
                card.setFrame(asymmetric);
                for(bool own:{false,true}){
                    VoiceCard peer(230,"Тестер",own,false,false,{},true);peer.setFrame(asymmetric);
                    auto image=peer.grab().toImage().scaled(peer.size());
                    QCOMPARE(image.pixelColor(30,24),QColor(Qt::white));
                    QCOMPARE(image.pixelColor(210,24),QColor(Qt::blue));
                    peer.setMicrophone(false,true);
                    QVERIFY(peer.grab().toImage().scaled(peer.size())!=image);
                }
            }
            auto silent=sample();
            card.setMicrophone(true,false);
            auto speaking=sample();
            int x=card.width()-(camera?19:16)-14, y=camera?27:24;
            for(int bar=0;bar<4;++bar){
                QCOMPARE(speaking.pixelColor(x+4*bar,y),QColor("#ccc"));
                QVERIFY(silent.pixelColor(x+4*bar,y)!=QColor("#ccc"));
            }
            QTest::qWait(160);QCOMPARE(sample(),speaking);
            card.setMicrophone(false,false);QCOMPARE(sample(),silent);
        }
    }

    void chatText(){
        const QString linked=MessageText::links("Вот (https://example.org/a(b)). www.example.org!");
        QCOMPARE(linked.count("<a "),2);
        QVERIFY(linked.contains("https://example.org/a(b)</a>)."));
        QVERIFY(!MessageText::links("https://a:b@example.org xhttps://example.org a@www.example.org").contains("<a "));
        QVERIFY(MessageText::links("<img>").contains("&lt;img&gt;"));
        const QJsonObject member{{"id","self"},{"role","member"}};
        const QJsonObject mention{{"start",0},{"end",3},{"kind","member"},{"id","self"}};
        QVERIFY(MessageText::body("@Я! <em>",{mention},member).contains("#285b9d"));
        QVERIFY(MessageText::body("@Я! <em>",{mention},member).contains("&lt;em&gt;"));
    }
    void monitorFormats(){
        QAudioFormat stereo;
        stereo.setSampleRate(44100);stereo.setChannelCount(2);stereo.setSampleFormat(QAudioFormat::Float);
        float input[480];
        for(int i=0;i<480;++i)input[i]=float(i)/960;
        const auto deviceBlock=AudioPcm::playbackBlock(input,stereo);
        QCOMPARE(deviceBlock.size(),441*2*int(sizeof(float)));
        for(int i=0;i<441;++i){
            const auto l=AudioPcm::decode(deviceBlock.constData()+i*8,QAudioFormat::Float);
            const auto r=AudioPcm::decode(deviceBlock.constData()+i*8+4,QAudioFormat::Float);
            if(l!=r)QFAIL("The left and right headphone samples differ");
        }
        const auto mono=AudioPcm::captureBlock(deviceBlock.constData(),stereo);
        QCOMPARE(mono.size(),size_t(480));
        for(int i=0;i<480;++i)QVERIFY(std::abs(mono[i]/32768.f-input[i])<.002f);
    }
    void audioDenoisers(){for(bool gt:{false,true}){VoiceDenoiser denoise(gt);std::mt19937 rng(27);std::uniform_int_distribution<int> noise(-2200,2200);double before=0,after=0;for(int j=0;j<150;++j){std::vector<int16_t> block(480);for(auto& x:block){x=int16_t(noise(rng));if(j>70)before+=double(x)*x;}denoise.filter(block);if(j>70)for(auto x:block)after+=double(x)*x;}QVERIFY(after<before*.8);}}
    void cameraEffect(){
        QImage photo(QCoreApplication::applicationDirPath()+"/assets/room-preview.png");QVERIFY(!photo.isNull());
        auto original=photo.copy(0,0,photo.width()/2,photo.height()).scaled(320,320).convertToFormat(QImage::Format_RGBA8888);
        BackgroundBlur effect;auto changed=effect.apply(original,18);QCOMPARE(changed.size(),original.size());QVERIFY(changed!=original);
        if(auto dest=qEnvironmentVariable("NIMBO_NATIVE_TEST_SCREENSHOT");!dest.isEmpty())QVERIFY(changed.save(QFileInfo(dest).absolutePath()+"/camera-effect.png"));
    }
    void profileExchange() {
        const auto fixture=qEnvironmentVariable("NIMBO_WEB_FIXTURE");
        if(fixture.isEmpty())QSKIP("Run test-native-desktop.py for independent WebCrypto fixture");
        QFile file(fixture);QVERIFY(file.open(QIODevice::ReadOnly)); const auto encrypted=file.readAll();
        QVERIFY_EXCEPTION_THROWN(DesktopProfile::open(encrypted,"wrong-password"),std::exception);
        const auto profile=DesktopProfile::open(encrypted,"native-fixture");
        QCOMPARE(profile["name"].toString(),QStringLiteral("Тестер"));
        QVERIFY(profile["hotkeys"].isObject());
        QFile out(fixture+".native");QVERIFY(out.open(QIODevice::WriteOnly));QVERIFY(out.write(DesktopProfile::seal(profile,"native-fixture"))>0);
        QCOMPARE(DesktopProfile::unprotect(DesktopProfile::protect("sample")),QByteArray("sample"));
        QVERIFY(DesktopProfile::origin("http://outside.example").isEmpty());
        QVERIFY(DesktopProfile::origin("https://name@outside.example").isEmpty());
    }
    void loginProfileImport(){
        const auto path=qEnvironmentVariable("NIMBO_WEB_FIXTURE");
        if(path.isEmpty())QSKIP("Run with a WebCrypto fixture");
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto encrypted=file.readAll();
        QTemporaryDir directory;QVERIFY(directory.isValid());
        DesktopWindow win(directory.path());win.setAttribute(Qt::WA_DontShowOnScreen);win.show();
        win.address->setText("http://127.0.0.1:1");
        auto malformed=QJsonDocument::fromJson(encrypted).object();malformed["iv"]="invalid";
        win.loadLoginFile(QJsonDocument(malformed).toJson());QVERIFY(win.loginImportBox->isHidden());
        win.loadLoginFile(encrypted);QVERIFY(win.loginImportBox->isVisible());QVERIFY(!win.unlockFile->isEnabled());
        for(auto* action:win.loginImportBox->findChildren<QPushButton*>())if(action->text()=="Отмена")action->click();
        QVERIFY(win.loginImportBox->isHidden());QVERIFY(win.pendingLoginFile.isEmpty());QVERIFY(win.localProfiles.isEmpty());
        win.loadLoginFile(encrypted);win.filePassword->setText("invalid-password");win.unlockFile->click();
        QTRY_VERIFY_WITH_TIMEOUT(win.loginImportBox->isEnabled(),10000);
        QVERIFY(win.loginStatus->text().contains("пароль"));QVERIFY(win.localProfiles.isEmpty());
        win.filePassword->setText("native-fixture");QTest::keyClick(win.filePassword,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(win.localHome && win.pages->currentWidget()==win.localHome,10000);
        QCOMPARE(win.profile["name"].toString(),QStringLiteral("Тестер"));QCOMPARE(win.localProfiles.size(),1);
        QVERIFY(win.localStatus->text().contains("локальный профиль"));QVERIFY(win.api.token.isEmpty());
        const auto first=win.localId;const auto original=win.profile;
        win.showLogin();win.loadLoginFile(QJsonDocument(original).toJson());QCOMPARE(win.localId,first);QCOMPARE(win.localProfiles.size(),1);
        DesktopWindow reopened(directory.path());QCOMPARE(reopened.localId,first);QCOMPARE(reopened.profile,original);
        const auto url=qEnvironmentVariable("NIMBO_NATIVE_TEST_URL");
        if(!url.isEmpty()){
            auto unregistered=original;unregistered["servers"]=QJsonArray{QJsonObject{{"url",url},{"profileKey",QString::fromLatin1(DesktopProfile::randomKey())}}};
            win.showLogin();win.address->setText(url);win.loadLoginFile(QJsonDocument(unregistered).toJson());
            QTRY_COMPARE_WITH_TIMEOUT(win.pages->currentWidget(),win.localHome,10000);QVERIFY(win.api.token.isEmpty());
            QVERIFY(!win.localStatus->text().isEmpty());QCOMPARE(win.localProfiles.size(),2);
        }
    }
    void switchLocalProfile(){
        const auto url=qEnvironmentVariable("NIMBO_NATIVE_TEST_URL3");
        if(url.isEmpty())QSKIP("Run test-native-desktop.py with the isolated profile server");
        QTemporaryDir directory;QVERIFY(directory.isValid());
        DesktopWindow restored(directory.path(),true);restored.setAttribute(Qt::WA_DontShowOnScreen);restored.show();
        restored.address->setText(url);restored.nickname->setText("Тестер");
        restored.password->setText(qEnvironmentVariable("NIMBO_NATIVE_TEST_CODE3"));restored.loginButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(!restored.selfId.isEmpty(),7000);
        QString invite,error;
        restored.api.request("POST","/api/invitations",{{"seconds",3600},{"singleUse",false}},[&](auto data,QString issue){
            invite=data.value("code").toString();error=issue;
        });
        QTRY_VERIFY_WITH_TIMEOUT(!invite.isEmpty() || !error.isEmpty(),7000);QVERIFY2(error.isEmpty(),qPrintable(error));
        const QString currentId=restored.localId, currentMember=restored.selfId;
        const QUrl originalServer=restored.api.server;
        const auto previousToken=restored.api.token;
        auto blank=restored.profile;
        blank["name"]="Дополнительный профиль";blank["servers"]=QJsonArray{};
        bool merged=false;const auto added=restored.persistImport(blank,false,merged);
        QVERIFY(!added.isEmpty() && added!=currentId);QVERIFY(!merged);
        restored.openView("profile");
        auto* prompt=restored.pages->currentWidget()->findChild<QWidget*>("switchProfileForm");
        QVERIFY(prompt);QVERIFY(prompt->isHidden());
        auto* login=prompt->parentWidget()->findChild<QPushButton*>();
        for(auto* button:prompt->parentWidget()->findChildren<QPushButton*>())if(button->text()=="Войти")login=button;
        QVERIFY(login && login->text()=="Войти");login->click();
        QTRY_VERIFY_WITH_TIMEOUT(!prompt->isHidden(),7000);
        QCOMPARE(restored.localId,currentId);QCOMPARE(restored.api.token,previousToken);
        auto* code=prompt->findChild<QLineEdit*>();QVERIFY(code);
        code->setText("unregistered-test-invite");QTest::keyClick(code,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(restored.pages->isEnabled() && !prompt->isHidden(),7000);
        QCOMPARE(restored.selfId,currentMember);
        QCOMPARE(originalServer.toString(),url);
        QVERIFY(!invite.isEmpty());code->setText(invite);QTest::keyClick(code,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(restored.pages->isEnabled(),10000);
        QVERIFY2(restored.localId==added,qPrintable(restored.status->text()));QCOMPARE(restored.view,QString("profile"));
        QTRY_VERIFY_WITH_TIMEOUT(!restored.selfId.isEmpty() && restored.selfId!=currentMember,7000);
        QCOMPARE(restored.api.server,originalServer);
        restored.chooseProfile(currentId,{},[]{});
        QTRY_VERIFY_WITH_TIMEOUT(restored.pages->isEnabled(),10000);
        QVERIFY2(restored.localId==currentId,qPrintable(restored.status->text()));
        QTRY_COMPARE_WITH_TIMEOUT(restored.selfId,currentMember,7000);
    }
    void audioSelectRows(){
        QWidget panel;auto* column=new QVBoxLayout(&panel);
        auto* box=Ui::select("Шумоподавление");column->addWidget(box);
        box->addItems({"GTCRN","RNNoise","WebRTC","Выключено"});
        panel.resize(640,100);panel.show();box->setFocus();QTest::keyClick(box,Qt::Key_Down);
        auto* list=box->view();QTRY_VERIFY(list->isVisible());
        const int height=list->visualRect(list->model()->index(0,0)).height();QVERIFY(height>=35);
        for(int i=0;i<4;++i){
            auto r=list->visualRect(list->model()->index(i,0));QCOMPARE(r.height(),height);
            QCOMPARE(r.top(),i*height);QVERIFY(list->viewport()->rect().contains(r.center()));
        }
        QTest::keyClick(list,Qt::Key_End);QTest::keyClick(list,Qt::Key_Escape);
        QCOMPARE(box->currentText(),QString("GTCRN"));
        QTest::mouseClick(box,Qt::LeftButton);QTRY_VERIFY(list->isVisible());
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualRect(list->model()->index(1,0)).center());
        QCOMPARE(box->currentText(),QString("RNNoise"));
        QTest::keyClick(box,Qt::Key_Return);QTRY_VERIFY(list->isVisible());
        QTest::keyClick(list,Qt::Key_End);QTest::keyClick(list,Qt::Key_Return);
        QCOMPARE(box->currentText(),QString("Выключено"));
    }
    void chatSound(){
        QTemporaryDir directory;DesktopWindow win(directory.path(),true);
        const QJsonObject first{{"id","n1"},{"createdAt",80}};
        win.workspace["notifications"]=QJsonArray{first};
        win.updateChatSound(true);QVERIFY(win.callSounds.isEmpty());
        win.updateChatSound(false);QVERIFY(win.callSounds.isEmpty());
        win.workspace["notifications"]=QJsonArray{first,QJsonObject{{"id","n0"},{"createdAt",70}}};
        win.updateChatSound(false);QVERIFY(win.callSounds.isEmpty());
        win.workspace["notifications"]=QJsonArray{first,QJsonObject{{"id","n2"},{"createdAt",90}}};
        win.updateChatSound(false);
        QPointer<QSoundEffect> chime=win.callSounds.value("chat");QVERIFY(chime);
        QTRY_VERIFY_WITH_TIMEOUT(chime && chime->status()==QSoundEffect::Ready,4000);
        QTRY_VERIFY_WITH_TIMEOUT(!chime && !win.callSounds.contains("chat"),4000);
        win.chatSoundCooldown.invalidate();win.updateChatSound(false);QVERIFY(!win.chatSoundCooldown.isValid());
        win.api.server=QUrl("http://127.0.0.1");win.selfId="tester";
        QSettings(win.dataPath+"/desktop.ini",QSettings::IniFormat).setValue("notify/127.0.0.1/tester/general",true);
        win.workspace["chatActivity"]=QJsonObject{{"general",QJsonObject{{"id","m1"},{"createdAt",95}}}};
        win.updateChatSound(false);QVERIFY(win.chatSoundCooldown.isValid());
    }
    void interruptedVoiceJoin(){
        const auto url=qEnvironmentVariable("NIMBO_NATIVE_TEST_URL4");
        if(url.isEmpty())QSKIP("Run test-native-desktop.py for an isolated server");
        QTemporaryDir directory;DesktopWindow win(directory.path(),true);
        win.setAttribute(Qt::WA_DontShowOnScreen);win.show();
        win.address->setText(url);win.nickname->setText("Тестер");win.password->setText(qEnvironmentVariable("NIMBO_NATIVE_TEST_CODE4"));
        win.loginButton->click();QTRY_VERIFY_WITH_TIMEOUT(!win.rooms.isEmpty(),10000);
        QList<int> voices;
        for(int i=0;i<win.channels->count();++i)
            if(win.channels->item(i)->data(Qt::UserRole).toJsonObject()["kind"]=="voice")voices.append(i);
        QVERIFY(voices.size()>=2);
        win.channels->setCurrentRow(voices[0]);QElapsedTimer feedback;feedback.start();
        win.join();QVERIFY(win.joining);QVERIFY(win.joinButton->isVisible());
        qInfo()<<"Join feedback (ms)"<<feedback.elapsed();
        const auto originalAttempt=win.voiceInviteGeneration;
        win.channels->setCurrentRow(voices[1]);win.join();
        auto target=win.selected;
        QCOMPARE(win.activeVoice,target);
        QVERIFY(win.joining && !win.leavingVoice && win.voiceInviteGeneration!=originalAttempt);
        QTRY_VERIFY_WITH_TIMEOUT(win.inCall && win.activeVoice==target,10000);
        QCOMPARE(win.roomPeople(target).size(),1);
        for(const int width:{946,1200,1201,1308,946}){
            win.resize(width,780);QCoreApplication::processEvents();
            QCOMPARE(win.findChild<QWidget*>("sidebar")->width(),242);
            const auto dockGeometry=win.dock->geometry();
            win.note("Не удалось открыть устройство. Выберите микрофон в настройках.");
            QCoreApplication::processEvents();QVERIFY(win.status->isVisible());QCOMPARE(win.dock->geometry(),dockGeometry);
            win.note({});QCoreApplication::processEvents();QCOMPARE(win.dock->geometry(),dockGeometry);
        }
        for(int i=0;i<3;++i){
            feedback.restart();win.joinButton->click();
            QVERIFY(!win.dock->isVisible());QVERIFY(win.emptyRoom->isEnabled());
            qInfo()<<"Leave feedback (ms)"<<feedback.elapsed();
            feedback.restart();win.emptyRoom->click();
            QVERIFY(!win.emptyRoom->isEnabled());
            QVERIFY(win.emptyRoom->text()!=QString("Подключаемся к разговору…") || feedback.elapsed()>=500);
            qInfo()<<"Rejoin feedback (ms)"<<feedback.elapsed();
            QTRY_VERIFY_WITH_TIMEOUT(win.inCall && !win.leavingVoice && win.dock->isVisible(),10000);
            qInfo()<<"Rejoin including media publication (ms)"<<feedback.elapsed();
            QCOMPARE(win.roomPeople(target).size(),1);
        }
        win.idleMediaTimer.setInterval(200);
        win.leaveCall();QTRY_VERIFY(!win.inCall);QTRY_VERIFY_WITH_TIMEOUT(!win.media,5000);
        win.join();QTRY_VERIFY_WITH_TIMEOUT(win.inCall,12000);
        QVERIFY(win.media);QCOMPARE(win.roomPeople(target).size(),1);
        win.leaveCall();QTRY_VERIFY_WITH_TIMEOUT(!win.media,6000);
        const auto mode=qEnvironmentVariable("NIMBO_QA_VOICE_MEMORY");
        if(!mode.isEmpty())voiceMemoryProbe(win,mode);
    }
    void localCameraAndRoomSwitches(){
        if(!qEnvironmentVariableIsSet("NIMBO_QA_LOCAL_DEVICES"))QSKIP("Requires a localhost room and actual devices");
        const auto url=qEnvironmentVariable("NIMBO_NATIVE_TEST_URL5");QVERIFY(QUrl(url).host()=="127.0.0.1");
        QTemporaryDir localDir,remoteDir;DesktopWindow restored(localDir.path()),peer(remoteDir.path(),true);
        auto login=[&](DesktopWindow& w,QString name,QString code){
            w.setAttribute(Qt::WA_DontShowOnScreen);w.resize(1264,784);w.show();
            w.address->setText(url);w.nickname->setText(name);w.password->setText(code);w.loginButton->click();
        };
        login(restored,"Тестер",qEnvironmentVariable("NIMBO_NATIVE_TEST_CODE5"));QTRY_VERIFY_WITH_TIMEOUT(!restored.rooms.isEmpty(),8000);
        QString invite;
        restored.api.request("POST","/api/invitations",{{"seconds",600},{"singleUse",true}},[&](auto data,QString){invite=data.value("code").toString();});
        QTRY_VERIFY_WITH_TIMEOUT(!invite.isEmpty(),5000);
        login(peer,"Собеседник",invite);QTRY_VERIFY_WITH_TIMEOUT(!peer.rooms.isEmpty(),8000);
        QStringList voices;for(auto entry:restored.rooms){auto room=entry.toObject();if(room["kind"]=="voice")voices<<room["id"].toString();}
        QVERIFY(voices.size()>=2);const QString originalRoom=voices[0],adjacent=voices[1];
        auto roomItem=[](DesktopWindow& w,const QString& id)->QListWidgetItem*{
            for(int i=0;i<w.channels->count();++i){auto* item=w.channels->item(i);if(item->data(Qt::UserRole).toJsonObject()["id"]==id)return item;}
            return nullptr;
        };
        auto selectRoom=[&](DesktopWindow& w,const QString& id){auto* item=roomItem(w,id);QVERIFY(item);w.channels->setCurrentItem(item);};
        QVERIFY(restored.api.server.host()=="127.0.0.1");
        QVERIFY(!restored.inCall && !peer.inCall);
        const auto mic=QMediaDevices::defaultAudioInput();
        const auto speaker=QMediaDevices::defaultAudioOutput();
        const auto camera=QMediaDevices::defaultVideoInput();
        qInfo()<<"Available default devices, microphone / output / camera:"<<!mic.isNull()<<!speaker.isNull()<<!camera.isNull();
        QVERIFY2(!mic.isNull()&&!speaker.isNull(),"No default microphone or output on this Windows host");

        restored.openView("room");selectRoom(restored,originalRoom);restored.join();
        QTRY_VERIFY_WITH_TIMEOUT(restored.inCall || !restored.joining,12000);
        QVERIFY2(restored.inCall,qPrintable(restored.status->text()));
        QSignalSpy actualFrames(restored.media.get(),&VoiceEvents::statistics);
        QTRY_VERIFY_WITH_TIMEOUT(!actualFrames.empty() && actualFrames.last()[0].toULongLong()>0,7000);
        for(const auto& room:{adjacent,originalRoom,adjacent,originalRoom}){
            auto* item=roomItem(restored,room);QVERIFY(item);
            restored.channels->scrollToItem(item);QCoreApplication::processEvents();
            item=roomItem(restored,room);QVERIFY(item);
            QTest::mouseClick(restored.channels->viewport(),Qt::LeftButton,Qt::NoModifier,restored.channels->visualItemRect(item).center());
            QTRY_VERIFY2_WITH_TIMEOUT(restored.inCall && !restored.leavingVoice && restored.activeVoice==room,qPrintable(restored.status->text()+" / selected="+restored.selected+" / active="+restored.activeVoice),12000);
        }
        restored.muteButton->click();QTRY_VERIFY(restored.muted);
        QSignalSpy switched(restored.media.get(),&VoiceEvents::audioDeviceSelected);
        restored.media->selectAudioDevice(true,mic.id());restored.media->selectAudioDevice(false,speaker.id());
        QTRY_COMPARE_WITH_TIMEOUT(switched.size(),2,8000);
        for(const auto& result:switched)QVERIFY2(result[2].toString().isEmpty(),qPrintable(result[2].toString()));
        QCOMPARE(restored.inputDevice,mic.id());QCOMPARE(restored.outputDevice,speaker.id());
        QVERIFY(restored.muted && restored.inCall);
        selectRoom(peer,originalRoom);peer.join();QTRY_VERIFY_WITH_TIMEOUT(peer.inCall,10000);
        const auto connection=restored.callIdentity;
        auto* worker=restored.media->workerContext();
        // Stop only the fixture's sink. The system default and other apps remain untouched.
        for(int cycle=0;cycle<3;++cycle){
            QPointer<QAudioSink> previous;
            bool stopped=false;
            QMetaObject::invokeMethod(worker,[&]{
                previous=worker->findChild<QAudioSink*>();
                if(previous){previous->stop();stopped=true;}
            },Qt::BlockingQueuedConnection);
            QVERIFY(stopped);
            auto resumed=[&]{
                bool ok=false;
                QMetaObject::invokeMethod(worker,[&]{
                    auto sinks=worker->findChildren<QAudioSink*>();
                    ok=previous.isNull() && sinks.size()==1 && sinks.first()->state()==QtAudio::ActiveState && sinks.first()->processedUSecs()>0;
                },Qt::BlockingQueuedConnection);
                return ok;
            };
            QTRY_VERIFY_WITH_TIMEOUT(resumed(),5000);
            QCOMPARE(restored.callIdentity,connection);QVERIFY(restored.inCall && restored.muted);
        }
        auto remoteSpeaking=[&]{
            for(int i=0;i<restored.channels->count();++i){
                const auto data=restored.channels->item(i)->data(Qt::UserRole+2).toJsonObject();
                if(data["identity"]==peer.callIdentity)return data["speaking"].toBool();
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(remoteSpeaking(),9000);
        peer.muteButton->click();QTRY_VERIFY(peer.muted);QTRY_VERIFY(!remoteSpeaking());
        if(!camera.isNull()){
            restored.media->changeVideo(false,2,camera.id(),{},{});
            const auto key=restored.callIdentity+"/camera";
            QTRY_VERIFY_WITH_TIMEOUT(!restored.trackFrames.value(key).isNull() || !restored.status->text().isEmpty(),15000);
            QVERIFY2(!restored.trackFrames.value(key).isNull(),qPrintable(restored.status->text()));
            qInfo()<<"Camera preview dimensions:"<<restored.trackFrames[key].size();
            QSignalSpy effectFailures(restored.media.get(),&VoiceEvents::effectFailed);
            for(bool effect:{false,true}){
                restored.updateCameraEffect(effect,20);
                QTest::qWait(750);
                int local=0,received=0;
                auto a=connect(restored.media.get(),&VoiceEvents::participantFrame,&restored,[&](QString id,bool screen,const QImage& frame){if(id.isEmpty()&&!screen&&!frame.isNull())++local;});
                auto b=connect(peer.media.get(),&VoiceEvents::participantFrame,&peer,[&](QString id,bool screen,const QImage& frame){if(id==restored.callIdentity&&!screen&&!frame.isNull())++received;});
                QElapsedTimer sample;sample.start();QTest::qWait(3000);
                const auto seconds=sample.elapsed()/1000.;
                disconnect(a);disconnect(b);
                qInfo()<<"Camera frames/s, blur / local / remote:"<<effect<<local/seconds<<received/seconds;
                QCOMPARE(effectFailures.count(),0);
                QVERIFY2(local/seconds>12 && received/seconds>12,"Camera preview regressed toward the old 10 fps limit");
            }
            restored.media->changeVideo(false,0,{},{},{});
            QTRY_VERIFY(!restored.trackFrames.contains(key));
        }
        peer.leaveCall();QTRY_VERIFY(!peer.inCall);
        restored.media->stop();QTRY_VERIFY(!restored.inCall);
    }
    void scrolledAntivirusUpdate(){
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        bool updating=false;QPointer<QTcpSocket> pending;
        auto reply=[](QTcpSocket* socket,QJsonObject data){
            const auto body=QJsonDocument(data).toJson(QJsonDocument::Compact);
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);
            socket->disconnectFromHost();
        };
        connect(&server,&QTcpServer::newConnection,&server,[&]{
            auto* socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                if(!socket->peek(socket->bytesAvailable()).contains("\r\n\r\n"))return;
                const auto request=socket->readAll();
                if(request.startsWith("POST /api/antivirus/update ")){pending=socket;return;}
                if(request.startsWith("GET /api/server-update ")){
                    reply(socket,{{"phase","current"},{"installed","server-v1.2.3"}});return;
                }
                if(request.startsWith("GET /api/antivirus ")){
                    reply(socket,{{"enabled",true},{"version","ClamAV 1.5.4"},{"scanLimitBytes",80*1048576},
                        {"updateConfigured",true},{"updater",QJsonObject{{"busy",updating}}}});
                }else reply(socket,{});
            });
        });
        QTemporaryDir directory;DesktopWindow win(directory.path());
        win.setAttribute(Qt::WA_DontShowOnScreen);win.resize(1150,600);win.show();
        win.api.server=QUrl("http://127.0.0.1:"+QString::number(server.serverPort()));
        win.pages->setCurrentIndex(1);win.self["role"]="owner";win.openView("general");
        auto* releases=win.detailPage->findChild<QWidget*>("serverUpdates");QVERIFY(releases);
        auto releaseText=[&]{
            QStringList lines;
            for(auto* label:releases->findChildren<QLabel*>())if(label->isVisibleTo(releases))lines<<label->text();
            return lines.join('\n');
        };
        QTRY_VERIFY(releaseText().contains("server-v1.2.3"));
        win.updates->finish(UiText::Id::YouHaveTheLatestVersion);
        QCOMPARE(win.updates->status(),UiText::WindowsApp()+QCoreApplication::applicationVersion());
        QVERIFY(!releaseText().contains(UiText::YouHaveTheLatestVersion()));
        auto* scroll=win.detailPage->findChild<QScrollArea*>("settingsScroll");
        auto* nav=win.detailPage->findChild<QWidget*>("settingsNavPanel");
        auto* antivirus=win.detailPage->findChild<QWidget*>("antivirusSettings");
        QVERIFY(scroll && nav && antivirus);
        auto* button=antivirus->findChild<QPushButton*>();auto* status=antivirus->findChild<QLabel*>("antivirusFeedback");
        QVERIFY(button && status);QTRY_VERIFY(button->isEnabled());
        auto* bar=scroll->verticalScrollBar();QTRY_VERIFY(bar->maximum()>0);
        const auto navAt=nav->mapTo(&win,QPoint());bar->setValue(bar->maximum());
        QCoreApplication::processEvents();QCOMPARE(nav->mapTo(&win,QPoint()),navAt);
        const auto buttonAt=button->mapTo(&win,QPoint());const int range=bar->maximum();
        QVERIFY(!button->visibleRegion().isEmpty());QTest::mouseClick(button,Qt::LeftButton);
        QTRY_VERIFY(pending);QVERIFY(status->isVisible() && !button->isEnabled());
        QVERIFY(status->mapTo(&win,QPoint()).x()>=buttonAt.x()+button->width());
        QCOMPARE(button->mapTo(&win,QPoint()),buttonAt);QCOMPARE(bar->maximum(),range);
        updating=true;reply(pending,{{"busy",true}});QTest::qWait(200);
        QCOMPARE(nav->mapTo(&win,QPoint()),navAt);QVERIFY(!button->isEnabled());
        const auto output=qEnvironmentVariable("NIMBO_SETTINGS_SHOTS");
        if(!output.isEmpty()){QDir().mkpath(output);QVERIFY(win.grab().save(output+"/scrolled-antivirus.png"));}
        updating=false;QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(),12000);
        QVERIFY(!status->isVisible());QCOMPARE(bar->maximum(),range);
        QCOMPARE(button->mapTo(&win,QPoint()),buttonAt);QCOMPARE(nav->mapTo(&win,QPoint()),navAt);
        for(auto* tab:nav->findChildren<QPushButton*>())QVERIFY(!tab->visibleRegion().isEmpty());
        win.resize(730,600);QTRY_VERIFY(scroll->isAncestorOf(nav));
        win.resize(1150,600);QTRY_VERIFY(!scroll->isAncestorOf(nav));
        bar->setValue(bar->maximum());QTRY_COMPARE(nav->mapTo(&win,QPoint()),navAt);
    }
    void settingsParity(){
        const auto out=qEnvironmentVariable("NIMBO_SETTINGS_SHOTS");if(out.isEmpty())QSKIP("Use --settings-shots");
        QTemporaryDir directory;QVERIFY(directory.isValid());DesktopWindow win(directory.path());
        win.setAttribute(Qt::WA_DontShowOnScreen);win.resize(1264,784);win.show();
        auto login=[](DesktopWindow& w,QString code,QString name){
            w.address->setText(qEnvironmentVariable("NIMBO_NATIVE_TEST_URL"));
            w.findChild<QLineEdit*>("nickname")->setText(name);
            w.findChild<QLineEdit*>("accessCode")->setText(code);w.findChild<QPushButton*>("primary")->click();
        };
        login(win,qEnvironmentVariable("NIMBO_NATIVE_TEST_CODE"),"Владелец");QTRY_VERIFY(win.self["role"]=="owner");
        win.settingsButton->click();QCOMPARE(win.view,QString("general"));generalSettings(win);if(QTest::currentTestFailed())return;
        // Observe the same saved policy through a newly constructed settings page.
        win.openView("general");QLineEdit* field=nullptr;for(auto* e:win.detailPage->findChildren<QLineEdit*>())if(e->accessibleName().endsWith("MB"))field=e;
        QVERIFY(field);QTRY_COMPARE(field->text(),QString("819"));
        QString invite;win.api.request("POST","/api/invitations",{{"seconds",600},{"singleUse",true}},[&](QJsonObject data,QString){invite=data["code"].toString();});
        QTRY_VERIFY(!invite.isEmpty());QTemporaryDir guestDir;DesktopWindow peer(guestDir.path());
        peer.setAttribute(Qt::WA_DontShowOnScreen);peer.resize(1264,784);peer.show();login(peer,invite,"Участник");
        QTRY_VERIFY(!peer.selfId.isEmpty());QVERIFY(peer.self["role"]!="owner");
        for(int width:{1264,730}){
            peer.resize(width,784);peer.openView("general");QTest::qWait(250);
            QVERIFY(!peer.detailPage->findChild<QWidget*>("antivirusSettings"));QVERIFY(!peer.detailPage->findChild<QWidget*>("storageMeter"));
            QVERIFY(peer.grab().save(out+QString("/member-general-%1.png").arg(width)));
        }
    }
    void realWorkspace() {
        auto url=qEnvironmentVariable("NIMBO_NATIVE_TEST_URL");
        if(url.isEmpty())QSKIP("Run test-native-desktop.py for an isolated server");
        QTemporaryDir directory;QVERIFY(directory.isValid());
        DesktopWindow win(directory.path());win.setAttribute(Qt::WA_DontShowOnScreen);win.resize(1264,784);win.show();
        auto* pages=win.findChild<QStackedWidget*>();QVERIFY(pages);
        win.findChild<QLineEdit*>("serverAddress")->setText(url);
        win.findChild<QLineEdit*>("nickname")->setText("Тестер");
        win.findChild<QLineEdit*>("accessCode")->setText(qEnvironmentVariable("NIMBO_NATIVE_TEST_CODE"));
        win.findChild<QPushButton*>("primary")->click();
        QTRY_COMPARE_WITH_TIMEOUT(pages->currentIndex(),1,10000);
        auto channels=win.findChild<QListWidget*>("channels");QTRY_VERIFY_WITH_TIMEOUT(channels->count()>=2,10000);
        {
            QTemporaryDir importedDir;QVERIFY(importedDir.isValid());
            DesktopWindow imported(importedDir.path());imported.setAttribute(Qt::WA_DontShowOnScreen);imported.show();
            imported.address->setText(url);imported.loadLoginFile(QJsonDocument(win.profile).toJson());
            QTRY_COMPARE_WITH_TIMEOUT(imported.view,QString("profile"),10000);
            QTRY_COMPARE_WITH_TIMEOUT(imported.selfId,win.selfId,10000);QVERIFY(!imported.api.token.isEmpty());
        }
        int textChannel=-1, voiceChannel=-1;
        for(int i=0;i<channels->count();++i) {
            auto room=channels->item(i)->data(Qt::UserRole).toJsonObject();
            if(room["kind"]=="text")textChannel=i;else if(room["kind"]=="voice")voiceChannel=i;
        }
        QVERIFY(textChannel>=0&&voiceChannel>=0);
        {
            channels->setCurrentRow(voiceChannel);
            const auto channel=win.selected;
            win.openView("general");
            auto* scroll=win.detailLayout->itemAt(0)->widget()->findChild<QScrollArea*>("settingsScroll");
            QVERIFY(scroll);
            QTRY_VERIFY(scroll->verticalScrollBar()->maximum()>0);
            scroll->verticalScrollBar()->setValue(75);
            auto route=win.updates->restartView();
            QCOMPARE(route["scroll"].toInt(),75);
            DesktopWindow reopened(directory.path(),false,route);
            reopened.setAttribute(Qt::WA_DontShowOnScreen);reopened.show();
            QTRY_COMPARE(reopened.view,QString("general"));
            QCOMPARE(reopened.api.server,win.api.server);
            QCOMPARE(reopened.selected,channel);
            auto* settings=reopened.detailLayout->itemAt(0)->widget()->findChild<QScrollArea*>("settingsScroll");
            QVERIFY(settings);
            QTRY_COMPARE(settings->verticalScrollBar()->value(),75);
            reopened.backButton->click();
            QCOMPARE(reopened.selected,channel);
            QCOMPARE(reopened.view,QString("room"));
            QVERIFY(!reopened.joining && !reopened.inCall);
            route["profile"]="different-profile";
            DesktopWindow different(directory.path(),false,route);
            QCOMPARE(different.pages->currentWidget(),different.localHome);
            QVERIFY(different.switchTarget.isEmpty());
            win.openView("room");
        }
        channels->setCurrentRow(textChannel);
        auto* edit=win.findChild<QPlainTextEdit*>("messageText");
        edit->setPlainText("Проверка нативного клиента");edit->selectAll();
        QTest::keyClick(edit,Qt::Key_B,Qt::ControlModifier);edit->moveCursor(QTextCursor::End);
        QTest::keyClick(edit,Qt::Key_Return);
        auto* messages=win.findChild<QListWidget*>("messages");
        QTRY_VERIFY_WITH_TIMEOUT(!messages->findItems("Проверка нативного клиента",Qt::MatchContains).isEmpty(),10000);
        channels->setCurrentRow(voiceChannel);QVERIFY(!edit->isVisible());channels->setCurrentRow(textChannel);
        QTRY_VERIFY_WITH_TIMEOUT(messages->count()>0,10000);
        QCOMPARE(win.lastMessages.first().toObject()["format"].toString(),QString(QStringLiteral("Проверка нативного клиента").size(),'1'));
        if(auto shot=qEnvironmentVariable("NIMBO_NATIVE_TEST_SCREENSHOT");!shot.isEmpty())QVERIFY(win.grab().save(shot));
        win.api.token="expired-test-session";
        bool recovered=false;QString recoveryError;
        win.api.request("GET","/api/workspace",{},[&](QJsonObject result,QString err){recovered=!result["selfId"].toString().isEmpty();recoveryError=err;});
        QTRY_VERIFY_WITH_TIMEOUT(recovered||!recoveryError.isEmpty(),8000);
        QVERIFY2(recovered,qPrintable(recoveryError));QVERIFY(!win.api.token.isEmpty() && win.api.token!="expired-test-session");
        const QFontInfo profileFont(win.profileName->font());
        QCOMPARE(profileFont.pixelSize(),12);
        QCOMPARE(profileFont.weight(),QFont::Medium);
        for(int cycle=0;cycle<3;++cycle){
            for(const auto section:{"profile","general","audio","video","channels","roles","keys"}){
                win.openView(section);
                QPointer<QWidget> page=win.detailLayout->itemAt(0)->widget();QVERIFY(page);
                win.openView("room");
                QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
                QVERIFY(page.isNull());QCOMPARE(win.detailLayout->count(),0);
            }
        }
        messageActions(win);
        if(QTest::currentTestFailed())return;
        QTest::keyClicks(win.compose,"* one");
        QTest::keyClick(win.compose,Qt::Key_Return,Qt::ShiftModifier);
        QTest::keyClicks(win.compose,"two");
        QCOMPARE(win.compose->toPlainText(),QString("• one\n• two"));
        QTest::keyClick(win.compose,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!win.lastMessages.isEmpty() && win.lastMessages.last().toObject()["text"].toString()=="* one\n* two",4000);
        QCOMPARE(win.lastMessages.last().toObject()["format"].toString(),QString("44000044"));
        fileActions(win);
        if(QTest::currentTestFailed())return;
        longChatReactions(win);
        if(QTest::currentTestFailed())return;
        invitations(win);
        if(QTest::currentTestFailed())return;
        generalSettings(win);
        if(QTest::currentTestFailed())return;
        channelChanges(win);
        win.findChild<QPushButton*>("profileNav")->click();auto buttons=win.findChildren<QPushButton*>();
        for(auto* b:buttons)if(b->objectName()=="logout")b->click();
        QTRY_COMPARE_WITH_TIMEOUT(pages->currentIndex(),0,10000);
        DesktopWindow restored(directory.path(),true);restored.setAttribute(Qt::WA_DontShowOnScreen);restored.resize(1264,784);restored.show();
        QSignalSpy transitions(restored.pages,&QStackedWidget::currentChanged);
        auto invalid=restored.profile["servers"].toArray().first().toObject();
        invalid["profileKey"]=QString::fromLatin1(DesktopProfile::randomKey());
        restored.switchServer(invalid);QVERIFY(restored.switchNotice->isVisibleTo(&restored));
        restored.switchAction->click();QVERIFY(!restored.switchNotice->isVisible());QVERIFY(!restored.switchProbe);
        restored.switchServer(invalid);
        QTRY_VERIFY_WITH_TIMEOUT(restored.localHome->isEnabled() && restored.localStatus->isVisible(),5000);
        QVERIFY(!restored.localStatus->text().isEmpty());QVERIFY(restored.switchNotice->isVisibleTo(&restored));QCOMPARE(restored.switchAction->text(),QString("Закрыть"));restored.switchAction->click();
        QCOMPARE(restored.pages->currentWidget(),restored.localHome);QCOMPARE(transitions.size(),0);
        auto* serverLink=restored.localHome->findChild<QPushButton*>("openSavedServer");
        QVERIFY(serverLink);
        auto ownerBadge=[](QWidget* parent)->QLabel*{
            for(auto* label:parent->findChildren<QLabel*>())
                if(label->accessibleName()=="Владелец" && label->isVisibleTo(parent))return label;
            return nullptr;
        };
        auto* savedCrown=ownerBadge(serverLink);
        QVERIFY(savedCrown);QCOMPARE(savedCrown->toolTip(),QString("Владелец при последнем входе"));
        QTest::mouseClick(serverLink,Qt::LeftButton,Qt::NoModifier,savedCrown->mapTo(serverLink,savedCrown->rect().center()));
        QCOMPARE(restored.pages->currentWidget(),restored.localHome);
        QTRY_COMPARE_WITH_TIMEOUT(restored.findChild<QStackedWidget*>()->currentIndex(),1,10000);
        QTRY_VERIFY_WITH_TIMEOUT(!restored.rooms.isEmpty(),5000);
        QCOMPARE(restored.memberPanel->geometry().right()+9,restored.memberPanel->parentWidget()->width());
        QCOMPARE(transitions.size(),1);QCOMPARE(transitions.first().first().toInt(),1);
        restored.openView("profile");
        QCOMPARE(restored.self["role"].toString(),QString("owner"));
        QTRY_VERIFY_WITH_TIMEOUT(ownerBadge(restored.detailPage),3000);
        auto* currentCrown=ownerBadge(restored.detailPage);
        QCOMPARE(currentCrown->toolTip(),QString("Вы владелец"));
        restored.openView("room");
        QString invite,inviteError;
        restored.api.request("POST","/api/invitations",{{"seconds",3600},{"singleUse",false}},[&](auto data,QString err){invite=data.value("code").toString();inviteError=err;});
        QTRY_VERIFY_WITH_TIMEOUT(!invite.isEmpty()||!inviteError.isEmpty(),5000);QVERIFY2(inviteError.isEmpty(),qPrintable(inviteError));
        QTemporaryDir otherDir;DesktopWindow peer(otherDir.path(),true);peer.setAttribute(Qt::WA_DontShowOnScreen);peer.show();
        peer.address->setText(url);peer.nickname->setText("Собеседник");peer.password->setText(invite);peer.loginButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(!peer.rooms.isEmpty(),5000);
        peer.openView("profile");
        auto* peerLink=peer.detailPage->findChild<QPushButton*>("openSavedServer");QVERIFY(peerLink);
        QTRY_VERIFY_WITH_TIMEOUT(peerLink->isVisibleTo(peer.detailPage),3000);
        QVERIFY(!ownerBadge(peer.detailPage));peer.openView("room");
        peer.removeKeys();peer.profile["hotkeys"]=QJsonObject{};
        auto choose=[&](DesktopWindow& w,QString type){for(int i=0;i<w.channels->count();++i)if(w.channels->item(i)->data(Qt::UserRole).toJsonObject().value("kind")==type){w.channels->setCurrentRow(i);return;}QFAIL("Missing channel");};
        choose(restored,"voice");choose(peer,"voice");restored.emptyRoom->click();peer.emptyRoom->click();
        QTRY_VERIFY_WITH_TIMEOUT(restored.inCall&&peer.inCall,20000);
        const auto originalRoom=restored.activeVoice;
        auto countMember=[&](DesktopWindow& observer,QString room,QString id){
            int count=0;
            for(auto entry:observer.roomPeople(room))
                if(entry.toObject()["identity"].toString().section('.',0,0)==id)++count;
            return count;
        };
        QTRY_COMPARE(countMember(peer,originalRoom,restored.selfId),1);
        auto oldSession=restored.callIdentity;
        restored.leaveCall();restored.join();
        QTRY_VERIFY_WITH_TIMEOUT(restored.inCall && !restored.callIdentity.isEmpty() && restored.callIdentity!=oldSession,10000);
        auto seesCurrent=[&]{for(auto entry:peer.roomPeople(originalRoom))if(entry.toObject()["identity"]==restored.callIdentity)return true;return false;};
        QTRY_VERIFY_WITH_TIMEOUT(seesCurrent() && countMember(peer,originalRoom,restored.selfId)==1,5000);
        QString adjacent;
        for(auto value:restored.rooms){auto room=value.toObject();if(room["kind"]=="voice" && room["id"]!=originalRoom){adjacent=room["id"].toString();break;}}
        QVERIFY(!adjacent.isEmpty());
        auto selectId=[&](QString id){for(int i=0;i<restored.channels->count();++i)if(restored.channels->item(i)->data(Qt::UserRole).toJsonObject()["id"]==id){restored.channels->setCurrentRow(i);return;}QFAIL("Missing voice room");};
        selectId(adjacent);restored.join();QTRY_VERIFY_WITH_TIMEOUT(restored.inCall && restored.activeVoice==adjacent,10000);
        QTRY_COMPARE_WITH_TIMEOUT(countMember(peer,originalRoom,restored.selfId),0,5000);
        selectId(originalRoom);restored.join();QTRY_VERIFY_WITH_TIMEOUT(restored.inCall && restored.activeVoice==originalRoom,10000);
        QTRY_VERIFY_WITH_TIMEOUT(seesCurrent() && countMember(peer,originalRoom,restored.selfId)==1,5000);
        peer.refresh();QTRY_COMPARE_WITH_TIMEOUT(countMember(peer,adjacent,restored.selfId),0,5000);
        QCOMPARE(countMember(restored,originalRoom,restored.selfId),1);
        auto ownSidebarRow=[&]() -> QListWidgetItem* {
            for(int i=0;i<restored.channels->count();++i){
                auto* item=restored.channels->item(i);
                auto identity=item->data(Qt::UserRole+2).toJsonObject()["identity"].toString();
                if(!identity.isEmpty() && identity.section('.',0,0)==restored.selfId)return item;
            }
            return nullptr;
        };
        auto controls=[&]()->QWidget*{
            for(int i=0;i<restored.channels->count();++i){
                auto* item=restored.channels->item(i);
                if(item->data(Qt::UserRole).toJsonObject()["id"]==originalRoom)return restored.channels->itemWidget(item);
            }
            return nullptr;
        };
        QTRY_VERIFY(controls());
        auto* channelLabel=controls()->findChild<QLabel*>();QVERIFY(channelLabel);
        const auto rowRect=restored.channels->visualItemRect(restored.channels->currentItem());
        QCOMPARE(channelLabel->mapTo(restored.channels->viewport(),QPoint()).x(),rowRect.left()+40);
        QVERIFY(channelLabel->width()>=channelLabel->fontMetrics().horizontalAdvance(channelLabel->text()));
        QTRY_VERIFY(ownSidebarRow());
        const auto selectedBeforeClick=restored.selected;
        const auto ownPoint=restored.channels->visualItemRect(ownSidebarRow()).center();
        QVERIFY(!(ownSidebarRow()->flags()&Qt::ItemIsSelectable));
        QTest::mouseMove(restored.channels->viewport(),ownPoint);
        QTest::mouseClick(restored.channels->viewport(),Qt::LeftButton,Qt::NoModifier,ownPoint);
        QCOMPARE(restored.selected,selectedBeforeClick);QVERIFY(!selectedBeforeClick.isEmpty());QVERIFY(restored.inCall);
        choose(restored,"text");
        restored.setMemberPanel(false,false);
        QCoreApplication::processEvents();
        QVERIFY(restored.dock->isVisible());
        auto dockRight=[&]{return restored.dock->mapTo(&restored,QPoint(restored.dock->width(),0)).x();};
        const int collapsedRight=dockRight();
        for(qreal fraction:{0.25,0.5,0.75,1.0}){
            restored.rosterProgress=fraction;restored.layoutMemberPanel();QCoreApplication::processEvents();
            QVERIFY(qAbs(dockRight()-collapsedRight)<=1);
        }
        restored.setMemberPanel(true,false);
        choose(restored,"voice");
        {
            QTemporaryDir deviceDir;
            DesktopWindow sameProfile(deviceDir.path(),true);
            sameProfile.setAttribute(Qt::WA_DontShowOnScreen);sameProfile.show();
            sameProfile.profile=restored.profile;sameProfile.openLocalHome();
            sameProfile.localHome->findChild<QPushButton*>("openSavedServer")->click();
            QTRY_COMPARE(sameProfile.pages->currentIndex(),1);
            choose(sameProfile,"voice");
            auto otherSessionRow=[&]()->QListWidgetItem*{
                for(int row=0;row<sameProfile.channels->count();++row){
                    auto* item=sameProfile.channels->item(row);
                    if(item->data(Qt::UserRole+2).toJsonObject()["identity"]==restored.callIdentity)return item;
                }
                return nullptr;
            };
            QTRY_VERIFY_WITH_TIMEOUT(otherSessionRow(),7000);
            QVERIFY(otherSessionRow()->text().endsWith(" · вы"));
            QVERIFY(!sameProfile.inCall && !sameProfile.joining);
            auto* voiceItem=sameProfile.channels->currentItem();QVERIFY(voiceItem);
            QTest::mouseClick(sameProfile.channels->viewport(),Qt::LeftButton,Qt::NoModifier,sameProfile.channels->visualItemRect(voiceItem).center());
            QTRY_VERIFY2_WITH_TIMEOUT(sameProfile.inCall,qPrintable("Выбран канал "+sameProfile.selected+"; статус "+sameProfile.status->text()),10000);
            QCOMPARE(sameProfile.selfId,restored.selfId);
            QVERIFY(sameProfile.callIdentity!=restored.callIdentity);
            auto stageSessions=[&]{
                QSet<QString> identities;
                for(auto* tile:restored.stageCards->findChildren<QWidget*>()){
                    auto identity=tile->property("connectionIdentity").toString();
                    if(!identity.isEmpty() && tile->isVisibleTo(restored.stageCards))identities.insert(identity);
                }
                return identities;
            };
            QTRY_COMPARE_WITH_TIMEOUT(stageSessions(),(QSet<QString>{restored.callIdentity,peer.callIdentity,sameProfile.callIdentity}),7000);
            sameProfile.media->changeVideo(false,1,{},{},{});
            QTRY_VERIFY_WITH_TIMEOUT(restored.trackFrames.contains(sameProfile.callIdentity+"/camera"),7000);
            QVERIFY(!restored.trackFrames.contains(restored.callIdentity+"/camera"));
            QTRY_VERIFY(sameProfile.localPerson()["speaking"].toBool());
            sameProfile.muteButton->click();
            QVERIFY(!sameProfile.localPerson()["speaking"].toBool());
            QVERIFY(!sameProfile.localPerson()["mic"].toBool());
            auto remoteMic=[&]{
                for(const auto p:restored.liveRoster)
                    if(p.toObject()["identity"]==sameProfile.callIdentity)return p.toObject()["mic"].toBool();
                return true;
            };
            QTRY_VERIFY_WITH_TIMEOUT(!remoteMic(),5000);
            QVERIFY(sameProfile.callSounds.contains("voice-mute"));
            QTRY_COMPARE_WITH_TIMEOUT(sameProfile.callSounds["voice-mute"]->status(),QSoundEffect::Ready,4000);
            sameProfile.muteButton->click();
            QTRY_VERIFY_WITH_TIMEOUT(remoteMic(),5000);
            sameProfile.media->changeVideo(false,0,{},{},{});
            QTRY_VERIFY_WITH_TIMEOUT(!restored.trackFrames.contains(sameProfile.callIdentity+"/camera"),7000);
            for(int cycle=0;cycle<3;++cycle){
                sameProfile.media->changeVideo(false,1,{},{},{});
                QTRY_VERIFY_WITH_TIMEOUT(restored.trackFrames.contains(sameProfile.callIdentity+"/camera"),7000);
                sameProfile.media->changeVideo(false,0,{},{},{});
                QTRY_VERIFY_WITH_TIMEOUT(!restored.trackFrames.contains(sameProfile.callIdentity+"/camera"),7000);
            }
            sameProfile.joinButton->click();
            QTRY_VERIFY_WITH_TIMEOUT(!sameProfile.inCall,7000);
            QTRY_COMPARE_WITH_TIMEOUT(stageSessions(),(QSet<QString>{restored.callIdentity,peer.callIdentity}),7000);
            QVERIFY(restored.inCall);
            for(const auto cue:{"join","leave","mute","unmute","video-on","video-off"}){
                const QString key=QStringLiteral("voice-")+cue;
                QTRY_VERIFY2_WITH_TIMEOUT(restored.callSounds.contains(key),qPrintable(key),4000);
                QTRY_COMPARE_WITH_TIMEOUT(restored.callSounds[key]->status(),QSoundEffect::Ready,4000);
            }
        }
        restored.media->changeVideo(false,1,{},{},{});
        peer.media->changeVideo(false,1,{},{},{});
        QSignalSpy stats(restored.media.get(),&VoiceEvents::statistics);
        QTRY_VERIFY_WITH_TIMEOUT(!stats.empty()&&stats.last()[1].toULongLong()>0&&stats.last()[2].toULongLong()>0,15000);
        QTRY_VERIFY_WITH_TIMEOUT(!restored.trackFrames.isEmpty()&&!restored.liveRoster.isEmpty(),5000);
        auto card=[&](QString name)->QWidget*{for(auto* w:restored.stageCards->findChildren<QWidget*>(name))if(w->isVisibleTo(restored.stageCards))return w;return nullptr;};
        QTRY_VERIFY(card("cameraCard")&&card("voiceCard"));
        QCOMPARE(card("voiceCard")->size(),QSize(230,190));QCOMPARE(card("cameraCard")->size(),QSize(380,261));
        QCOMPARE(card("voiceCard")->mapTo(restored.stageCards,QPoint()).y(),card("cameraCard")->mapTo(restored.stageCards,QPoint()).y());
        auto wide=restored.size();restored.resize(850,wide.height());QTRY_VERIFY(card("cameraCard")&&card("voiceCard")&&card("voiceCard")->mapTo(restored.stageCards,QPoint()).y()!=card("cameraCard")->mapTo(restored.stageCards,QPoint()).y());
        restored.resize(wide);QTRY_VERIFY(card("cameraCard")&&card("voiceCard")&&card("voiceCard")->mapTo(restored.stageCards,QPoint()).y()==card("cameraCard")->mapTo(restored.stageCards,QPoint()).y());
        peer.media->changeVideo(true,1,{},{},{});
        QTRY_VERIFY_WITH_TIMEOUT(card("screenCard")&&card("cameraCard"),8000);
        auto* group=card("screenCard")->parentWidget();QCOMPARE(group->objectName(),QString("shareGroup"));
        const auto capture=group->grab().toImage();
        for(auto* tile:group->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)){
            const auto pixel=tile->mapTo(group,QPoint(tile->width()-2,tile->height()-2));
            QCOMPARE(capture.pixelColor(pixel*capture.devicePixelRatio()),QColor("#15191b"));
        }
        auto* expand=card("screenCard")->findChild<QPushButton*>();QVERIFY(expand);expand->click();
        auto* viewer=restored.findChild<QWidget*>("streamViewer");QTRY_VERIFY(viewer&&viewer->isVisible());
        auto viewerButton=[&](const QString& name){
            for(auto* b:viewer->findChildren<QPushButton*>())if(b->accessibleName()==name)return b;
            return static_cast<QPushButton*>(nullptr);
        };
        auto* minimize=viewerButton("Свернуть трансляцию");QVERIFY(minimize);minimize->click();
        QTRY_COMPARE(viewer->width(),320);QVERIFY(viewerButton("Развернуть трансляцию"));
        viewerButton("Развернуть трансляцию")->click();QVERIFY(viewer->width()>320);
        choose(restored,"text");QTRY_COMPARE(viewer->width(),320);
        auto* surface=viewer->findChild<QWidget*>("streamSurface");QVERIFY(surface);
        auto drag=[&](QWidget* target,QPoint delta){
            const auto center=target->rect().center();
            QTest::mousePress(target,Qt::LeftButton,Qt::NoModifier,center);
            QTest::mouseMove(target,center+delta);
            QTest::mouseRelease(target,Qt::LeftButton,Qt::NoModifier,center);
        };
        const auto before=viewer->pos();drag(surface,{-88,30});
        QVERIFY(viewer->x()<before.x());QVERIFY(viewer->y()>before.y());
        const auto moved=viewer->pos();restored.syncViewer(false);QCOMPARE(viewer->pos(),moved);
        auto* grip=viewer->findChild<QWidget*>("streamResizeGrip");QVERIFY(grip);
        const int oldWidth=viewer->width();drag(grip,{-32,16});QVERIFY(viewer->width()>oldWidth);
        auto* sound=viewerButton("Заглушить трансляцию у себя");QVERIFY(sound);sound->click();
        QVERIFY(viewerButton("Включить звук трансляции у себя"));QVERIFY(!restored.mutedStreams.isEmpty());
        drag(surface,{-3000,3000});
        const auto* area=restored.messages->viewport();
        QVERIFY(QRect(area->mapTo(&restored,QPoint()),area->size()).contains(viewer->geometry()));
        choose(restored,"voice");QTRY_VERIFY(!viewer->isVisible());
        QTRY_VERIFY(card("screenCard"));
        expand=card("screenCard")->findChild<QPushButton*>();QVERIFY(expand);expand->click();QTRY_VERIFY(viewer->isVisible());
        peer.media->changeVideo(true,0,{},{},{});
        QTRY_VERIFY(!viewer->isVisible());
        if(qEnvironmentVariableIsSet("NIMBO_QA_CAPTURE_WINDOW")){
            const auto windowName="Nimbo — QA "+QUuid::createUuid().toString(QUuid::WithoutBraces);
            QProcess target;
            target.start(QCoreApplication::applicationFilePath(),{"--capture-target",windowName});
            QVERIFY(target.waitForStarted(5000));
            const auto stopTarget=qScopeGuard([&]{
                target.terminate();
                if(!target.waitForFinished(2000)){target.kill();target.waitForFinished(2000);}
            });
            QVERIFY2(target.waitForReadyRead(5000), "The capture target did not paint its first frame");
            QCOMPARE(target.readLine().trimmed(),QByteArray("painted"));
            auto source=[&]{
                for(const auto& w:QWindowCapture::capturableWindows())if(w.description()==windowName)return w;
                return QCapturableWindow{};
            };
            QTRY_VERIFY_WITH_TIMEOUT(source().isValid(),3500);
            QElapsedTimer captureTime;captureTime.start();
            QTimer::singleShot(0,&peer,[&]{
                auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if(!dialog)dialog=peer.findChild<QDialog*>("sharePicker");
                QVERIFY(dialog);QTimer::singleShot(3000,dialog,&QDialog::reject);
                QVERIFY(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
                auto* confirm=dialog->findChild<QPushButton*>("confirm");QVERIFY(confirm && !confirm->isEnabled());
                auto checkCards=[&]{
                    const auto cards=dialog->findChildren<QPushButton*>("source");
                    QVERIFY(!cards.isEmpty());
                    for(auto* card:cards){
                        card->layout()->activate();
                        const auto labels=card->findChildren<QLabel*>(Qt::FindDirectChildrenOnly);
                        QCOMPARE(labels.size(),2);
                        QVERIFY(card->rect().contains(labels[0]->geometry()));
                        QVERIFY(card->rect().contains(labels[1]->geometry()));
                        QVERIFY(labels[1]->y()>labels[0]->geometry().bottom());
                        QVERIFY(!labels[1]->text().isEmpty());
                    }
                    if(cards.size()>3)QVERIFY(cards[3]->y()>cards[0]->geometry().bottom());
                };
                checkCards();
                dialog->findChild<QPushButton*>("source")->click();
                QVERIFY(confirm->isEnabled());
                for(auto* tab:dialog->findChildren<QPushButton*>())if(tab->text()=="Программы"){tab->click();break;}
                QCoreApplication::processEvents();checkCards();QVERIFY(!confirm->isEnabled());
                for(auto* button:dialog->findChildren<QPushButton*>("source"))
                    if(button->accessibleName()==windowName){
                        auto* scroll=dialog->findChild<QScrollArea*>();
                        scroll->ensureWidgetVisible(button);QCoreApplication::processEvents();
                        QTest::mouseClick(button,Qt::LeftButton,Qt::NoModifier,button->rect().center());break;
                    }
                QVERIFY(confirm->isEnabled());confirm->click();
            });
            peer.chooseVideo(true);
            QTRY_VERIFY_WITH_TIMEOUT(peer.screenMode==4 || !peer.status->text().isEmpty(),5000);
            QVERIFY2(peer.screenMode==4,qPrintable(peer.status->text()));
            const auto track=peer.callIdentity+"/screen";
            QTRY_VERIFY_WITH_TIMEOUT(!peer.trackFrames.value(track).isNull() || peer.screenMode!=4,20000);
            QVERIFY2(!peer.trackFrames.value(track).isNull(),qPrintable(peer.status->text()));
            qInfo()<<"Window capture first preview (ms)"<<captureTime.elapsed();
            auto greenFrame=[](const QImage& frame){
                if(frame.isNull())return false;
                const auto color=frame.pixelColor(frame.width()/2,frame.height()/2);
                return color.green()>150 && color.red()<85 && color.blue()<120;
            };
            auto previewColor=[&]{
                const auto frame=peer.trackFrames.value(track);
                return frame.isNull()?QString("missing"):frame.pixelColor(frame.width()/2,frame.height()/2).name();
            };
            QTRY_VERIFY2_WITH_TIMEOUT(greenFrame(peer.trackFrames.value(track)),qPrintable("Capture preview center: "+previewColor()),8000);
            const auto received=[&]{
                const auto frame=restored.trackFrames.value(peer.callIdentity+"/screen");
                if(frame.isNull())return false;
                const auto color=frame.pixelColor(frame.width()/2,frame.height()/2);
                return color.green()>150 && color.red()<85 && color.blue()<120;
            };
            QTRY_VERIFY_WITH_TIMEOUT(received(),10000);
            qInfo()<<"Window capture first decoded color (ms)"<<captureTime.elapsed();
            peer.media->changeVideo(true,0,{},{},{});
            QTRY_VERIFY_WITH_TIMEOUT(!restored.trackFrames.contains(peer.callIdentity+"/screen"),8000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!card("screenCard")&&card("cameraCard"),8000);
        auto control=[&](const QString& caption)->QPushButton*{
            for(auto* b:restored.stageCards->findChildren<QPushButton*>("mediaModeration"))
                if(b->isVisibleTo(restored.stageCards)&&b->accessibleName()==caption)return b;
            return nullptr;
        };
        QTRY_VERIFY(control("Выключить микрофон: Собеседник"));
        control("Выключить микрофон: Собеседник")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!restored.mediaModerationBusy,10000);
        QVERIFY2(restored.status->text().isEmpty(),qPrintable(restored.status->text()));
        QTRY_VERIFY_WITH_TIMEOUT(peer.muted,8000);
        peer.muteButton->click();QTRY_VERIFY(!peer.muted);
        restored.memberMenu(restored.selfId,restored.mapToGlobal(QPoint(400,200)));
        QVERIFY(!restored.findChild<QFrame*>("participantMenu"));
        restored.memberMenu(peer.selfId,restored.mapToGlobal(QPoint(400,200)));
        auto* menu=restored.findChild<QFrame*>("participantMenu");QVERIFY(menu);
        auto* volume=menu->findChild<QSlider*>();QVERIFY(volume);volume->setValue(15);
        QCOMPARE(QSettings(restored.dataPath+"/desktop.ini",QSettings::IniFormat).value("volume/"+url+"/"+peer.selfId).toInt(),75);
        menu->close();
        restored.muteButton->click();QVERIFY(restored.muted);
        auto shot=qEnvironmentVariable("NIMBO_NATIVE_TEST_SCREENSHOT");QTest::qWait(120);
        QVERIFY(restored.grab().save(QFileInfo(shot).absolutePath()+"/voice-room.png"));
        auto second=qEnvironmentVariable("NIMBO_NATIVE_TEST_URL2");QVERIFY(!second.isEmpty());
        restored.switchServer({{"url",second},{"displayName","Тестер"},{"profileKey",QString::fromLatin1(DesktopProfile::randomKey())}},true,qEnvironmentVariable("NIMBO_NATIVE_TEST_CODE2"));
        QTRY_COMPARE_WITH_TIMEOUT(restored.api.server,QUrl(second),8000);QVERIFY(restored.inCall&&restored.muted);QCOMPARE(restored.callOrigin,url);
        QCOMPARE(restored.dock->parentWidget(),restored.header);
        QVERIFY(restored.callReturn->isVisible());
        QVERIFY(restored.callServer->mapTo(restored.callReturn,QPoint()).y()<restored.callLabel->mapTo(restored.callReturn,QPoint()).y());
        const auto identityBeforeReturn=restored.callIdentity;
        QElapsedTimer switchClock;switchClock.start();
        restored.callReturn->click();QCOMPARE(restored.api.server,QUrl(url));QCOMPARE(restored.selected,originalRoom);
        qInfo()<<"Prepared server return (click through workspace render), ms:"<<switchClock.elapsed();
        QCOMPARE(restored.callIdentity,identityBeforeReturn);
        for(const auto& value:restored.profile["servers"].toArray())if(value.toObject()["url"]==second)restored.switchServer(value.toObject());
        QTRY_COMPARE(restored.api.server,QUrl(second));
        choose(restored,"text");QTRY_VERIFY_WITH_TIMEOUT(!restored.loadingMessages,5000);QVERIFY(restored.lastMessages.isEmpty());
        bool newRoom=false;
        restored.api.request("POST","/api/channels",{{"name","Комната второго сервера"},{"kind","text"},{"section","text"}},[&](QJsonObject,QString error){newRoom=error.isEmpty();});
        QTRY_VERIFY(newRoom);restored.refresh();
        QTRY_VERIFY(!restored.channels->findItems("Комната второго сервера",Qt::MatchExactly).isEmpty());
        restored.openView("audio");
        restored.serverSwitch->click();
        auto* picker=restored.findChild<QGraphicsView*>("serverMenu");QVERIFY(picker);
        QTest::qWait(180);
        QGraphicsProxyWidget* other=nullptr;
        for(auto* item:picker->scene()->items())
            if(auto* card=qgraphicsitem_cast<QGraphicsProxyWidget*>(item);card && card->widget()->accessibleName().endsWith(QUrl(url).authority()) && !card->widget()->property("currentServer").toBool())other=card;
        QVERIFY(other);
        const auto point=picker->mapFromScene(other->mapToScene(QPointF(93,23)));
        QCOMPARE(picker->itemAt(point),static_cast<QGraphicsItem*>(other));
        QTest::mouseClick(picker->viewport(),Qt::LeftButton,Qt::NoModifier,point);
        QTRY_VERIFY_WITH_TIMEOUT(restored.api.server==QUrl(url) || (restored.switchNotice->isVisible() && !restored.switchProbe),8000);
        QVERIFY2(restored.api.server==QUrl(url),qPrintable(restored.switchMessage->text()));
        QTRY_COMPARE_WITH_TIMEOUT(restored.api.server,QUrl(url),8000);QCOMPARE(restored.kind,QString("voice"));
        auto exitControl=[&]()->QPushButton*{
            for(auto* b:restored.channels->findChildren<QPushButton*>())
                if(b->isVisibleTo(restored.channels) && b->accessibleName().startsWith("Выйти из комнаты:"))return b;
            return nullptr;
        };
        QVERIFY(restored.channels->findItems("Комната второго сервера",Qt::MatchExactly).isEmpty());
        QTRY_VERIFY(exitControl());QVERIFY(!exitControl()->visibleRegion().isEmpty());
        QCOMPARE(restored.activeVoice,originalRoom);QVERIFY(restored.joinButton->isVisibleTo(&restored));
        choose(restored,"text");QTRY_VERIFY_WITH_TIMEOUT(!restored.lastMessages.isEmpty(),5000);QVERIFY(restored.inCall);
        restored.setMemberPanel(true,false);restored.memberToggle->click();QTRY_COMPARE(restored.memberPanel->height(),62);QVERIFY(restored.memberToggle->isVisible());QVERIFY(!restored.members->isVisible());
        restored.memberToggle->click();QTRY_COMPARE(restored.memberPanel->height(),restored.memberPanel->parentWidget()->height()-16);QVERIFY(restored.members->isVisible());
        restored.settingsButton->click();QCOMPARE(restored.view,QString("general"));restored.backButton->click();
        QSignalSpy deviceChanges(peer.media.get(),&VoiceEvents::audioDeviceSelected);
        peer.media->selectAudioDevice(true,"missing-test-device");
        QTRY_COMPARE(deviceChanges.count(),1);
        QVERIFY(!deviceChanges.last().at(2).toString().isEmpty());QVERIFY(peer.inCall);
        QCOMPARE(peer.inputDevice,deviceChanges.last().at(1).toByteArray());
        peer.media->selectAudioDevice(false,{});
        QTRY_COMPARE(deviceChanges.count(),2);
        QVERIFY(deviceChanges.last().at(2).toString().isEmpty());QVERIFY(peer.inCall);
        peer.media->localConnectionQualityChanged("poor");
        QCOMPARE(peer.networkDot->toolTip(),QString("Нестабильная связь в звонке"));
        QVERIFY(!peer.networkWarning->isHidden());
        peer.workspaceLatency=2010;peer.media->localConnectionQualityChanged({});
        QCOMPARE(peer.latencyBadge->text(),QString("2010 мс"));
        QVERIFY(peer.networkWarning->isHidden());
        peer.workspaceFailed=true;peer.updateNetworkIndicator();
        QCOMPARE(peer.networkDot->accessibleName(),QString("Не удалось обновить список каналов"));QVERIFY(peer.latencyBadge->isHidden());
        peer.workspaceFailed=false;peer.workspaceLatency=700;peer.updateNetworkIndicator();
        QCOMPARE(peer.networkDot->accessibleName(),QString("Соединение в порядке"));
        const auto identity=peer.callIdentity;QVERIFY(!identity.isEmpty());
        const auto voice=peer.activeVoice;
        const QJsonObject mine{{"identity",identity},{"name","QA-call"}};
        const QJsonObject otherDevice{{"identity",peer.selfId+".other-device"},{"name","QA-other"}};
        const QJsonObject staleRoom{{"people",QJsonArray{mine,otherDevice}}};
        auto stale=peer.workspace;stale["presence"]=QJsonObject{{voice,staleRoom}};peer.renderWorkspace(stale);
        auto* sideExit=exitControl();QVERIFY(sideExit);
        QTest::mouseClick(sideExit,Qt::LeftButton);peer.media->stop();QTRY_VERIFY_WITH_TIMEOUT(!restored.inCall&&!peer.inCall,8000);
        peer.renderWorkspace(stale);
        QCOMPARE(peer.workspace["presence"].toObject()[voice].toObject()["people"].toArray(),QJsonArray{otherDevice});
        QVERIFY(peer.channels->findItems("QA-call",Qt::MatchStartsWith).isEmpty());
        QCOMPARE(peer.channels->findItems("QA-other",Qt::MatchStartsWith).size(),1);
        peer.openView("general");
        QVERIFY(!peer.detailPage->findChild<QWidget*>("storageMeter"));QVERIFY(!peer.detailPage->findChild<QWidget*>("antivirusSettings"));
        auto* updatesPanel=peer.detailPage->findChild<QWidget*>("serverUpdates");QVERIFY(updatesPanel);
        for(auto* child:updatesPanel->findChildren<QPushButton*>())if(child->text().startsWith("Откатиться"))QVERIFY(child->isHidden());
        if(auto out=qEnvironmentVariable("NIMBO_SETTINGS_SHOTS");!out.isEmpty()){
            QTest::qWait(250);QVERIFY(peer.grab().save(out+"/member-general.png"));
        }
        stageBadLocalRelease(peer);
        for(auto* b:updatesPanel->findChildren<QPushButton*>())if(b->text()=="Обновить")b->click();
        QTRY_VERIFY(peer.updates->status().contains("Контрольная сумма"));
        QVERIFY(!QFile::exists(qEnvironmentVariable("NIMBO_QA_UPDATE_INBOX")+"/install"));
        peer.openView("room");
        memberSettings(restored,peer.selfId);


    }
};
int main(int argc,char** argv){
    QString platform=qEnvironmentVariable("QT_QPA_PLATFORM","windows");
    for(int i=1;i+1<argc;++i)
        if(qstrcmp(argv[i],"-platform")==0)platform=QString::fromLocal8Bit(argv[i+1]);
    const auto backend=platform.section(':',0,0);
    const auto plugin=QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteDir().filePath("platforms/q"+backend+".dll");
    if(!QFileInfo::exists(plugin)){
        std::fprintf(stderr,"Requested Qt platform is absent from the test deployment: %s\n",qPrintable(backend));
        return 2;
    }
    QApplication application(argc,argv);application.setApplicationVersion(NIMBO_DESKTOP_VERSION);
    const auto arguments=application.arguments();
    if(arguments.size()==3 && arguments[1]=="--write-test-microphone"){
        // Chromium's default fake microphone also emits a beep. Feed it the
        // same quiet phrase and rest used by the native synthetic participant.
        auto pcm=soundCheckPhrase(48000);
        for(auto& sample:pcm)sample/=3;
        pcm.resize(6*48000);
        QFile file(arguments[2]);
        if(!file.open(QIODevice::WriteOnly))return 2;
        const quint32 bytes=quint32(pcm.size()*sizeof(qint16));
        QDataStream wav(&file);wav.setByteOrder(QDataStream::LittleEndian);
        wav.writeRawData("RIFF",4);wav<<quint32(36+bytes);wav.writeRawData("WAVEfmt ",8);
        wav<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(48000)<<quint32(96000)<<quint16(2)<<quint16(16);
        wav.writeRawData("data",4);wav<<bytes;
        wav.writeRawData(reinterpret_cast<const char*>(pcm.data()),bytes);
        return wav.status()==QDataStream::Ok && file.flush()?0:2;
    }
    if(arguments.size()==4 && arguments[1]=="--measure-output")return measureProcessOutput(arguments[2].toULong(),arguments[3]);
    if(arguments.size()==3 && arguments[1]=="--probe-fonts")return NativeDesktopTests::fontSamples(arguments[2])?0:1;
    if(arguments.size()==3 && arguments[1]=="--capture-target"){
        QWidget pattern;
        pattern.setWindowTitle(arguments[2]);
        pattern.resize(320,200);
        pattern.setAutoFillBackground(true);
        QPalette colors;
        colors.setColor(QPalette::Window,QColor("#1cd844"));
        pattern.setPalette(colors);
        pattern.show();
        if(!QTest::qWaitForWindowExposed(&pattern,4000))return 2;
        pattern.repaint();
        QTimer::singleShot(100,&pattern,[]{std::puts("painted");std::fflush(stdout);});
        return application.exec();
    }
    NativeDesktopTests suite;
    return QTest::qExec(&suite,argc,argv);
}
#include "native_desktop_tests.moc"
