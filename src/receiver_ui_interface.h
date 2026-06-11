#ifndef RECEIVER_UI_INTERFACE_H
#define RECEIVER_UI_INTERFACE_H

#include <QObject>
#include <QString>
#include "interface.h"

class ReceiverUiInterface : public PluginInterface
{
public:
    virtual ~ReceiverUiInterface() = default;
};

#define ReceiverUiInterface_iid "org.logos.ReceiverUiInterface"
Q_DECLARE_INTERFACE(ReceiverUiInterface, ReceiverUiInterface_iid)

#endif // RECEIVER_UI_INTERFACE_H
