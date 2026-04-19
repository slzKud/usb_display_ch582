#include "pch.h"
#include "DataManager.h"

CDataManager CDataManager::m_instance;

CDataManager::CDataManager()
{
}

CDataManager::~CDataManager()
{
    SaveConfig();
    DisconnectHID();
}

CDataManager& CDataManager::Instance()
{
    return m_instance;
}

const CString& CDataManager::StringRes(UINT id)
{
    auto iter = m_string_table.find(id);
    if (iter != m_string_table.end())
    {
        return iter->second;
    }
    else
    {
        AFX_MANAGE_STATE(AfxGetStaticModuleState());
        m_string_table[id].LoadString(id);
        return m_string_table[id];
    }
}

void CDataManager::InitDefaultItems()
{
    m_setting_data.items.clear();

    // Define all 13 monitor items with their default VAR_ID assignments
    struct ItemDef
    {
        UINT name_res_id;
        int var_id;
        int variant_type;
        ITrafficMonitor::MonitorItem monitor_item;
        bool default_enabled;
    };

    static const ItemDef defs[] = {
        { IDS_ITEM_CPU,             0xF0, VARIANT_TYPE_FLOAT, ITrafficMonitor::MI_CPU,              true  },
        { IDS_ITEM_MEMORY,          0xF1, VARIANT_TYPE_FLOAT, ITrafficMonitor::MI_MEMORY,           true  },
        { IDS_ITEM_GPU_USAGE,       0xF2, VARIANT_TYPE_FLOAT, ITrafficMonitor::MI_GPU_USAGE,        true  },
        { IDS_ITEM_HDD_USAGE,       0xF3, VARIANT_TYPE_FLOAT, ITrafficMonitor::MI_HDD_USAGE,        false },
        { IDS_ITEM_CPU_TEMP,        0xF4, VARIANT_TYPE_FLOAT, ITrafficMonitor::MI_CPU_TEMP,         false },
        { IDS_ITEM_GPU_TEMP,        0xF5, VARIANT_TYPE_FLOAT, ITrafficMonitor::MI_GPU_TEMP,         false },
        { IDS_ITEM_HDD_TEMP,        0xF6, VARIANT_TYPE_FLOAT, ITrafficMonitor::MI_HDD_TEMP,         false },
        { IDS_ITEM_MAIN_BOARD_TEMP, 0xF7, VARIANT_TYPE_FLOAT, ITrafficMonitor::MI_MAIN_BOARD_TEMP,  false },
        { IDS_ITEM_CPU_FREQ,        0xF8, VARIANT_TYPE_INT,   ITrafficMonitor::MI_CPU_FREQ,         false },
        { IDS_ITEM_UP_SPEED,        0xF9, VARIANT_TYPE_INT,   ITrafficMonitor::MI_UP,               false },
        { IDS_ITEM_DOWN_SPEED,      0xFA, VARIANT_TYPE_INT,   ITrafficMonitor::MI_DOWN,             false },
        { IDS_ITEM_TODAY_UP,        0xFB, VARIANT_TYPE_INT,   ITrafficMonitor::MI_TODAY_UP_TRAFFIC, false },
        { IDS_ITEM_TODAY_DOWN,      0xFC, VARIANT_TYPE_INT,   ITrafficMonitor::MI_TODAY_DOWN_TRAFFIC, false },
    };

    for (const auto& d : defs)
    {
        MonitorItemConfig cfg;
        cfg.name_res_id = d.name_res_id;
        cfg.var_id = d.var_id;
        cfg.variant_type = d.variant_type;
        cfg.monitor_item = d.monitor_item;
        cfg.enabled = d.default_enabled;
        m_setting_data.items.push_back(cfg);
    }

    m_preview_strings.resize(m_setting_data.items.size());
}

static void WritePrivateProfileInt(const wchar_t* app_name, const wchar_t* key_name, int value, const wchar_t* file_path)
{
    wchar_t buff[16];
    swprintf_s(buff, L"%d", value);
    WritePrivateProfileString(app_name, key_name, buff, file_path);
}

