#include "interface_text.h"
#include "share_picker.h"
#include "ui.h"
#include <QtWidgets>
#include <QWindowCapture>
#include <private/qcapturablewindow_p.h>
#include <QtGui/qscreen_platform.h>
#include <windows.h>
#include <dwmapi.h>

namespace {
struct Source {
    QString title,program;
    QPointer<QScreen> monitor;
    HWND handle=nullptr;
    QPixmap image;
};

QString programPath(HWND window){
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid==GetCurrentProcessId())return {};
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return {};
    wchar_t path[32768];DWORD length=std::size(path);
    const bool ok=QueryFullProcessImageNameW(process,0,path,&length);
    CloseHandle(process);
    return ok?QString::fromWCharArray(path,int(length)):QString();
}

QPixmap monitorPreview(QScreen* screen){
    auto* native=screen->nativeInterface<QNativeInterface::QWindowsScreen>();
    MONITORINFO info{sizeof(MONITORINFO)};
    if(!native || !GetMonitorInfoW(native->handle(),&info))return {};
    const RECT r=info.rcMonitor;
    const QSize physical(r.right-r.left,r.bottom-r.top);
    const QSize target=physical.scaled(320,180,Qt::KeepAspectRatio);
    HDC desktop=GetDC(nullptr);
    if(!desktop)return {};
    HDC output=CreateCompatibleDC(desktop);
    HBITMAP bitmap=CreateCompatibleBitmap(desktop,target.width(),target.height());
    QPixmap result;
    if(output && bitmap){
        auto previous=SelectObject(output,bitmap);
        SetStretchBltMode(output,HALFTONE);
        SetBrushOrgEx(output,0,0,nullptr);
        const bool copied=StretchBlt(output,0,0,target.width(),target.height(),desktop,
                                     r.left,r.top,physical.width(),physical.height(),SRCCOPY|CAPTUREBLT);
        SelectObject(output,previous);
        if(copied)result=QPixmap::fromImage(QImage::fromHBITMAP(bitmap));
    }
    if(bitmap)DeleteObject(bitmap);
    if(output)DeleteDC(output);
    ReleaseDC(nullptr,desktop);
    return result;
}

BOOL CALLBACK collectProgram(HWND h,LPARAM data){
    const int length=GetWindowTextLengthW(h);
    if(!IsWindowVisible(h) || GetAncestor(h,GA_ROOT)!=h || length<=0)return TRUE;
    const auto style=GetWindowLongPtrW(h,GWL_EXSTYLE);
    if((style&WS_EX_TOOLWINDOW) || (GetWindow(h,GW_OWNER) && !(style&WS_EX_APPWINDOW)))return TRUE;
    DWORD cloaked=0;DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
    wchar_t classBuffer[256]={};GetClassNameW(h,classBuffer,256);
    const QString cls=QString::fromWCharArray(classBuffer);
    if(cloaked || cls=="Progman" || cls=="WorkerW" || cls=="Shell_TrayWnd" || cls=="Shell_SecondaryTrayWnd")return TRUE;
    const auto path=programPath(h);const auto exe=QFileInfo(path).fileName().toLower();
    if(path.isEmpty() || exe=="textinputhost.exe" || exe=="shellexperiencehost.exe" || exe=="startmenuexperiencehost.exe")return TRUE;
    std::wstring buffer(size_t(length)+1,L'\0');
    const int count=GetWindowTextW(h,buffer.data(),int(buffer.size()));
    const auto title=QString::fromWCharArray(buffer.data(),count).trimmed();
    if(!title.isEmpty() && !title.startsWith("Window 0x",Qt::CaseInsensitive))
        reinterpret_cast<QList<Source>*>(data)->append({title,path,nullptr,h,{}});
    return TRUE;
}

QList<Source> programs(){
    QList<Source> result;
    EnumWindows(collectProgram,reinterpret_cast<LPARAM>(&result));
    return result;
}

