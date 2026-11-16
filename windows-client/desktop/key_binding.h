#pragma once
#include "interface_text.h"
#include <QtWidgets>
#include <windows.h>

namespace KeyBinding {
inline const QStringList actions={"mic","camera","screen","members","effects","settings"};
inline QString actionName(int index){
    constexpr UiText::Id names[]={UiText::Id::Microphone,UiText::Id::Camera,UiText::Id::ScreenSharing,UiText::Id::MembersPanel,UiText::Id::BackgroundBlur,UiText::Id::SoundSettings};
    return QString::fromUtf8(UiText::value(names[index]));
}
inline bool modifier(const QString& code){
    return code.startsWith("Control")||code.startsWith("Shift")||code.startsWith("Alt")||code.startsWith("Meta");
}
inline QJsonObject binding(QString code,Qt::KeyboardModifiers keys){
    if(code.isEmpty())return {};
    return {{"code",code},{"ctrl",keys.testFlag(Qt::ControlModifier)&&!code.startsWith("Control")},
        {"shift",keys.testFlag(Qt::ShiftModifier)&&!code.startsWith("Shift")},
        {"alt",keys.testFlag(Qt::AltModifier)&&!code.startsWith("Alt")},
        {"meta",keys.testFlag(Qt::MetaModifier)&&!code.startsWith("Meta")}};
}
inline QJsonObject defaults(){
    QJsonObject data;
    const QString letters="MVSPBO";
    for(int i=0;i<actions.size();++i)data[actions[i]]=binding("Key"+letters.mid(i,1),Qt::ControlModifier|Qt::AltModifier);
    return data;
}
inline QString keyboard(const QKeyEvent& e){
    const auto scan=e.nativeScanCode();
    const int base=scan&255;
    const bool ext=(scan&0xff00)!=0;
    const QString top="QWERTYUIOP",home="ASDFGHJKL",bottom="ZXCVBNM";
    if(base>=0x10&&base<=0x19)return "Key"+top.mid(base-0x10,1);
    if(base>=0x1e&&base<=0x26)return "Key"+home.mid(base-0x1e,1);
    if(base>=0x2c&&base<=0x32)return "Key"+bottom.mid(base-0x2c,1);
    if(base>=2&&base<=11)return "Digit"+QString::number((base-1)%10);
    if(e.key()==Qt::Key_Shift)return base==0x36?"ShiftRight":"ShiftLeft";
    if(e.key()==Qt::Key_Control)return ext?"ControlRight":"ControlLeft";
    if(e.key()==Qt::Key_Alt)return ext?"AltRight":"AltLeft";
    if(e.key()==Qt::Key_Meta)return base==0x5c?"MetaRight":"MetaLeft";
    if(e.key()==Qt::Key_AltGr||e.modifiers().testFlag(Qt::GroupSwitchModifier))return {};
    if(e.key()>=Qt::Key_F1&&e.key()<=Qt::Key_F24)return "F"+QString::number(1+e.key()-Qt::Key_F1);
    if(e.key()>=Qt::Key_0&&e.key()<=Qt::Key_9)return QString(e.modifiers().testFlag(Qt::KeypadModifier)?"Numpad":"Digit")+QChar(e.key());
    if(e.key()>=Qt::Key_A&&e.key()<=Qt::Key_Z)return "Key"+QString(QChar(e.key()));
    static const QHash<int,QString> physical={{12,"Minus"},{13,"Equal"},{26,"BracketLeft"},{27,"BracketRight"},
        {39,"Semicolon"},{40,"Quote"},{41,"Backquote"},{43,"Backslash"},{51,"Comma"},{52,"Period"},{86,"IntlBackslash"}};
    if(physical.contains(base))return physical[base];
    if(base==0x35)return ext?"NumpadDivide":"Slash";
    static const QHash<int,QString> keys={{Qt::Key_Space,"Space"},{Qt::Key_Backspace,"Backspace"},{Qt::Key_Delete,"Delete"},
        {Qt::Key_Insert,"Insert"},{Qt::Key_Home,"Home"},{Qt::Key_End,"End"},{Qt::Key_PageUp,"PageUp"},
        {Qt::Key_PageDown,"PageDown"},{Qt::Key_Left,"ArrowLeft"},{Qt::Key_Right,"ArrowRight"},{Qt::Key_Up,"ArrowUp"},
        {Qt::Key_Down,"ArrowDown"},{Qt::Key_Return,"Enter"},{Qt::Key_Enter,"NumpadEnter"},{Qt::Key_Tab,"Tab"},
        {Qt::Key_Backtab,"Tab"},{Qt::Key_Escape,"Escape"},{Qt::Key_CapsLock,"CapsLock"},{Qt::Key_NumLock,"NumLock"},
        {Qt::Key_ScrollLock,"ScrollLock"},{Qt::Key_Pause,"Pause"},{Qt::Key_Print,"PrintScreen"},{Qt::Key_Menu,"ContextMenu"},
        {Qt::Key_VolumeUp,"AudioVolumeUp"},{Qt::Key_VolumeDown,"AudioVolumeDown"},{Qt::Key_VolumeMute,"AudioVolumeMute"},
        {Qt::Key_MediaPlay,"MediaPlayPause"},{Qt::Key_MediaTogglePlayPause,"MediaPlayPause"},{Qt::Key_MediaNext,"MediaTrackNext"},
        {Qt::Key_MediaPrevious,"MediaTrackPrevious"},{Qt::Key_Asterisk,"NumpadMultiply"},{Qt::Key_Plus,"NumpadAdd"},
        {Qt::Key_Minus,"NumpadSubtract"},{Qt::Key_Period,"NumpadDecimal"}};
    return keys.value(e.key());
}
inline QString mouse(Qt::MouseButton button){return button==Qt::BackButton?"Mouse4":button==Qt::ForwardButton?"Mouse5":QString();}
inline QString label(QJsonObject b){
    if(b.isEmpty())return UiText::Unassigned();
    QString code=b["code"].toString();
    const QMap<QString,QString> keys={{"Slash","/"},{"Backslash","\\"},{"Semicolon",";"},{"Quote","'"},{"Backquote",QString(QChar(96))},
        {"BracketLeft","["},{"BracketRight","]"},{"Minus","−"},{"Equal","="},{"Comma",","},{"Period","."},{"Space",UiText::Space()},
        {"ArrowUp","↑"},{"ArrowDown","↓"},{"ArrowLeft","←"},{"ArrowRight","→"},
        {"ControlLeft",UiText::LeftCtrl()},{"ControlRight",UiText::RightCtrl()},{"AltLeft",UiText::LeftAlt()},{"AltRight",UiText::RightAlt()},
        {"ShiftLeft",UiText::LeftShift()},{"ShiftRight",UiText::RightShift()},{"MetaLeft",UiText::Left()},{"MetaRight",UiText::Right()}};
    if(keys.contains(code))code=keys[code];
    else code.replace(QRegularExpression("^(Key|Digit)"),"").replace(QRegularExpression("^Mouse"),UiText::Mouse()).replace(QRegularExpression("^Numpad"),"Num ");
    QStringList parts;
    if(b["ctrl"].toBool())parts<<"Ctrl";if(b["alt"].toBool())parts<<"Alt";
    if(b["shift"].toBool())parts<<"Shift";if(b["meta"].toBool())parts<<"⌘";
    parts<<code;return parts.join(" + ");
}
inline bool editing(QWidget* w){
    for(;w;w=w->parentWidget())
        if(qobject_cast<QLineEdit*>(w)||qobject_cast<QPlainTextEdit*>(w)||qobject_cast<QTextEdit*>(w)||
           qobject_cast<QComboBox*>(w)||qobject_cast<QAbstractSpinBox*>(w)||w->property("captureButton").toBool())return true;
    return false;
}
}