static void WritePrivateProfileString2(const wchar_t* app_name, const wchar_t* key_name, const std::wstring& value, const wchar_t* file_path)
{
    WritePrivateProfileString(app_name, key_name, value.c_str(), file_path);
}

void CDataManager::LoadConfig(const std::wstring& config_dir)
{
    // Compute config path: <config_dir>/<ModuleFilename>.ini
    HMODULE hModule = reinterpret_cast<HMODULE>(&__ImageBase);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(hModule, path, MAX_PATH);
    std::wstring module_path = path;
    m_config_path = module_path;
    if (!config_dir.empty())
    {
        size_t index = module_path.find_last_of(L"\\/");
        std::wstring module_file_name = module_path.substr(index + 1);
        m_config_path = config_dir + module_file_name;
    }
    m_config_path += L".ini";

    // Initialize default items first (so order is always consistent)
    InitDefaultItems();

    // Load HID settings
    m_setting_data.vid = GetPrivateProfileInt(_T("config"), _T("vid"), 0x413D, m_config_path.c_str());
    m_setting_data.pid = GetPrivateProfileInt(_T("config"), _T("pid"), 0x2107, m_config_path.c_str());
    m_setting_data.send_interval_ms = GetPrivateProfileInt(_T("config"), _T("send_interval_ms"), 1000, m_config_path.c_str());

    // Load per-item settings
    for (size_t i = 0; i < m_setting_data.items.size(); i++)
    {
        wchar_t key[64];
        swprintf_s(key, L"item%d_enabled", (int)i);
        int enabled = GetPrivateProfileInt(_T("items"), key, m_setting_data.items[i].enabled ? 1 : 0, m_config_path.c_str());
        m_setting_data.items[i].enabled = (enabled != 0);

        swprintf_s(key, L"item%d_var_id", (int)i);
        int var_id = GetPrivateProfileInt(_T("items"), key, m_setting_data.items[i].var_id, m_config_path.c_str());
        m_setting_data.items[i].var_id = var_id;

        swprintf_s(key, L"item%d_type", (int)i);
        int type = GetPrivateProfileInt(_T("items"), key, m_setting_data.items[i].variant_type, m_config_path.c_str());
        m_setting_data.items[i].variant_type = type;
    }
}

void CDataManager::SaveConfig() const
{
    if (m_config_path.empty())
        return;

    WritePrivateProfileInt(_T("config"), _T("vid"), m_setting_data.vid, m_config_path.c_str());
    WritePrivateProfileInt(_T("config"), _T("pid"), m_setting_data.pid, m_config_path.c_str());
    WritePrivateProfileInt(_T("config"), _T("send_interval_ms"), m_setting_data.send_interval_ms, m_config_path.c_str());

    for (size_t i = 0; i < m_setting_data.items.size(); i++)
    {
        wchar_t key[64];
        swprintf_s(key, L"item%d_enabled", (int)i);
        WritePrivateProfileInt(_T("items"), key, m_setting_data.items[i].enabled ? 1 : 0, m_config_path.c_str());

        swprintf_s(key, L"item%d_var_id", (int)i);
        WritePrivateProfileInt(_T("items"), key, m_setting_data.items[i].var_id, m_config_path.c_str());

        swprintf_s(key, L"item%d_type", (int)i);
        WritePrivateProfileInt(_T("items"), key, m_setting_data.items[i].variant_type, m_config_path.c_str());
    }
}

bool CDataManager::ConnectHID()
{
    if (m_hid_device.IsOpen())
        return true;

    return m_hid_device.Open((unsigned short)m_setting_data.vid, (unsigned short)m_setting_data.pid);
}

void CDataManager::DisconnectHID()
{
    m_hid_device.Close();
}

bool CDataManager::IsHIDConnected() const
{
    return m_hid_device.IsOpen();
}

std::string CDataManager::WideToUtf8Truncated(const std::wstring& wstr)
{
    if (wstr.empty())
        return std::string();

    // Convert to UTF-8
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(utf8_len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &utf8[0], utf8_len, nullptr, nullptr);

    // Truncate to VARIANT_STR_MAX_LEN bytes
    if (utf8.size() > VARIANT_STR_MAX_LEN)
        utf8.resize(VARIANT_STR_MAX_LEN);

    return utf8;
}

