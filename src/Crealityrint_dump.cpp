
#include "Crealityrint_dump.hpp"
// 自定义对话框类




static const char *payload_text = 
  "To: " DUMPTOOL_TO "\\r\
"
  "From: " DUMPTOOL_TO " (Sender Name)\\r\
"
  "Subject: Error Report\\r\
"
  "\\r\
"
  "Dump.\\r\
";

struct upload_status {
  size_t bytes_read;
  FILE *attachment;
};

static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp) {
  struct upload_status *upload_ctx = (struct upload_status *)userp;
  const char *data;

  if(size * nmemb < 1)
    return 0;

  if(upload_ctx->bytes_read < strlen(payload_text)) {
    data = payload_text + upload_ctx->bytes_read;
    size_t room = strlen(payload_text) - upload_ctx->bytes_read;
    memcpy(ptr, data, room);
    upload_ctx->bytes_read += room;
    return room;
  }

  size_t chunk_size = fread(ptr, 1, size * nmemb, upload_ctx->attachment);
  return chunk_size;
};

void ErrorReportDialog::sendEmail(wxString zipFilePath) {
        // 发送邮件
        CURL *curl;
        CURLcode res = CURLE_OK;
        struct curl_slist *recipients = NULL;
        struct upload_status upload_ctx = { 0 };
        upload_ctx.attachment = fopen(zipFilePath.ToStdString().c_str(), "rb");

        curl = curl_easy_init();
        curl_version_info_data* ver = curl_version_info(CURLVERSION_NOW);
        for (int i = 0; i < sizeof(ver->protocols); ++i) {
            std::cout << ver->protocols[i] << std::endl;
        }
         if(curl) {
            curl_easy_setopt(curl, CURLOPT_URL, DUMPTOOL_HOST);
            curl_easy_setopt(curl, CURLOPT_USERNAME, DUMPTOOL_USER);
            curl_easy_setopt(curl, CURLOPT_PASSWORD, DUMPTOOL_PASS);

            curl_easy_setopt(curl, CURLOPT_MAIL_FROM, DUMPTOOL_TO);
            struct curl_slist *recipients = NULL;
            recipients = curl_slist_append(recipients, DUMPTOOL_TO);
            curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

            curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
            curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // 连接超时5秒
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);       // 数据传输超时15秒
            curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);

            res = curl_easy_perform(curl);

            if(res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s", curl_easy_strerror(res));

            curl_slist_free_all(recipients);
            curl_easy_cleanup(curl);
            fclose(upload_ctx.attachment);
        }

        return ;
    }
    wxString ErrorReportDialog::zipFiles() {
        // 创建一个zip文件
        wxString format1 = "%Y%m%d%H%M%S";
        wxString zipFilePath = wxString::Format("%s/SanityPrint_%s_%s.zip",wxFileName::GetTempDir(),SANITYPRINT_VERSION,wxDateTime::Now().Format(format1));
        mz_zip_archive archive;
        mz_zip_zero_struct(&archive);
        mz_bool status = mz_zip_writer_init_file(&archive, zipFilePath.mb_str(), 0);
        if (!status) {
            std::cerr << "Failed to create zip file!" << std::endl;
            return "";
        }
        // 添加文件到zip文件
        wxFileName fileName(m_dumpFilePath);
        wxString nameWithExt = fileName.GetFullName();
        status = mz_zip_writer_add_file(&archive, nameWithExt.mb_str(), m_dumpFilePath.mb_str(), "", 0, MZ_BEST_COMPRESSION);
        if (!status) {
            std::cerr << "Failed to add file to zip!" << std::endl;
            mz_zip_writer_end(&archive);
            return "";
        }
         status = mz_zip_writer_add_file(&archive, "system_info.json", m_systemInfoFilePath.mb_str(), "", 0, MZ_BEST_COMPRESSION);
        if (!status) {
            std::cerr << "Failed to add file to zip!" << std::endl;
            mz_zip_writer_end(&archive);
            return "";
        }

        status = mz_zip_writer_finalize_archive(&archive);
        if (MZ_FALSE == status) {
            mz_zip_writer_end(&archive);
            return "";
        }
        // 关闭zip文件
        mz_zip_writer_end(&archive);
        return zipFilePath;
    }

    void ErrorReportDialog::sendReport()
    {
         m_systemInfoFilePath = getSystemInfo();
        if(!m_dumpFilePath.IsEmpty() && !m_systemInfoFilePath.IsEmpty()) {
            wxString dumpfile = zipFiles();
            if(dumpfile.IsEmpty()) {
                    return ;
            }
            sendEmail(dumpfile);
        }
    }
// 自定义应用程序类
