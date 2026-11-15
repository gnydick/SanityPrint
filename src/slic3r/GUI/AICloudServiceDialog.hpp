#ifndef slic3r_AICloudServiceDialog_hpp_
#define slic3r_AICloudServiceDialog_hpp_

#include "GUI_Utils.hpp"
#include "2DBed.hpp"
#include "I18N.hpp"
#include "slic3r/Utils/Http.hpp"

#include <libslic3r/BuildVolume.hpp>

#include <wx/dialog.h>
#include <wx/choicebk.h>
#include <list>

#include "Widgets/TextInput.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/HoverBorderIcon.hpp"
#include "Widgets/ProgressBar.hpp"
#include "UnsavedChangesDialog.hpp"

namespace Slic3r {
namespace GUI {
class AICloudService;
class CommWithAICloudService
{
public:
    enum ENTaskState : char {ENTS_NULL, ENTS_AddTask, ENTS_Detail, ENTS_Result, ENTS_FAIL, ENTS_OK};
    enum ENRespDataType : char { ENRDT_NULL, ENRDT_SupportGeneration, ENRDT_ZseamPainting };
    struct STRespData
    {
        ENRespDataType dataType = ENRDT_NULL; 
        std::string object_id = "";
        std::string part_name = "";
        int enable_support = -1;
        std::string support_type = "";
        std::vector<int> block_seam_domains = {};
    };
    enum ENTaskRespDataType : char { ENTRDT_NULL, ENTRDT_AddTask, ENTRDT_Detail};
    struct STTaskRespData
    {
        ENTaskRespDataType dataType = ENTRDT_NULL;
        int code;
        std::string msg;
        std::string reqId;
        std::string result;
        int state;
        int waitingCountAhead = 0;
        int waitingCountTotal = 0;
        int waitingLeftTime = 0;
        int waitingTime = 0;
    };

    CommWithAICloudService();
    ~CommWithAICloudService();
    int addTask(const std::string& host, const std::string& url, const std::string& filePath, STTaskRespData& stAddTaskRespData);
    int detail(const std::string& host, const std::string& url, const std::string& recordId, STTaskRespData& stDetailTaskRespData);
    int result(const std::string& host, const std::string& url, const std::string& recordId);
    int getAIRecommendationSupportGeneration(const std::string& host, const std::string& url, const std::string& recordId, std::list<STRespData>& response, std::string& errorInfo);
    int getAIRecommendationZseamPainting(const std::string& host, const std::string& url, const std::string& recordId, std::list<STRespData>& response, std::string& errorInfo);
    ENTaskState getTaskState();
    void cancel();
    void reset();

private:
    void updateHttp(Http* http);
    std::string getApiUrl();
    ENTaskState m_taskState = ENTS_NULL;
    std::mutex  m_mutexHttp;
    Http* m_http = nullptr;
};

class AICloudService_ResultListItem : public wxPanel
{
public:
    AICloudService_ResultListItem(wxWindow* parent, wxSize itemSize);
    void updateData(const wxString& modelName, const wxString& beforProcessing, const wxString& afterProcessing);

private:
    wxString wrapText(const wxString& text, wxDC& dc, int maxWidth);
    
    wxBoxSizer* m_mainSizer = nullptr;
    wxStaticText* m_modelName = nullptr;
    wxStaticText* m_beforeProcessing = nullptr;
    wxStaticText* m_afterProcessing = nullptr;
    int m_columnWidth = 0;
};

class AICloudService_ResultDialog : public DPIDialog
{
public:
    AICloudService_ResultDialog(wxWindow* parent);

    void build_dialog();

    void on_dpi_changed(const wxRect& suggested_rect) override{}
    void updateData(const std::list<CommWithAICloudService::STRespData>& datas);
    void updateDataToModel(const std::list<CommWithAICloudService::STRespData>& datas, std::function<void(int, int)> funProcessCb);
    int  getDataCount(const std::list<CommWithAICloudService::STRespData>& datas);

private:
    void on_discard_btn_clicked(wxEvent&);
    void on_apply_btn_clicked(wxEvent&);
    const ModelObject * getModelObjectBy3mfPartId(int partId);
    bool getModelObjectBy3mfPartId(int partId, std::vector<const ModelObject*>& vtMO);
    bool blockerModelFacets(int partId, const std::vector<int>& vtFacetIdx, std::function<void(int, int)> funProcessCb);
    bool blockerModelFacets(const ModelObject* mo, const std::vector<int>& vtFacetIdx, std::function<void(int, int)> funProcessCb);

private:
    wxScrolledWindow* m_scrolled_window = nullptr;
    wxBoxSizer* m_scrolled_window_sizer = nullptr;
    DiffViewCtrl* m_tree = nullptr;
    int m_dataCount = 0;
};

class AICloudService_ProgressDialog : public DPIDialog
{
public:
    AICloudService_ProgressDialog(wxWindow* parent);

