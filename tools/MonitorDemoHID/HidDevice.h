#pragma once
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <string>
#include <vector>

// Simple HID device wrapper using Windows native HID API
class CHidDevice
{
public:
    CHidDevice();
    ~CHidDevice();

    // Open the first HID device matching vid/pid
    bool Open(unsigned short vid, unsigned short pid);
    void Close();
    bool IsOpen() const { return m_handle != INVALID_HANDLE_VALUE; }

    // Write data (first byte should be Report ID, e.g. 0)
    bool Write(const uint8_t* data, int len);

    // Read data (not used by this plugin, but kept for completeness)
    bool Read(uint8_t* data, int len, int& bytes_read);

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    CRITICAL_SECTION m_cs;
    OVERLAPPED m_ol = {};
    USHORT m_outputReportLength = 0;
};

// Enumerate HID devices, return true if at least one device matches vid/pid
bool HidEnumerate(unsigned short vid, unsigned short pid);
