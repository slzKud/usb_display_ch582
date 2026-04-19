// OptionsDlg.cpp : implementation file
//

#include "pch.h"
#include "MonitorDemoHID.h"
#include "OptionsDlg.h"
#include "afxdialogex.h"


// COptionsDlg dialog

IMPLEMENT_DYNAMIC(COptionsDlg, CDialog)

COptionsDlg::COptionsDlg(CWnd* pParent /*=nullptr*/)
    : CDialog(IDD_OPTIONS_DIALOG, pParent)
{
}

COptionsDlg::~COptionsDlg()
{
}

void COptionsDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_MONITOR_LIST, m_monitor_list);
    DDX_Control(pDX, IDC_TYPE_COMBO, m_type_combo);
}


BEGIN_MESSAGE_MAP(COptionsDlg, CDialog)
    ON_NOTIFY(NM_CLICK, IDC_MONITOR_LIST, &COptionsDlg::OnListItemClick)
    ON_CBN_KILLFOCUS(IDC_TYPE_COMBO, &COptionsDlg::OnComboKillFocus)
END_MESSAGE_MAP()


// COptionsDlg message handlers

BOOL COptionsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // Initialize VID/PID fields
    SetDlgItemText(IDC_VID_EDIT, HexToString(m_data.vid));
    SetDlgItemText(IDC_PID_EDIT, HexToString(m_data.pid));

    // Hide the combo box initially
    m_type_combo.ShowWindow(SW_HIDE);

    // Initialize list control
    InitListCtrl();
    UpdateListFromData();
    UpdateConnectionStatus();

    return TRUE;
}

void COptionsDlg::InitListCtrl()
{
    m_monitor_list.SetExtendedStyle(
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);

    // Columns: Item | VAR_ID | Type | Preview
    m_monitor_list.InsertColumn(0, _T("Item"), LVCFMT_LEFT, 120);
    m_monitor_list.InsertColumn(1, _T("VAR_ID"), LVCFMT_CENTER, 55);
    m_monitor_list.InsertColumn(2, _T("Type"), LVCFMT_CENTER, 55);
    m_monitor_list.InsertColumn(3, _T("Preview"), LVCFMT_LEFT, 120);
}

void COptionsDlg::UpdateListFromData()
{
    m_monitor_list.DeleteAllItems();

    for (size_t i = 0; i < m_data.items.size(); i++)
    {
        const auto& item = m_data.items[i];

        CString name = CDataManager::Instance().StringRes(item.name_res_id);
        int row = m_monitor_list.InsertItem((int)i, name);

        // VAR_ID as hex
        wchar_t var_id_str[16];
        swprintf_s(var_id_str, L"0x%02X", item.var_id);
        m_monitor_list.SetItemText(row, 1, var_id_str);

        // Type string
        m_monitor_list.SetItemText(row, 2, TypeToString(item.variant_type));

        // Preview string (from DataManager, updated during DataRequired)
        CString preview = L"--";
        if (i < m_preview_strings.size() && !m_preview_strings[i].empty())
            preview = m_preview_strings[i].c_str();
        m_monitor_list.SetItemText(row, 3, preview);

        // Checkbox state
        m_monitor_list.SetCheck(row, item.enabled);
    }
}

void COptionsDlg::UpdateDataFromList()
{
    // Read HID settings
    CString vid_str, pid_str;
    GetDlgItemText(IDC_VID_EDIT, vid_str);
    GetDlgItemText(IDC_PID_EDIT, pid_str);
    m_data.vid = ParseHexString(vid_str);
    m_data.pid = ParseHexString(pid_str);

    // Read checkbox states and type from list
    for (int i = 0; i < m_monitor_list.GetItemCount() && i < (int)m_data.items.size(); i++)
    {
        m_data.items[i].enabled = m_monitor_list.GetCheck(i) != 0;

        // Read type from list text
        CString type_str = m_monitor_list.GetItemText(i, 2);
        m_data.items[i].variant_type = StringToType(type_str);
    }
}