int CDataManager::BuildVarSetPacket(uint8_t* packet, int var_id, int variant_type, const void* data, int data_len)
{
    // Build the DATA field: [0]=sub_command(0x05), [1]=VAR_ID, [2..]=PACKED_VARIANT_DATA
    uint8_t data_field[59];
    data_field[0] = MCU_OPT_VAR_SET;
    data_field[1] = (uint8_t)var_id;

    // Pack variant data
    int variant_offset = 2;
    data_field[variant_offset] = (uint8_t)variant_type;
    data_field[variant_offset + 1] = (uint8_t)(data_len & 0xFF);
    data_field[variant_offset + 2] = (uint8_t)((data_len >> 8) & 0xFF);
    memcpy(data_field + variant_offset + 3, data, data_len);

    int data_field_len = 2 + 3 + data_len;  // sub_cmd + var_id + type_header + data

    // Build full 64-byte packet
    memset(packet, 0, PACKET_SIZE);
    packet[0] = PROTOCOL_MAGIC1;  // 'D'
    packet[1] = PROTOCOL_MAGIC2;  // 'G'
    packet[2] = COMMAND_MCU_OPT;  // 0x02
    packet[3] = (uint8_t)data_field_len;
    memcpy(packet + 4, data_field, data_field_len);

    // Checksum
    uint8_t checksum = 0;
    int checksum_range = 4 + data_field_len;
    for (int i = 0; i < checksum_range; i++)
        checksum += packet[i];
    packet[checksum_range] = checksum;

    return PACKET_SIZE;
}

bool CDataManager::SendHIDPacket(const uint8_t* packet, int len)
{
    if (!m_hid_device.IsOpen())
        return false;

    // HID write first byte is Report ID, followed by 64-byte data
    uint8_t writeBuf[PACKET_SIZE + 1] = { 0 };
    memcpy(writeBuf + 1, packet, len);
    return m_hid_device.Write(writeBuf, PACKET_SIZE + 1);
}

void CDataManager::SendMonitorData(ITrafficMonitor* app)
{
    if (!IsHIDConnected() || app == nullptr)
        return;

    for (size_t idx = 0; idx < m_setting_data.items.size(); idx++)
    {
        auto& item = m_setting_data.items[idx];
        if (!item.enabled)
            continue;

        double value = app->GetMonitorValue(item.monitor_item);
        uint8_t packet[PACKET_SIZE];

        if (item.variant_type == VARIANT_TYPE_STR)
        {
            // Get the formatted string from TrafficMonitor (with units)
            const wchar_t* str_val = app->GetMonitorValueString(item.monitor_item, true);
            std::string utf8 = WideToUtf8Truncated(str_val ? str_val : L"");
            BuildVarSetPacket(packet, item.var_id, VARIANT_TYPE_STR, utf8.data(), (int)utf8.size());
        }
        else if (item.variant_type == VARIANT_TYPE_FLOAT)
        {
            float fval = (float)value;
            BuildVarSetPacket(packet, item.var_id, VARIANT_TYPE_FLOAT, &fval, 4);
        }
        else // VARIANT_TYPE_INT
        {
            int32_t ival = (int32_t)value;
            BuildVarSetPacket(packet, item.var_id, VARIANT_TYPE_INT, &ival, 4);
        }

        if (!SendHIDPacket(packet, PACKET_SIZE))
        {
            DisconnectHID();
            return;
        }
    }

    // Update preview strings
    if (m_preview_strings.size() < m_setting_data.items.size())
        m_preview_strings.resize(m_setting_data.items.size());

    for (size_t idx = 0; idx < m_setting_data.items.size(); idx++)
    {
        const auto& item = m_setting_data.items[idx];
        const wchar_t* str_val = app->GetMonitorValueString(item.monitor_item, true);
        m_preview_strings[idx] = str_val ? str_val : L"--";
    }
}
