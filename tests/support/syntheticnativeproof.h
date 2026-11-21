#pragma once
#include "nativeinputintent.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>

// TEST ONLY. This deliberately synthetic accepted activation is not an OS/human
// input claim, commissioning evidence or production synchronization option.
namespace SyntheticNativeProof {
class Carrier final:public QObject {
public:
    QQuickWindow window;
    QQuickItem control{window.contentItem()};
    explicit Carrier(QObject *parent):QObject(parent){
        window.resize(80,80);control.setSize({40,40});
        control.setProperty("pressed",false);control.setProperty("nativeActionEnabled",true);
    }
};
}
class NativeInputIntentTestAccess {
public:
    static bool acceptedActivation(NativeInputIntentAdapter &adapter,const QString &action,
                                  NativeInputIntentAdapter::BindingProvider bindingProvider,
                                  const std::function<void(QObject *)> &registerControl){
        SyntheticNativeProof::Carrier *carrier=nullptr;
        for(auto child:adapter.children())if(auto found=dynamic_cast<SyntheticNativeProof::Carrier *>(child)){carrier=found;break;}
        if(!carrier)carrier=new SyntheticNativeProof::Carrier(&adapter);
        const QPointer<NativeInputIntentAdapter> alive(&adapter);
        carrier->window.show();carrier->window.requestActivate();
        QElapsedTimer wait;wait.start();
        while(alive&&!carrier->window.isActive()&&wait.elapsed()<1000){
            QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);
        }
        if(!alive||!carrier->window.isActive())return false;
        registerControl(&carrier->control);
        if(!alive)return false;
        QThread::msleep(1); // Accepted activation is newer than real registration.
        const auto binding=bindingProvider();if(binding.isEmpty())return false;
        const auto now=NativeInputIntentAdapter::nowMs();
        adapter.m_domain=NativeInputIntentAdapter::Domain::X11Server32;
        adapter.m_sample={1000,now-5,now-4};
        const auto key=action+"#"+QString::number(quintptr(&carrier->control),16);
        auto token=std::make_shared<NativeInputContextToken>();
        adapter.m_controls[key]=&carrier->control;adapter.m_clickBindings[key]=std::move(bindingProvider);
        adapter.m_tokens[key]=token;
        adapter.m_pending[key]={{++adapter.m_interaction,now,now+250,binding,token},
            &carrier->control,&carrier->window,adapter.m_context,false,true};
        return true;
    }
};