class Picker final : public QDialog {
public:
    explicit Picker(QWidget* parent):QDialog(parent,Qt::Dialog|Qt::FramelessWindowHint){
        setObjectName("sharePicker");setWindowTitle(UiText::ScreenSharing());setWindowModality(Qt::WindowModal);
        setStyleSheet("QDialog#sharePicker{background:#171717} QScrollArea,QWidget#grid{background:transparent;border:0}"
                      "QPushButton{background:#242424;border:0;border-radius:10px;padding:8px 14px;font-size:12px}"
                      "QPushButton:hover,QPushButton:focus{background:#303030} QPushButton:checked{background:#353535}"
                      "QPushButton#source{padding:0;border:2px solid transparent} QPushButton#source:checked{border-color:#dcdcdc}"
                      "QPushButton#confirm{background:#e4e4e4;color:#111} QPushButton#confirm:disabled{background:#383838;color:#999}");
        resize(qMin(700,parent->width()-32),qMin(500,parent->height()-32));
        auto* body=new QVBoxLayout(this);body->setContentsMargins(32,28,32,24);body->setSpacing(16);
        body->addWidget(Ui::text(UiText::ScreenSharing(),"font-size:24px;font-weight:600"));
        auto* tabs=new QHBoxLayout;auto* group=new QButtonGroup(this);
        for(const QString tab:{UiText::Monitors(),UiText::Applications()}){
            auto* b=new QPushButton(tab);b->setCheckable(true);b->setChecked(tab==UiText::Monitors());b->setAutoDefault(false);
            group->addButton(b);tabs->addWidget(b);
            connect(b,&QPushButton::clicked,this,[this,tab]{populate(tab==UiText::Monitors());});
        }
        tabs->addStretch();body->addLayout(tabs);
        scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);body->addWidget(scroll,1);
        auto* actions=new QHBoxLayout;actions->addStretch();auto* cancel=new QPushButton(UiText::Cancel());cancel->setAutoDefault(false);
        confirm=new QPushButton(UiText::Share());confirm->setObjectName("confirm");
        actions->addWidget(cancel);actions->addWidget(confirm);body->addLayout(actions);
        connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
        connect(confirm,&QPushButton::clicked,this,[this]{
            if(current<0)return;
            auto& src=sources[current];
            if(src.monitor){selection=ShareSource{src.monitor->name(),{}};accept();}
            else if(src.handle){
                if(IsWindow(src.handle) && programPath(src.handle)==src.program){
                    // Load Qt's capture backend only when the user starts sharing.
                    for(const auto& window:QWindowCapture::capturableWindows()){
                        const auto* native=QCapturableWindowPrivate::handle(window);
                        if(native && reinterpret_cast<HWND>(native->id)==src.handle && window.isValid()){
                            selection=ShareSource{{},window};accept();return;
                        }
                    }
                }
                populate(false);
            }else populate(true);
        });
        connect(scroll->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{positionPreviews();});
        // Take monitor snapshots before the modal or its dimmed backdrop is visible.
        for(auto* screen:QGuiApplication::screens()){
            const QString name=QString(UiText::Monitor12()).arg(monitors.size()+1).arg(screen->name());
            monitors.append({name,{},screen,nullptr,monitorPreview(screen)});
        }
        populate(true);
    }
    ~Picker(){clearPreviews();}
    std::optional<ShareSource> selection;
protected:
    void resizeEvent(QResizeEvent* event) override{
        QDialog::resizeEvent(event);
        QPainterPath corners;corners.addRoundedRect(rect(),20,20);setMask(QRegion(corners.toFillPolygon().toPolygon()));
        QTimer::singleShot(0,this,[this]{positionPreviews();});
    }
