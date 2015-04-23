#include "IHIDListener.hpp"
#include "inputdev/CDeviceFinder.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>

class CHIDListenerIOKit final : public IHIDListener
{
    CDeviceFinder& m_finder;
    
    CFRunLoopRef m_listenerRunLoop;
    IOHIDManagerRef m_hidManager;
    bool m_scanningEnabled;
    
    static void deviceConnected(CHIDListenerIOKit* listener,
                                IOReturn,
                                void*,
                                IOHIDDeviceRef device)
    {
        if (!listener->m_scanningEnabled)
            return;
        if (listener->m_finder._hasToken(device))
            return;
        CFIndex vid, pid;
        CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey)), kCFNumberCFIndexType, &vid);
        CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey)), kCFNumberCFIndexType, &pid);
        CFStringRef manuf = (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDManufacturerKey));
        CFStringRef product = (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
        listener->m_finder._insertToken(CDeviceToken(vid, pid,
                                                     CFStringGetCStringPtr(manuf, kCFStringEncodingUTF8),
                                                     CFStringGetCStringPtr(product, kCFStringEncodingUTF8),
                                                     device));
    }
    
    static void deviceDisconnected(CHIDListenerIOKit* listener,
                                   IOReturn ret,
                                   void* sender,
                                   IOHIDDeviceRef device)
    {
        if (CFRunLoopGetCurrent() != listener->m_listenerRunLoop)
        {
            CFRunLoopPerformBlock(listener->m_listenerRunLoop, kCFRunLoopDefaultMode, ^{
                deviceDisconnected(listener, ret, sender, device);
            });
            CFRunLoopWakeUp(listener->m_listenerRunLoop);
            return;
        }
        listener->m_finder._removeToken(device);
    }
    
    static void applyDevice(IOHIDDeviceRef device, CHIDListenerIOKit* listener)
    {
        if (listener->m_finder._hasToken(device))
            return;
        CFIndex vid, pid;
        CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey)), kCFNumberCFIndexType, &vid);
        CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey)), kCFNumberCFIndexType, &pid);
        CFStringRef manuf = (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDManufacturerKey));
        CFStringRef product = (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
        listener->m_finder._insertToken(CDeviceToken(vid, pid,
                                                     CFStringGetCStringPtr(manuf, kCFStringEncodingUTF8),
                                                     CFStringGetCStringPtr(product, kCFStringEncodingUTF8),
                                                     device));
    }

public:
    CHIDListenerIOKit(CDeviceFinder& finder)
    : m_finder(finder)
    {
        
        /* Register HID Manager */
        m_hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionNone);
        IOHIDManagerSetDeviceMatching(m_hidManager, NULL);
        IOHIDManagerRegisterDeviceMatchingCallback(m_hidManager, (IOHIDDeviceCallback)deviceConnected, this);
        IOHIDManagerRegisterDeviceRemovalCallback(m_hidManager, (IOHIDDeviceCallback)deviceDisconnected, this);
        m_listenerRunLoop = CFRunLoopGetCurrent();
        IOHIDManagerScheduleWithRunLoop(m_hidManager, m_listenerRunLoop, kCFRunLoopDefaultMode);
        IOReturn ret = IOHIDManagerOpen(m_hidManager, kIOHIDManagerOptionNone);
        if (ret != kIOReturnSuccess)
            throw std::runtime_error("error establishing IOHIDManager");
        
        /* Initial Device Add */
        m_scanningEnabled = true;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
        m_scanningEnabled = false;
        
    }
    
    ~CHIDListenerIOKit()
    {
        IOHIDManagerUnscheduleFromRunLoop(m_hidManager, m_listenerRunLoop, kCFRunLoopDefaultMode);
        IOHIDManagerClose(m_hidManager, kIOHIDManagerOptionNone);
        CFRelease(m_hidManager);
    }
    
    /* Automatic device scanning */
    bool startScanning()
    {
        m_scanningEnabled = true;
        return true;
    }
    bool stopScanning()
    {
        m_scanningEnabled = false;
        return true;
    }
    
    /* Manual device scanning */
    bool scanNow()
    {
        CFSetRef devs = IOHIDManagerCopyDevices(m_hidManager);
        m_finder.m_tokensLock.lock();
        CFSetApplyFunction(devs, (CFSetApplierFunction)applyDevice, this);
        m_finder.m_tokensLock.unlock();
        CFRelease(devs);
        return true;
    }
    
};

IHIDListener* IHIDListenerNew(CDeviceFinder& finder)
{
    return new CHIDListenerIOKit(finder);
}