    void build_dialog();

    void on_dpi_changed(const wxRect& suggested_rect) override{}
    void updateProgress(const wxString& info, int step);
    void updateQueue(const wxString& errorInfo, int waitingCountAhead, int waitingCountTotal);
    void updateTimeout(int step, int timeout);

    void reset();
    void updateHeadText(const wxString& head);
    void setProcessMax(int max);
    void disable();

private:
    void on_cancel_btn_clicked(wxEvent&);

private:
    wxStaticText* m_headText = nullptr; 
    ProgressBar* m_progressBar = nullptr;
    wxStaticText* m_progressText = nullptr;
    wxStaticText* m_info = nullptr;
    wxStaticText* m_queueText = nullptr;
    wxStaticText* m_timeoutText = nullptr;
    Button* m_btnCancel = nullptr;
    bool m_bEnable = true;
    int m_nStep = 0;
};

class AICloudService_QueueDialog : public DPIDialog
{
public:
    AICloudService_QueueDialog(wxWindow* parent);

    void build_dialog();
    void on_dpi_changed(const wxRect& suggested_rect) override{}
    void updateQueue(bool waiting, int waitingCountAhead, int waitingCountTotal);
    int  getShowModelRet();

private:
    void on_cancel_btn_clicked(wxEvent&);

private:
    int m_showModelRet = -1;
    wxStaticText* m_queueText = nullptr;
};

class AICloudService_TipDialog : public DPIDialog
{
public:
    AICloudService_TipDialog(wxWindow* parent);

    void build_dialog();

    void on_dpi_changed(const wxRect& suggested_rect) override {}
    bool getCheckedSupportGeneration();
    bool getCheckedZseamPainting();

private:
    void on_ok_btn_clicked(wxEvent&);

private:
    ::CheckBox* m_ckAIRecommendationSupportGeneration = nullptr;
    ::CheckBox* m_ckAIRecommendationZseamPainting = nullptr;
    Button* m_okBtn = nullptr;
};

class AICloudService
{
public:
    enum class ENAICmd {
        ENAIC_NULL,
        ENAIC_SAVE_3MF,
        ENAIC_CLOUD_PROCESS,
    };
    static AICloudService* getInstance();
    void run();
    void doAIRecommendation(bool bAIRecommendationSupportGeneration, bool bAIRecommendationZseamPainting);
    std::string get3mfPath() { return m_3mfPath; }
    void getRespData(std::list<CommWithAICloudService::STRespData>& lstRespData);
    void cleanup(); // Cleanup method; releases resources when switching languages

private:
    int startup();
    void shutdown();
    void onRun();

    int doSave3mf();
    int doCloudProcess();
    std::string getAIUrl();

private:
    AICloudService();
    ~AICloudService();

private:
    std::thread m_thread;
    std::atomic_bool m_bRunning = false;
    std::atomic_bool m_bStoped  = false;
    std::mutex m_mutexQuit;
    std::condition_variable m_cvQuit;
    std::list<ENAICmd> m_lstAICmd;
    std::mutex m_mutexLstAICmd;

    std::shared_ptr<AICloudService_ProgressDialog> m_progressDlg = nullptr;
    //std::shared_ptr<AICloudService_QueueDialog> m_queueDlg = nullptr;
    std::string m_3mfPath = "";
    std::mutex m_mutexRespData;
    std::list<CommWithAICloudService::STRespData>  m_lstRespData;
    bool m_bAIRecommendationSupportGeneration = false;
    bool m_bAIRecommendationZseamPainting = false;

    std::mutex  m_mutexErrMsg;
    std::string m_errMsg = "";
    std::string m_host;
    std::mutex  m_mutexHost;

    CommWithAICloudService commSupportService;
    CommWithAICloudService commZSeamService;
};

} // GUI
} // Slic3r


#endif  /* slic3r_BedShapeDialog_hpp_ */
