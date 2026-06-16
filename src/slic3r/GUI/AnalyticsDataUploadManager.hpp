#ifndef ANALYTICS_DATA_UPLOAD_MANAGER_HPP
#define ANALYTICS_DATA_UPLOAD_MANAGER_HPP

#include <functional>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <map>
#include <future>
#include <string>
#include "nlohmann/json.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
namespace GUI {

class PartPlate;  // forward declaration

// when to upload analytics data
enum class AnalyticsUploadTiming {
    ON_CLICK_START_PRINT_CMD,        // when user clicks the ("start print" or "send only") command(on SendToPrinter front page)
    ON_SLICE_PLATE_CMD,    //when user clicks the (slice or slice all) command
    ON_FIRST_LAUNCH,      // when software is launched for the first time (when "AppData\Roaming\Creality" directory first created)
    ON_PREFERENCES_CHANGED,    //when user close the preference dialog
    ON_SOFTWARE_LAUNCH,      //when creality slicer launch, it could possibly launch multiple times every day
    ON_SOFTWARE_CRASH,        // when software crash and then reboot
    ON_SOFTWARE_CLOSE        // when software close
};

// what kind of data to upload
enum class AnalyticsDataEventType {
    ANALYTICS_GLOBAL_PRINT_PARAMS,
    ANALYTICS_OBJECT_PRINT_PARAMS,
    ANALYTICS_SLICE_PLATE,
    ANALYTICS_FIRST_LAUNCH,
    ANALYTICS_PREFERENCES_CHANGED,
    ANALYTICS_SOFTWARE_LAUNCH,
    ANALYTICS_SOFTWARE_CRASH,
    ANALYTICS_BAD_ALLOC,
    ANALYTICS_SOFTWARE_CLOSE,
    ANALYTICS_DEVICE_INFO,
    ANALYTICS_ACCOUNT_DEVICE_INFO,
    ANALYTICS_ONLINE_MODELS,
    ANALYTICS_PREPARE,
    ANALYTICS_PREVIEW,
    ANALYTICS_DEVICE,
    ANALYTICS_CLICK_HOME_PAGE_PROJECTS,
    ANALYTICS_CLICK_HOME_PAGE_ONLINE_PARAMS,
    ANALYTICS_CLICK_HOME_PAGE_TUTORIALS,
    ANALYTICS_CLICK_HOME_PAGE_PERSON_CENTER,
    ANALYTICS_CLICK_HOME_PAGE_FEEDBACK,
    ANALYTICS_CLICK_HOME_PAGE_MAKENOW,
    ANALYTICS_CLICK_HOME_PAGE_CREALITYMALL,
    ANALYTICS_MODEL_ACTION_ADD,
    ANALYTICS_MODEL_ACTION_ADD_PLATE,
    ANALYTICS_MODEL_ACTION_MOVE,
    ANALYTICS_MODEL_ACTION_ROTATE,
    ANALYTICS_MODEL_ACTION_AUTO_ORIENT,
    ANALYTICS_MODEL_ACTION_ARRANGE_ALL,
    ANALYTICS_MODEL_ACTION_LAY_ON_FACE,
    ANALYTICS_MODEL_ACTION_SPLIT_TO_OBJECTS,
    ANALYTICS_MODEL_ACTION_SPLIT_TO_PARTS,
    ANALYTICS_MODEL_ACTION_SCALE,
    ANALYTICS_MODEL_ACTION_HOLLOW,
    ANALYTICS_MODEL_ACTION_ADD_HOLE,
    ANALYTICS_MODEL_ACTION_CUT,
    ANALYTICS_MODEL_ACTION_BOOLEAN,
    ANALYTICS_MODEL_ACTION_MEASURE,
    ANALYTICS_MODEL_ACTION_SUPPORT_PAINT,
    ANALYTICS_MODEL_ACTION_ZSEAM_PAINT,
    ANALYTICS_MODEL_ACTION_VARIABLE_LAYER,
    ANALYTICS_MODEL_ACTION_PAINT,
    ANALYTICS_MODEL_ACTION_EMBOSS,
    ANALYTICS_MODEL_ACTION_ASSEMBLY_VIEW,
    ANALYTICS_AI_SERVICE_CALL,
    ANALYTICS_GOTO_WIKI,
    ANALYTICS_GOTO_SUPPORT,
    ANALYTICS_TAB_HOME,
    ANALYTICS_SLICE_SINGLE_COMPLETE,
    ANALYTICS_SLICE_ALL_COMPLETE,
    ANALYTICS_PRINT_SEND,
    ANALYTICS_PRINT_BEGIN,
    ANALYTICS_PRINT_ERROR,
    // Click events
    ANALYTICS_CLICK_SEND_SINGLE,
    ANALYTICS_CLICK_SEND_MULTI,
    // File project events
    ANALYTICS_FILE_PROJECT_NEW,
    ANALYTICS_FILE_PROJECT_OPEN,
    ANALYTICS_FILE_PROJECT_SAVE,
    ANALYTICS_FILE_PROJECT_SAVE_AS,
    // File model events
    ANALYTICS_FILE_IMPORT_MODEL,
    ANALYTICS_FILE_EXPORT_MODEL,
    // File preset events
    ANALYTICS_FILE_IMPORT_PRESET,
    ANALYTICS_FILE_EXPORT_PRESET,
    // File GCode events
    ANALYTICS_FILE_EXPORT_GCODE_SINGLE,
    ANALYTICS_FILE_EXPORT_GCODE_ALL,
    // Model action events
    ANALYTICS_MODEL_BOOLEAN
};

struct AnalyticsEventPayload {
    AnalyticsDataEventType type;
    nlohmann::json data;
};

struct AnalyticsProjectInfo {
    std::string url;
    std::string file_id;
    std::string file_format;
    std::string model_id;
    std::string name;

