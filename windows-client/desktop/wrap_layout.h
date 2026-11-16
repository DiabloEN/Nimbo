#pragma once
#include <QLayout>
#include <QWidget>

class WrapLayout final : public QLayout {
public:
    explicit WrapLayout(QWidget* parent=nullptr):QLayout(parent){setContentsMargins(0,0,0,0);}
    ~WrapLayout() override{while(auto* item=takeAt(0))delete item;}
    void setStacked(bool value){if(stacked==value)return;stacked=value;invalidate();}
    void addItem(QLayoutItem* item) override{items.append(item);invalidate();}
    int count() const override{return items.size();}
    QLayoutItem* itemAt(int i) const override{return items.value(i);}
    QLayoutItem* takeAt(int i) override{return i>=0&&i<count()?items.takeAt(i):nullptr;}
    bool hasHeightForWidth() const override{return true;}
    int heightForWidth(int width) const override{return arrange({0,0,width,0},false);}
    void setGeometry(const QRect& rect) override{QLayout::setGeometry(rect);arrange(rect,true);}
    QSize sizeHint() const override{
        int width=0;
        for(auto* item:items)width=stacked?qMax(width,item->sizeHint().width()):width+item->sizeHint().width()+spacing();
        auto margins=contentsMargins();
        width+=margins.left()+margins.right();
        return {width,heightForWidth(width)};
    }
    QSize minimumSize() const override{
        int width=0,height=0;
        for(auto* item:items){width=qMax(width,item->minimumSize().width());height=qMax(height,item->minimumSize().height());}
        auto m=contentsMargins();return {width+m.left()+m.right(),height+m.top()+m.bottom()};
    }
private:
    int arrange(QRect rect,bool apply) const{
        auto margins=contentsMargins();auto inner=rect.marginsRemoved(margins);
        int x=0,y=0,line=0;
        for(auto* item:items){
            if(item->isEmpty())continue;
            auto size=item->sizeHint();
            size.setWidth(stacked?inner.width():qMin(inner.width(),size.width()));
            if(x>0&&(stacked||x+size.width()>inner.width())){y+=line+spacing();line=0;x=0;}
            if(apply)item->setGeometry(QRect(inner.topLeft()+QPoint(x,y),size));
            x+=size.width()+spacing();line=qMax(line,size.height());
        }
        return margins.top()+y+line+margins.bottom();
    }
    QList<QLayoutItem*> items;
    bool stacked=false;
};
