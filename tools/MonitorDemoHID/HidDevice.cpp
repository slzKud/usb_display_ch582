#include "pch.h"
#include "HidDevice.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

static void DbgPrint(LPCTSTR fmt, ...)
{
    CString msg;
    va_list args;
    va_start(args, fmt);
    msg.FormatV(fmt, args);
    va_end(args);
    OutputDebugString(msg);
}

CHidDevice::CHidDevice()
{
    InitializeCriticalSection(&m_cs);
    m_ol = {};
    m_ol.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

CHidDevice::~CHidDevice()
{
    Close();
    DeleteCriticalSection(&m_cs);
    CloseHandle(m_ol.hEvent);
}

bool CHidDevice::Open(unsigned short vid, unsigned short pid)
{
    Close();

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO hDevInfo = SetupDiGetClassDevs(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        DbgPrint(_T("[MonitorDemoHID] SetupDiGetClassDevs failed\r\n"));
        return false;
    }

    SP_DEVICE_INTERFACE_DATA devInterfaceData{};
    devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    bool opened = false;
    DWORD memberIndex = 0;

    while (SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &hidGuid, memberIndex, &devInterfaceData))
    {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInterfaceData, nullptr, 0, &requiredSize, nullptr);

        if (requiredSize > 0)
        {
            std::vector<BYTE> detailDataBuffer(requiredSize);
            SP_DEVICE_INTERFACE_DETAIL_DATA* pDetailData = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(detailDataBuffer.data());
            pDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInterfaceData, pDetailData, requiredSize, nullptr, nullptr))
            {
                HANDLE hDevice = CreateFile(pDetailData->DevicePath,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED,
                    nullptr);

                if (hDevice != INVALID_HANDLE_VALUE)
                {
                    HIDD_ATTRIBUTES attrib{};
                    attrib.Size = sizeof(HIDD_ATTRIBUTES);
                    if (HidD_GetAttributes(hDevice, &attrib))
                    {
                        if (attrib.VendorID == vid && attrib.ProductID == pid)
                        {
                            // Get output report length
                            PHIDP_PREPARSED_DATA preparsedData = NULL;
                            if (HidD_GetPreparsedData(hDevice, &preparsedData))
                            {
                                HIDP_CAPS caps;
                                if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS)
                                {
                                    m_outputReportLength = caps.OutputReportByteLength;
                                }
                                HidD_FreePreparsedData(preparsedData);
                            }

                            m_handle = hDevice;
                            opened = true;
                            DbgPrint(_T("[MonitorDemoHID] Opened HID device VID=0x%04X PID=0x%04X path=%S output_len=%d\r\n"),
                                vid, pid, pDetailData->DevicePath, m_outputReportLength);
                            break;
                        }
                    }
                    CloseHandle(hDevice);
                }
            }
        }
        memberIndex++;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    if (!opened)
    {
        DbgPrint(_T("[MonitorDemoHID] Failed to open HID device VID=0x%04X PID=0x%04X\r\n"), vid, pid);
    }
    return opened;
}

