#include "pch.h"
#include "MonitorDemoHID.h"
#include "DataManager.h"
#include "OptionsDlg.h"

CMonitorDemoHID CMonitorDemoHID::m_instance;

CMonitorDemoHID::CMonitorDemoHID()
{
}

CMonitorDemoHID& CMonitorDemoHID::Instance()
{
    return m_instance;
}

IPluginItem* CMonitorDemoHID::GetItem(int index)
{
    // This plugin has no display items - it is a pure data sender
    return nullptr;
}

void CMonitorDemoHID::DataRequired()
{
    if (m_app == nullptr)
        return;

    // Try to connect if not connected
    g_data.ConnectHID();

    // Send all enabled monitor items over HID
    g_data.SendMonitorData(m_app);
}

const wchar_t* CMonitorDemoHID::GetInfo(PluginInfoIndex index)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    static CString str;
    switch (index)
    {
    case TMI_NAME:
        str.LoadString(IDS_PLUGIN_NAME);
        return str.GetString();
    case TMI_DESCRIPTION:
        str.LoadString(IDS_PLUGIN_DESCRIPTION);
        return str.GetString();
    case TMI_AUTHOR:
        return L"TrafficMonitor";
    case TMI_COPYRIGHT:
        return L"Copyright (C) 2026";
    case TMI_VERSION:
        return L"1.0";
    case TMI_URL:
        return L"https://github.com/zhongyang219/TrafficMonitor";
    default:
        break;
    }
    return L"";
}

ITMPlugin::OptionReturn CMonitorDemoHID::ShowOptionsDialog(void* hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    COptionsDlg dlg(CWnd::FromHandle((HWND)hParent));
    dlg.m_data = CDataManager::Instance().m_setting_data;
    dlg.m_preview_strings = CDataManager::Instance().m_preview_strings;
    if (dlg.DoModal() == IDOK)
    {
        CDataManager::Instance().m_setting_data = dlg.m_data;
        // Reconnect with new settings
        g_data.DisconnectHID();
        return ITMPlugin::OR_OPTION_CHANGED;
    }
    return ITMPlugin::OR_OPTION_UNCHANGED;
}

void CMonitorDemoHID::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    switch (index)
    {
    case ITMPlugin::EI_CONFIG_DIR:
        g_data.LoadConfig(std::wstring(data));
        break;
    default:
        break;
    }
}

void CMonitorDemoHID::OnInitialize(ITrafficMonitor* pApp)
{
    m_app = pApp;
}

ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CMonitorDemoHID::Instance();
}
