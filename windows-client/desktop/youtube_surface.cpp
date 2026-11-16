#include "interface_text.h"
#include "youtube_surface.h"
#include <QtWidgets>
#include <wrl.h>
#include <WebView2.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

struct YoutubeSurface::Browser {
    ComPtr<ICoreWebView2Controller> controller;
    ~Browser(){ if(controller)controller->Close(); }
};

YoutubeSurface::~YoutubeSurface(){
    for(auto widget:ancestors)if(widget)widget->removeEventFilter(this);
    disconnect(scrollConnection);
    if(fullscreen && host && host->parentWidget())host->parentWidget()->setWindowState(savedState);
    browser.reset();
    delete host.data();
}

quint32 YoutubeSurface::browserProcessId() const{
    UINT32 pid=0;
    ComPtr<ICoreWebView2> web;
    if(browser->controller && SUCCEEDED(browser->controller->get_CoreWebView2(&web)))web->get_BrowserProcessId(&pid);
    return pid;
}

void YoutubeSurface::fit(){
    if(!host || !clipViewport || !host->parentWidget())return;
    auto* root=host->parentWidget();
    if(window()!=root || clipViewport->window()!=root){host->hide();return;}
    const QRect full=fullscreen?root->rect():QRect(mapTo(root,QPoint()),size());
    const QRect clip(clipViewport->mapTo(root,QPoint()),clipViewport->size());
    const QRect visible=fullscreen?full:full.intersected(clip).intersected(root->rect());
    if(!visible.isEmpty() && host->geometry()!=visible){
        host->setGeometry(visible);
        if(browser->controller)browser->controller->NotifyParentWindowPositionChanged();
    }
    const bool display=isVisible() && !visible.isEmpty();
    if(browser->controller){
        const double scale=host->devicePixelRatioF();
        const QPoint offset=visible.isEmpty()?QPoint():full.topLeft()-visible.topLeft();
        RECT bounds{qRound(offset.x()*scale),qRound(offset.y()*scale),
                    qRound((offset.x()+full.width())*scale),qRound((offset.y()+full.height())*scale)};
        RECT previous{};
        if(FAILED(browser->controller->get_Bounds(&previous)) || !EqualRect(&previous,&bounds))
            browser->controller->put_Bounds(bounds);
    }
    if(display==host->isHidden())host->setVisible(display);
}

void YoutubeSurface::setFullscreen(bool enabled){
    if(fullscreen==enabled || !host || !host->parentWidget())return;
    auto* root=host->parentWidget();
    fullscreen=enabled;
    if(enabled){
        savedState=root->windowState();
        root->showFullScreen();
        host->raise();
        if(browser->controller)browser->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }else root->setWindowState(savedState);
    fit();
}

void YoutubeSurface::exitFullscreen(){
    if(!fullscreen)return;
    ComPtr<ICoreWebView2> web;
    if(browser->controller && SUCCEEDED(browser->controller->get_CoreWebView2(&web)))
        web->ExecuteScript(L"if(document.fullscreenElement)document.exitFullscreen()",nullptr);
    setFullscreen(false);
}

void YoutubeSurface::trackCard(){
    for(auto widget:ancestors)if(widget)widget->removeEventFilter(this);
    disconnect(scrollConnection);ancestors.clear();
    QWidget* viewport=parentWidget();
    for(auto* w=parentWidget();w;w=w->parentWidget()){
        if(auto* area=qobject_cast<QAbstractScrollArea*>(w);area && area->viewport()->isAncestorOf(this)){
            viewport=area->viewport();
            scrollConnection=connect(area->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{fit();});
            break;
        }
    }
    clipViewport=viewport;
    // QListView scrolls its viewport children itself. Keep the HWND outside
    // that subtree because fit() already applies the scroll offset.
    auto* root=viewport->window();
    if(!host){
        host=new QWidget(root);host->setObjectName("youtubeViewportHost");
        host->setAttribute(Qt::WA_DontCreateNativeAncestors);host->setAttribute(Qt::WA_NativeWindow);
    }else if(host->parentWidget()!=root)host->setParent(root);
    for(auto* w=parentWidget();w;w=w->parentWidget()){
        ancestors.append(w);w->installEventFilter(this);if(w==root)break;
    }
    fit();
}

bool YoutubeSurface::eventFilter(QObject* obj,QEvent* event){
    if(fullscreen && event->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){
        exitFullscreen();return true;
    }
    switch(event->type()){
        case QEvent::Move:case QEvent::Resize:case QEvent::Show:case QEvent::Hide:case QEvent::LayoutRequest:fit();break;
        default:break;
    }
    return QWidget::eventFilter(obj,event);
}

void YoutubeSurface::resizeEvent(QResizeEvent* e){QWidget::resizeEvent(e);fit();}