    bool is_valid = false;
};

class AnalyticsDataUploadManager
{
public:
    static AnalyticsDataUploadManager& getInstance()
    {
        static std::unique_ptr<AnalyticsDataUploadManager> instance;
        static std::once_flag flag;
        std::call_once(flag, []() {
            instance.reset(new AnalyticsDataUploadManager());
        });
        return *instance;
    }

    ~AnalyticsDataUploadManager();

    void triggerUploadTasks(AnalyticsUploadTiming triggerTiming, const std::vector<AnalyticsDataEventType>& dataEventTypes, int plate_idx = 0, const std::string& device_mac = "");
    void triggerUploadTasksWithPayload(const AnalyticsEventPayload& payload, int plate_idx = 0, const std::string& device_mac = "");

    void mark_analytics_project_info(const std::string& full_url,
                                               const std::string& model_id,
                                               const std::string& file_id,
                                               const std::string& file_format,
                                               const std::string& name);

    void set_analytics_project_info_valid(bool valid);
    void clear_analytics_project_info();

    static void uploadSlice822ClickEvent(const std::string& module, int id=1);

    // ============================================================
    // Creality Cloud Sensors analytics reporting interface (new system - independent area)
    // Reports to a different server address than the existing Firebase Analytics
    // ============================================================

    /**
     * @brief Test connectivity to the server
     *
     * @return true if the connection succeeds
     * @note For debugging only; checks whether the Creality Cloud server can be reached
     */
    static bool test_sensors_connection();

    /**
     * @brief Send the print-begin event (print_001) to Creality Cloud
     *
     * @param data Business data JSON object (contains task_id, model_id, plate_idx, etc.)
     * @note Automatically gathers all required parameters and converts them to Sensors SDK format before sending to the corresponding server
     */
    void send_print_begin_event(const nlohmann::json& data = nlohmann::json());

    /**
     * @brief Send Sensors analytics data to the Creality Cloud server (general-purpose interface)
     *
     * @param payload Complete analytics data JSON object (prepared by the caller)
     * @note Automatically selects the correct reporting address based on version and region
     */
    void send_sensors_payload_to_creality(const nlohmann::json& payload);

    // ============================================================
    // 3MF file fingerprint management
    // ============================================================

    /**
     * @brief Compute the 3MF file fingerprint (synchronous)
     * @param file_path 3MF file path
     * @return MD5 fingerprint string (32-character hexadecimal)
     */
    std::string computeModelFingerprint(const std::string& file_path);

