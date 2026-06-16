#ifndef ACCOUNT_DEVICE_MANAGER_hpp_
#define ACCOUNT_DEVICE_MANAGER_hpp_
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>


class AccountDeviceMgr
{
public:
    struct DeviceInfo
    {
        std::string device_unique_id;    // device id (possibly the MAC address)
        std::string address;             // IP address
        int         connectType = 3;         // connection type
        std::string mac;                 // MAC address
        std::string model;               // printer model
        std::string name;                // printer name
        std::string group;               // printer group
        // other device info
    };

    struct AccountInfo
    {
        std::string             account_id; // account id
        std::vector<DeviceInfo> my_devices;
        // other account info
    };

    struct AccountDeviceInfo
    {
        std::unordered_map<std::string, AccountInfo> account_infos;
    };

    static AccountDeviceMgr& getInstance()
    {
        std::call_once(flag, []() { instance.reset(new AccountDeviceMgr()); });
        return *instance;
    }

    static void destroyInstance() { instance.reset(); }

    static std::mutex& getFileMutex() { return file_mutex; }

    ~AccountDeviceMgr();

public:
    void unbind_device(const std::string& device_id);
    void unbind_device_by_address(const std::string& address);
    void unbind_device_by_group(const std::string& group);
    void add_to_my_devices(const DeviceInfo& device_info);
    void load();
    std::string get_account_device_bind_json_info();
    void        reset_account_device_file_id(const std::string& fid);
    std::string get_account_device_info_for_printers_init();
    const AccountDeviceInfo& get_account_device_info() { return accountDeviceInfos;}

private:
    AccountDeviceMgr();

    AccountDeviceMgr(const AccountDeviceMgr&)            = delete;
    AccountDeviceMgr& operator=(const AccountDeviceMgr&) = delete;

    void clear_all_account_info();
    void add_device_to_account(AccountInfo& account, const DeviceInfo& device_info);
    void save_to_file();

    void        sync_to_cloud();
    std::string get_local_device_dir();
    std::string account_device_json_file();

    AccountDeviceInfo accountDeviceInfos;

    static std::unique_ptr<AccountDeviceMgr> instance;
    static std::once_flag            flag;

    std::string m_account_device_file_id = "";   //parameter package unique id

    static std::mutex file_mutex; // static mutex
};

#endif /* ACCOUNT_DEVICE_MANAGER_hpp_ */