YoutubeSurface::YoutubeSurface(QWidget* parent,const QString& videoId,int startMs,const QString& playerFolder)
    :QWidget(parent),browser(std::make_unique<Browser>()){
    hide();
    setObjectName("youtubeSurface");
    trackCard();
    const auto handle=reinterpret_cast<HWND>(host->winId());
    QPointer<YoutubeSurface> guard(this);
    const auto finish=[guard](int seconds,QString error,QImage still={}){
        if(guard)QTimer::singleShot(0,guard,[guard,seconds,error,still=std::move(still)]{
            if(guard){auto done=guard->finished;if(done)done(seconds,error,still);}
        });
    };
    const auto fail=[finish]{finish(0,QString::fromUtf8(UiText::CouldNotOpenTheYoutubePlayerOpen()));};
    auto* deadline=new QTimer(this);deadline->setSingleShot(true);
    connect(deadline,&QTimer::timeout,this,fail);deadline->start(30000);
    const QString cache=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/youtube";
    const QString folder=QDir::toNativeSeparators(playerFolder.isEmpty()?QCoreApplication::applicationDirPath()+"/assets/player":playerFolder);
    const QString url="https://nimbo-player.example/video.html?id="+videoId+"&start="+QString::number(startMs)
        +"&hl="+UiText::locale().bcp47Name()+"&title="+QString::fromLatin1(QUrl::toPercentEncoding(UiText::YoutubePlayerTitle()));
    const HRESULT result=CreateCoreWebView2EnvironmentWithOptions(nullptr,reinterpret_cast<LPCWSTR>(cache.utf16()),nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [guard,handle,folder,url,fail,finish,deadline](HRESULT hr,ICoreWebView2Environment* env)->HRESULT{
            if(!guard)return S_OK;
            if(FAILED(hr)||!env){fail();return S_OK;}
            const HRESULT pending=env->CreateCoreWebView2Controller(handle,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [guard,folder,url,fail,finish,deadline](HRESULT hr,ICoreWebView2Controller* control)->HRESULT{
                    if(!guard){if(control)control->Close();return S_OK;}
                    if(FAILED(hr)||!control){fail();return S_OK;}
                    guard->browser->controller=control;
                    ComPtr<ICoreWebView2> web;
                    ComPtr<ICoreWebView2_3> mapping;
                    if(FAILED(control->get_CoreWebView2(&web))||FAILED(web.As(&mapping))||
                       FAILED(mapping->SetVirtualHostNameToFolderMapping(L"nimbo-player.example",
                       reinterpret_cast<LPCWSTR>(folder.utf16()),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY))){fail();return S_OK;}
                    EventRegistrationToken token{};
                    control->add_AcceleratorKeyPressed(Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                        [guard](ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs* args)->HRESULT{
                            UINT key=0;COREWEBVIEW2_KEY_EVENT_KIND kind{};
                            args->get_VirtualKey(&key);args->get_KeyEventKind(&kind);
                            if(guard && guard->fullscreen && key==VK_ESCAPE && kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN){
                                args->put_Handled(TRUE);
                                QTimer::singleShot(0,guard,[guard]{if(guard)guard->exitFullscreen();});
                            }
                            return S_OK;
                        }).Get(),&token);
                    web->add_ContainsFullScreenElementChanged(Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
                        [guard](ICoreWebView2* sender,IUnknown*)->HRESULT{
                            BOOL active=FALSE;
                            if(guard && SUCCEEDED(sender->get_ContainsFullScreenElement(&active)))guard->setFullscreen(active);
                            return S_OK;
                        }).Get(),&token);
                    // A windowed iframe receives the wheel before Qt. Leave YouTube's
                    // own menus, zoom and fullscreen alone; otherwise scroll the chat.
                    web->AddScriptToExecuteOnDocumentCreated(LR"js(
                        if (location.origin === 'https://www.youtube-nocookie.com' && window !== parent) {
                          window.addEventListener('wheel', e => {
                            if (e.ctrlKey || e.shiftKey || document.fullscreenElement || !e.deltaY ||
                                e.composedPath().some(n => n instanceof Element && (n.matches('[role=menu], [role=listbox], input, .ytp-popup') ||
                                  (n.scrollHeight > n.clientHeight && /scroll|auto/.test(getComputedStyle(n).overflowY))))) return;
                            e.preventDefault();
                            parent.postMessage({nimboScroll:e.deltaY*(e.deltaMode===1?16:e.deltaMode===2?innerHeight:1)},'https://nimbo-player.example');
                          },{passive:false});
                        })js",nullptr);
                    web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                        [](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* a){return a->put_Handled(TRUE);}).Get(),&token);
                    web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
                        [](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* a)->HRESULT{
                            COREWEBVIEW2_PERMISSION_KIND kind{};a->get_PermissionKind(&kind);
                            LPWSTR raw=nullptr;a->get_Uri(&raw);const QUrl origin(QString::fromWCharArray(raw));CoTaskMemFree(raw);
                            const bool allowed=kind==COREWEBVIEW2_PERMISSION_KIND_AUTOPLAY && origin.scheme()=="https" &&
                                (origin.host()=="nimbo-player.example" || origin.host()=="www.youtube-nocookie.com");
                            ComPtr<ICoreWebView2PermissionRequestedEventArgs3> scoped;
                            if(SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&scoped))))scoped->put_SavesInProfile(FALSE);
                            return a->put_State(allowed?COREWEBVIEW2_PERMISSION_STATE_ALLOW:COREWEBVIEW2_PERMISSION_STATE_DENY);
                        }).Get(),&token);
                    if(qEnvironmentVariableIsSet("NIMBO_QA_SILENT_OUTPUT")){
                        ComPtr<ICoreWebView2_8> audio;if(SUCCEEDED(web.As(&audio)))audio->put_IsMuted(TRUE);
                    }
                    web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                        [url](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
                            LPWSTR uri=nullptr;args->get_Uri(&uri);
                            const bool allowed=uri && QString::fromWCharArray(uri)==url;CoTaskMemFree(uri);
                            return args->put_Cancel(!allowed);
                        }).Get(),&token);
                    web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                        [guard,url,finish,fail,deadline](ICoreWebView2* sender,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT{
                            LPWSTR src=nullptr;args->get_Source(&src);
                            const bool local=src && QString::fromWCharArray(src)==url;CoTaskMemFree(src);
                            LPWSTR value=nullptr;
                            if(!local||FAILED(args->TryGetWebMessageAsString(&value)))return S_OK;
                            const QString msg=QString::fromWCharArray(value);CoTaskMemFree(value);
                            if(qEnvironmentVariableIsSet("NIMBO_QA_ONLINE_YOUTUBE"))qInfo()<<"YouTube state"<<msg;
                            // The user's Play click happened in Qt before this browser existed.
                            // Forward that one activation when the local embed is ready.
                            if(msg=="ready"){
                                if(!guard)return S_OK;
                                guard->show();guard->fit();
                                if(guard->host)guard->host->raise();
                                const auto fired=sender->CallDevToolsProtocolMethod(L"Runtime.evaluate",
                                    LR"json({"expression":"window.startRequestedVideo()","userGesture":true})json",
                                    Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                                        [fail](HRESULT hr,LPCWSTR){if(FAILED(hr))fail();return S_OK;}).Get());
                                if(FAILED(fired))fail();
                                return S_OK;
                            }
                            if(!guard)return S_OK;
                            if(msg.startsWith("wheel:") && guard->host && guard->host->isVisible()){
                                if(auto* area=guard->clipViewport?qobject_cast<QAbstractScrollArea*>(guard->clipViewport->parentWidget()):nullptr){
                                    auto* bar=area->verticalScrollBar();bar->setValue(bar->value()+qBound(-800,msg.mid(6).toInt(),800));
                                }
                                return S_OK;
                            }
                            if(msg=="playing" || msg=="blocked"){
                                deadline->stop();
                                guard->show();guard->fit();
                                guard->setAccessibleName(msg=="playing"?UiText::YoutubeVideoIsPlaying():UiText::PlayYoutubeVideo());
                                if(guard->started)guard->started();
                                return S_OK;
                            }
                            if(msg.startsWith("pause:")){
                                bool ok=false;const int time=msg.mid(6).toInt(&ok);if(!ok || time<0)return S_OK;
                                ComPtr<IStream> stream;
                                if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))){finish(time,{});return S_OK;}
                                const auto captured=sender->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,stream.Get(),
                                    Callback<ICoreWebView2CapturePreviewCompletedHandler>(
                                    [guard,stream,finish,time](HRESULT hr)->HRESULT{
                                        if(!guard)return S_OK;
                                        STATSTG stat{};QImage frame;
                                        if(SUCCEEDED(hr) && SUCCEEDED(stream->Stat(&stat,STATFLAG_NONAME)) && stat.cbSize.QuadPart<=8*1024*1024){
                                            QByteArray bytes(int(stat.cbSize.QuadPart),Qt::Uninitialized);ULONG read=0;
                                            if(SUCCEEDED(stream->Seek({},STREAM_SEEK_SET,nullptr)) && SUCCEEDED(stream->Read(bytes.data(),ULONG(bytes.size()),&read)))
                                                frame=QImage::fromData(reinterpret_cast<const uchar*>(bytes.constData()),int(read),"PNG");
                                        }
                                        finish(time,{},frame);return S_OK;
                                    }).Get());
                                if(FAILED(captured))finish(time,{});
                            }else if(msg=="ended")finish(0,{});else if(msg=="error"||msg.startsWith("error:"))fail();
                            return S_OK;
                        }).Get(),&token);
                    guard->fit();control->put_IsVisible(TRUE);
                    if(FAILED(web->Navigate(reinterpret_cast<LPCWSTR>(url.utf16()))))fail();
                    return S_OK;
                }).Get());
            if(FAILED(pending))fail();
            return S_OK;
        }).Get());
    if(FAILED(result))fail();
}
