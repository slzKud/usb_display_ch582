#pragma once
#include "DataManager.h"

// COptionsDlg dialog

class COptionsDlg : public CDialog
{
    DECLARE_DYNAMIC(COptionsDlg)

public:
    COptionsDlg(CWnd* pParent = nullptr);
    virtual ~COptionsDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_OPTIONS_DIALOG };
#endif

    SettingData m_data;
    std::vector<std::wstring> m_preview_strings; // preview from DataManager

protected:
    virtual void DoDataExchange(CDataExchange* pDX);

    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
    virtual void OnOK();

private:
    CListCtrl m_monitor_list;
    CComboBox m_type_combo;    // in-place combo for type editing
    int m_combo_item = -1;     // row index being edited
    int m_combo_col = 2;       // column index (Type column)

    void InitListCtrl();
    void UpdateListFromData();
    void UpdateDataFromList();
    void ShowTypeCombo(int row, int col);
    void CommitTypeCombo();
    void UpdateConnectionStatus();

    CString IntToString(int value) const;
    CString HexToString(int value) const;
    int ParseHexString(const CString& str) const;

    // Type enum to string helpers
    static const wchar_t* TypeToString(int type);
    static int StringToType(const CString& str);

    afx_msg void OnListItemClick(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnComboKillFocus();
};
