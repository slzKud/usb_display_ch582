#pragma once
#include <string>
#include <vector>
#include <map>
#include "PluginInterface.h"

#include "HidDevice.h"

#define g_data CDataManager::Instance()

// Protocol constants
#define PROTOCOL_MAGIC1     0x44  // 'D'
#define PROTOCOL_MAGIC2     0x47  // 'G'
#define COMMAND_MCU_OPT     0x02
#define COMMAND_MCU_OPT_RESP (0x02 | 0x20)
#define MCU_OPT_VAR_SET     0x05

// Variant types
#define VARIANT_TYPE_INT    0
#define VARIANT_TYPE_FLOAT  1
#define VARIANT_TYPE_BOOL   2
#define VARIANT_TYPE_STR    3

// Max UTF-8 string length in protocol
#define VARIANT_STR_MAX_LEN 15

// Packet size (same as USB HID)
#define PACKET_SIZE         64

struct MonitorItemConfig
{
    std::wstring name;                       // display name (resource string)
    UINT name_res_id;                        // string resource ID
    int var_id;                              // VAR_ID: 0xF0~0xFF
    int variant_type;                        // PACKED_VARIANT_DATA type (0=INT, 1=FLOAT, 3=STR)
    ITrafficMonitor::MonitorItem monitor_item; // which TrafficMonitor item to read
    bool enabled;                            // whether to send this item
};

struct SettingData
{
    int vid = 0x413D;
    int pid = 0x2107;
    int send_interval_ms = 1000;
    std::vector<MonitorItemConfig> items;
};

class CDataManager
{
private:
    CDataManager();
    ~CDataManager();

public:
    static CDataManager& Instance();

    void LoadConfig(const std::wstring& config_dir);
    void SaveConfig() const;
    const CString& StringRes(UINT id);      // get a string resource by ID

    // HID connection management
    bool ConnectHID();
    void DisconnectHID();
    bool IsHIDConnected() const;

    // Build and send all enabled items as protocol packets
    void SendMonitorData(ITrafficMonitor* app);

    // Preview strings (updated in DataRequired, shown in OptionsDlg)
    std::vector<std::wstring> m_preview_strings;

public:
    SettingData m_setting_data;

private:
    static CDataManager m_instance;
    std::wstring m_config_path;
    CHidDevice m_hid_device;
    std::map<UINT, CString> m_string_table;

    // Initialize default monitor item list
    void InitDefaultItems();

    // Build a single 64-byte protocol packet for writing a variant
    int BuildVarSetPacket(uint8_t* packet, int var_id, int variant_type, const void* data, int data_len);

    // Convert a wide string to UTF-8, truncated to VARIANT_STR_MAX_LEN bytes
    static std::string WideToUtf8Truncated(const std::wstring& wstr);

    // Send raw packet via HID
    bool SendHIDPacket(const uint8_t* packet, int len);
};
