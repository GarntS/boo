#ifndef CDOLPHINSMASHADAPTER_HPP
#define CDOLPHINSMASHADAPTER_HPP

#include "CDeviceBase.hpp"

class CDolphinSmashAdapter final : public CDeviceBase
{
    void deviceDisconnected();
public:
    CDolphinSmashAdapter(CDeviceToken* token, IHIDDevice* hidDev);
    ~CDolphinSmashAdapter();
};

#endif // CDOLPHINSMASHADAPTER_HPP