const wchar_t* COptionsDlg::TypeToString(int type)
{
    switch (type)
    {
    case VARIANT_TYPE_INT:   return L"Int";
    case VARIANT_TYPE_FLOAT: return L"Float";
    case VARIANT_TYPE_STR:   return L"String";
    default:                 return L"Int";
    }
}

int COptionsDlg::StringToType(const CString& str)
{
    if (str == L"Float")  return VARIANT_TYPE_FLOAT;
    if (str == L"String") return VARIANT_TYPE_STR;
    return VARIANT_TYPE_INT;
}

void COptionsDlg::ShowTypeCombo(int row, int col)
{
    // Commit any previously open combo
    CommitTypeCombo();

    if (row < 0 || row >= m_monitor_list.GetItemCount())
        return;

    CRect cell_rect;
    m_monitor_list.GetSubItemRect(row, col, LVIR_BOUNDS, cell_rect);

    // Convert to dialog client coordinates
    m_monitor_list.ClientToScreen(&cell_rect);
    ScreenToClient(&cell_rect);

    // Populate combo with options
    m_type_combo.ResetContent();
    m_type_combo.AddString(L"Int");
    m_type_combo.AddString(L"Float");
    m_type_combo.AddString(L"String");

    // Select current value
    CString current = m_monitor_list.GetItemText(row, col);
    m_type_combo.SelectString(-1, current);

    // Position and show
    m_type_combo.MoveWindow(&cell_rect, TRUE);
    m_type_combo.ShowWindow(SW_SHOW);
    m_type_combo.SetFocus();
    m_type_combo.ShowDropDown(TRUE);

    m_combo_item = row;
    m_combo_col = col;
}

void COptionsDlg::CommitTypeCombo()
{
    if (m_combo_item >= 0 && m_combo_item < m_monitor_list.GetItemCount())
    {
        CString text;
        m_type_combo.GetWindowText(text);
        if (!text.IsEmpty())
        {
            m_monitor_list.SetItemText(m_combo_item, m_combo_col, text);
        }
        m_type_combo.ShowWindow(SW_HIDE);
        m_combo_item = -1;
    }
}

void COptionsDlg::OnListItemClick(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMITEMACTIVATE pNMIA = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);

    if (pNMIA->iItem >= 0 && pNMIA->iSubItem == 2)  // Clicked on Type column
    {
        ShowTypeCombo(pNMIA->iItem, pNMIA->iSubItem);
    }

    *pResult = 0;
}

void COptionsDlg::OnComboKillFocus()
{
    CommitTypeCombo();
}

void COptionsDlg::OnOK()
{
    CommitTypeCombo();
    UpdateDataFromList();
    CDialog::OnOK();
}

CString COptionsDlg::IntToString(int value) const
{
    CString str;
    str.Format(_T("%d"), value);
    return str;
}

CString COptionsDlg::HexToString(int value) const
{
    CString str;
    str.Format(_T("0x%04X"), value);
    return str;
}

int COptionsDlg::ParseHexString(const CString& str) const
{
    CString s = str;
    s.Trim();
    if (s.Left(2).CompareNoCase(_T("0x")) == 0)
        return (int)_tcstoul(s, nullptr, 16);
    return _ttoi(s);
}

void COptionsDlg::UpdateConnectionStatus()
{
    // Read current VID/PID from dialog for enumeration
    CString vid_str, pid_str;
    GetDlgItemText(IDC_VID_EDIT, vid_str);
    GetDlgItemText(IDC_PID_EDIT, pid_str);
    int vid = ParseHexString(vid_str);
    int pid = ParseHexString(pid_str);

    // Enumerate HID devices with current VID/PID
    bool found = HidEnumerate((unsigned short)vid, (unsigned short)pid);

    CString status;
    if (found)
        status.Format(_T("Device detected"));
    else
        status.Format(_T("No device detected"));

    SetDlgItemText(IDC_STATIC_STATUS, status);
}