    /**
     * @brief Compute the 3MF file fingerprint (asynchronous)
     * @param file_path 3MF file path
     * @return future, usable to obtain the computation result
     */
    std::future<std::string> computeModelFingerprintAsync(const std::string& file_path);

    /**
     * @brief Get the cached fingerprint
     * @param file_path 3MF file path
     * @return Fingerprint string; returns an empty string if not yet computed
     */
    std::string getCachedFingerprint(const std::string& file_path);

    /**
     * @brief Clear all fingerprint caches
     */
    void clearFingerprintCache();

    // ============================================================
    // Project geometry modification tracking (blacklist method)
    // ============================================================

    /**
     * @brief Geometry modification type enum (blacklisted operations)
     */
    enum class ModelModifyType {
        REPAIR,              // repair model
        SIMPLIFY,            // simplify model
        HOLLOW,              // hollow out
        ADD_HOLE,            // drill hole
        CUT,                 // cut
        BOOLEAN,             // boolean operation
        EMBOSS,              // emboss
        VARIABLE_LAYER,      // variable layer height
        SUPPORT_PAINT,       // support painting
        ZSEAM_PAINT,         // Z-seam painting
        SPLIT_OBJECTS,       // split into objects
        SPLIT_PARTS,         // split into parts
        ADD_PART,            // add part
        DELETE_PART,         // delete part
        HEIGHT_RANGE         // height range modification
    };

    /**
     * @brief Project geometry modification tracker (global singleton)
     *
     * Features:
     * - Tracks whether the project has been essentially modified (geometry changed)
     * - Uses the blacklist method: only blacklisted operations are marked
     * - Automatically resets every time a new 3MF is imported
     * - Phase 3: caches slice info (printer_info, slice_param)
     * - Stored by plate index, each plate cached independently
     * - Includes slice parameter collection (low-intrusion design)
     */
    class ProjectModificationTracker {
    public:
        // ============================================================
        // Parameter collection feature
        // ============================================================

        // parameter type enum
        enum class ParamType {
            Float, FloatFirst, Int, IntFirst, Bool, BoolFirst,
            String, StringFirst, StringMulti, Percent, PercentFirst,
            FloatOrPercent, FloatOrPercentFirst, Enum
        };

        // parameter definition struct
        struct ParamDef {
            const char* config_key;
            const char* output_key;
            ParamType type;
        };

        // collect parameters (static method, for external callers)
        // @param config DynamicPrintConfig reference
        // @return collected parameters JSON
        static nlohmann::json collect_params(const DynamicPrintConfig& config);

        // collect object/part modification parameters (added in phase 4)
        // @param plate PartPlate pointer (used to get objects on the current plate)
        // @param plate_idx plate index (used for logging)
        // @return obj_list JSON array
        static nlohmann::json collect_obj_params(PartPlate* plate, int plate_idx);

    private:
        // internal helper functions
        static void add_param(const DynamicPrintConfig& config,
                              nlohmann::json& output,
                              const char* config_key, 
                              const char* output_key, 
                              ParamType type);
        static void collect_printer_params(const DynamicPrintConfig& config, nlohmann::json& output);
        static void collect_filament_params(const DynamicPrintConfig& config, nlohmann::json& output);
        static void collect_process_params(const DynamicPrintConfig& config, nlohmann::json& output);

        // parameter definition tables
        static const ParamDef s_printer_params[];
        static constexpr size_t s_printer_params_count = 5;
        static const ParamDef s_filament_params[];
        static constexpr size_t s_filament_params_count = 52;
        static const ParamDef s_process_params[];
        static constexpr size_t s_process_params_count = 119;

    private:
        bool m_is_modified = false;
        std::vector<ModelModifyType> m_modify_history;
        mutable std::mutex m_mutex;
        
        // Phase 3: slice info cache (stored by plate index)
        std::map<int, std::string> m_printer_info;   // key=plate index, value=JSON string
        std::map<int, std::string> m_slice_param;    // key=plate index, value=JSON string
        std::map<int, std::string> m_filament_info;  // key=plate index, value=JSON string

    public:
        static ProjectModificationTracker& getInstance();

        // Phase 2: mark as modified (called after an operation succeeds)
        void mark_modified(ModelModifyType type);