void CHidDevice::Close()
{
    if (m_handle != INVALID_HANDLE_VALUE)
    {
        CancelIo(m_handle);
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
    m_outputReportLength = 0;
}

bool CHidDevice::Write(const uint8_t* data, int len)
{
    EnterCriticalSection(&m_cs);
    if (m_handle == INVALID_HANDLE_VALUE)
    {
        LeaveCriticalSection(&m_cs);
        DbgPrint(_T("[MonitorDemoHID] Write failed: device not open\r\n"));
        return false;
    }

    // Ensure we send at least OutputReportByteLength bytes (Windows requirement)
    uint8_t writeBuf[256] = { 0 };
    int length_to_send = len;
    if (len < (int)m_outputReportLength)
    {
        memcpy(writeBuf, data, len);
        memset(writeBuf + len, 0, m_outputReportLength - len);
        length_to_send = m_outputReportLength;
        data = writeBuf;
    }
    else if (len > (int)m_outputReportLength && m_outputReportLength > 0)
    {
        // Truncate to output report length
        length_to_send = m_outputReportLength;
    }

    DWORD bytes_written = 0;
    HANDLE hEvent = m_ol.hEvent;
    m_ol = {};
    m_ol.hEvent = hEvent;
    ResetEvent(m_ol.hEvent);

    BOOL res = WriteFile(m_handle, data, length_to_send, &bytes_written, &m_ol);
    if (!res)
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            DWORD err = GetLastError();
            DbgPrint(_T("[MonitorDemoHID] WriteFile failed immediately: err=%d\r\n"), err);
            LeaveCriticalSection(&m_cs);
            return false;
        }

        // Wait for the overlapped I/O to complete (with timeout)
        res = WaitForSingleObject(m_ol.hEvent, 1000);
        if (res != WAIT_OBJECT_0)
        {
            DbgPrint(_T("[MonitorDemoHID] WriteFile timeout or error: wait_res=%d\r\n"), res);
            CancelIo(m_handle);
            LeaveCriticalSection(&m_cs);
            return false;
        }

        res = GetOverlappedResult(m_handle, &m_ol, &bytes_written, FALSE);
        if (!res)
        {
            DWORD err = GetLastError();
            DbgPrint(_T("[MonitorDemoHID] GetOverlappedResult failed: err=%d\r\n"), err);
            LeaveCriticalSection(&m_cs);
            return false;
        }
    }

    if (bytes_written != (DWORD)length_to_send)
    {
        DbgPrint(_T("[MonitorDemoHID] Write incomplete: %d/%d bytes\r\n"), bytes_written, length_to_send);
        LeaveCriticalSection(&m_cs);
        return false;
    }

    DbgPrint(_T("[MonitorDemoHID] Write success: %d bytes\r\n"), bytes_written);
    LeaveCriticalSection(&m_cs);
    return true;
}

bool CHidDevice::Read(uint8_t* data, int len, int& bytes_read)
{
    EnterCriticalSection(&m_cs);
    bytes_read = 0;
    if (m_handle == INVALID_HANDLE_VALUE)
    {
        LeaveCriticalSection(&m_cs);
        return false;
    }

    OVERLAPPED ol = {};
    ol.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    DWORD read = 0;
    BOOL ok = ReadFile(m_handle, data, len, &read, &ol);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
    {
        WaitForSingleObject(ol.hEvent, 100);
        GetOverlappedResult(m_handle, &ol, &read, FALSE);
    }
    CloseHandle(ol.hEvent);
    bytes_read = (int)read;
    LeaveCriticalSection(&m_cs);
    return ok || read > 0;
}

bool HidEnumerate(unsigned short vid, unsigned short pid)
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO hDevInfo = SetupDiGetClassDevs(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE)
        return false;

    SP_DEVICE_INTERFACE_DATA devInterfaceData{};
    devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    bool found = false;
    DWORD memberIndex = 0;

    while (SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &hidGuid, memberIndex, &devInterfaceData))
    {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInterfaceData, nullptr, 0, &requiredSize, nullptr);

        if (requiredSize > 0)
        {
            std::vector<BYTE> detailDataBuffer(requiredSize);
            SP_DEVICE_INTERFACE_DETAIL_DATA* pDetailData = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(detailDataBuffer.data());
            pDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInterfaceData, pDetailData, requiredSize, nullptr, nullptr))
            {
                HANDLE hDevice = CreateFile(pDetailData->DevicePath,
                    0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr,
                    OPEN_EXISTING,
                    0,
                    nullptr);

                if (hDevice != INVALID_HANDLE_VALUE)
                {
                    HIDD_ATTRIBUTES attrib{};
                    attrib.Size = sizeof(HIDD_ATTRIBUTES);
                    if (HidD_GetAttributes(hDevice, &attrib))
                    {
                        if (attrib.VendorID == vid && attrib.ProductID == pid)
                        {
                            found = true;
                            CloseHandle(hDevice);
                            break;
                        }
                    }
                    CloseHandle(hDevice);
                }
            }
        }
        memberIndex++;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return found;
}