private:
    struct Thumbnail{QLabel* area;int source;HTHUMBNAIL handle=nullptr;SIZE original{};bool unavailable=false;};
    void clearPreviews(){for(auto& t:thumbnails)if(t.handle)DwmUnregisterThumbnail(t.handle);thumbnails.clear();}
    void positionPreviews(){
        const QRect viewport(scroll->viewport()->mapTo(this,QPoint()),scroll->viewport()->size());
        for(auto& t:thumbnails){
            const QRect area(t.area->mapTo(this,QPoint()),t.area->size());
            if(!area.intersects(viewport)){
                if(t.handle){DwmUnregisterThumbnail(t.handle);t.handle=nullptr;}
                continue;
            }
            if(t.unavailable)continue;
            if(!t.handle){
                const auto& src=sources[t.source];
                if(FAILED(DwmRegisterThumbnail(reinterpret_cast<HWND>(winId()),src.handle,&t.handle)) ||
                   FAILED(DwmQueryThumbnailSourceSize(t.handle,&t.original)) || t.original.cx<=0 || t.original.cy<=0){
                    if(t.handle){DwmUnregisterThumbnail(t.handle);t.handle=nullptr;}
                    t.unavailable=true;
                    t.area->setPixmap(QFileIconProvider().icon(QFileInfo(src.program)).pixmap(32,32));
                    continue;
                }
            }
            QSize fitted=QSize(t.original.cx,t.original.cy).scaled(area.size(),Qt::KeepAspectRatio);
            QRect target(QPoint(),fitted);target.moveCenter(area.center());
            const QRect shown=target.intersected(viewport);
            DWM_THUMBNAIL_PROPERTIES prop={};prop.dwFlags=DWM_TNP_VISIBLE;prop.fVisible=!shown.isEmpty();
            if(prop.fVisible){
                const qreal scale=devicePixelRatioF();
                prop.dwFlags|=DWM_TNP_RECTDESTINATION|DWM_TNP_RECTSOURCE;
                prop.rcDestination={qRound(shown.x()*scale),qRound(shown.y()*scale),qRound((shown.right()+1)*scale),qRound((shown.bottom()+1)*scale)};
                const qreal sx=qreal(t.original.cx)/target.width(),sy=qreal(t.original.cy)/target.height();
                prop.rcSource={qRound((shown.x()-target.x())*sx),qRound((shown.y()-target.y())*sy),
                               qRound((shown.right()+1-target.x())*sx),qRound((shown.bottom()+1-target.y())*sy)};
            }
            DwmUpdateThumbnailProperties(t.handle,&prop);
        }
    }
    void populate(bool screens){
        clearPreviews();current=-1;confirm->setEnabled(false);
        sources=screens?monitors:programs();
        auto* gridHost=new QWidget;gridHost->setObjectName("grid");auto* grid=new QGridLayout(gridHost);
        grid->setSizeConstraint(QLayout::SetMinimumSize);grid->setContentsMargins(0,0,4,0);grid->setSpacing(8);grid->setAlignment(Qt::AlignTop);
        auto* choices=new QButtonGroup(gridHost);
        if(sources.isEmpty())grid->addWidget(Ui::text(UiText::NoSourcesAvailable(),"color:#aaa"),0,0);
        for(int i=0;i<sources.size();++i){
            const auto& src=sources[i];auto* card=new QPushButton;card->setAutoDefault(false);card->setObjectName("source");
            card->setCheckable(true);card->setAccessibleName(src.title);card->setToolTip(src.title);choices->addButton(card);
            auto* inner=new QVBoxLayout(card);inner->setSpacing(5);inner->setContentsMargins(8,8,8,8);
            auto* area=Ui::text("","background:#101010;border-radius:6px");area->setFixedHeight(94);area->setAlignment(Qt::AlignCenter);
            area->setAttribute(Qt::WA_TransparentForMouseEvents);inner->addWidget(area);
            auto* description=Ui::text("","font-size:12px");description->setAttribute(Qt::WA_TransparentForMouseEvents);
            description->setText(description->fontMetrics().elidedText(src.title,Qt::ElideRight,(width()-104)/3));inner->addWidget(description);
            card->ensurePolished();card->setFixedHeight(inner->sizeHint().height());
            card->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);grid->addWidget(card,i/3,i%3);
            if(!src.handle)area->setPixmap(src.image.scaled((width()-104)/3,94,Qt::KeepAspectRatio,Qt::SmoothTransformation));
            else thumbnails.append({area,i});
            connect(card,&QPushButton::clicked,this,[this,i]{current=i;confirm->setEnabled(true);});
        }
        for(int i=0;i<3;++i)grid->setColumnStretch(i,1);
        scroll->setWidget(gridHost);
        QTimer::singleShot(0,this,[this]{positionPreviews();});
    }
    QList<Source> monitors,sources;QList<Thumbnail> thumbnails;
    int current=-1;QScrollArea* scroll=nullptr;QPushButton* confirm=nullptr;
};
}

std::optional<ShareSource> chooseShareSource(QWidget* parent){
    Picker picker(parent);
    QWidget backdrop(parent);backdrop.setGeometry(parent->rect());backdrop.setStyleSheet("background:rgba(0,0,0,185)");
    backdrop.show();backdrop.raise();
    picker.move(parent->mapToGlobal(parent->rect().center()-picker.rect().center()));
    picker.exec();return picker.selection;
}