        // Phase 2: query whether it has been modified
        bool is_essentially_modified() const;

        // Phase 2+3: reset (called when importing a new 3MF, clears all state)
        void reset();

        // Phase 2: get the modification history (for debugging)
        const std::vector<ModelModifyType>& get_history() const;

        // Phase 3: cache slice info (by plate index)
        void cache_slice_info(int plate_idx,
                              const std::string& printer_info,
                              const std::string& slice_param,
                              const std::string& filament_info);

        // Phase 3: get cached slice info
        std::string get_printer_info(int plate_idx) const;
        std::string get_slice_param(int plate_idx) const;
        std::string get_filament_info(int plate_idx) const;
    };

private:
    AnalyticsDataUploadManager();
    
    AnalyticsDataUploadManager(const AnalyticsDataUploadManager&)            = delete;
    AnalyticsDataUploadManager& operator=(const AnalyticsDataUploadManager&) = delete;
    
    // Initialize environment and region configuration (executed only once, on first call)
    void init_sensors_config_if_needed();

    // cached configuration info
    bool m_sensors_config_initialized = false;
    std::string m_sensors_upload_url;

    void processUploadData(AnalyticsDataEventType dataEventType, int plate_idx, const std::string& device_mac);

    void uploadGlobalPrintParams(int plate_idx, const std::string& device_mac);
    void uploadObjectPrintParams(int plate_idx,const std::string& device_mac);
    void uploadSlicePlateEventData();
    void uploadFirstLaunchEventData();
    void uploadPreferencesChangedData();
    void uploadSoftwareLaunchData();
    void uploadSoftwareCrashData();
    void uploadSoftwareBadAlloc();
    void uploadSoftwareCloseData();
    void uploadDeviceInfoData();
    void uploadAccountDeviceInfoData();
    void uploadOnlineModelsEvent();
    void uploadPrepareEvent();
    void uploadPreviewEvent();
    void uploadDeviceEvent();
    void uploadClickHomePageProjectsEvent();
    void uploadClickHomePageOnlineParamsEvent();
    void uploadClickHomePageTutorialsEvent();
    void uploadClickHomePagePersonCenterEvent();
    void uploadClickHomePageFeedbackEvent();
    void uploadClickHomePageMakenowEvent();
    void uploadClickHomePageCrealitymallEvent();
    void uploadModelActionAddEvent();
    void uploadModelActionAddPlateEvent();
    void uploadModelActionMoveEvent();
    void uploadModelActionRotateEvent();
    void uploadModelActionAutoOrientEvent();
    void uploadModelActionArrangeAllEvent();
    void uploadModelActionLayOnFaceEvent();
    void uploadModelActionSplitToObjectsEvent();
    void uploadModelActionSplitToPartsEvent();
    void uploadModelActionScaleEvent();
    void uploadModelActionHollowEvent();
    void uploadModelActionAddHoleEvent();
    void uploadModelActionCutEvent();
    void uploadModelActionBooleanEvent();
    void uploadModelActionMeasureEvent();
    void uploadModelActionSupportPaintEvent();
    void uploadModelActionZseamPaintEvent();
    void uploadModelActionVariableLayerEvent();
    void uploadModelActionPaintEvent();
    void uploadModelActionEmbossEvent();
    void uploadModelActionAssemblyViewEvent();
    void uploadAiServiceCallEvent();

    void track_model_action(const std::string& event_name, nlohmann::json& js);
    
    // Delayed sending of print_send event to ensure frontend page is ready
    void track_model_action_delayed_print_send(const nlohmann::json& js);
    void on_delayed_print_send_timer(nlohmann::json js);

private:
    AnalyticsProjectInfo m_analytics_project_info;

    // fingerprint cache: file path -> fingerprint
    std::unordered_map<std::string, std::string> m_fingerprint_cache;
    mutable std::mutex m_fingerprint_mutex;

    // low-level MD5 computation function
    std::string computeMD5(const std::string& file_path, size_t chunk_size = 1024 * 1024);

};

} // namespace GUI
} // namespace Slic3r

#endif // ANALYTICS_DATA_UPLOAD_MANAGER_HPP
