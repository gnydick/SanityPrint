#include "slic3r/GUI/FileUploader.hpp"
// Generate a random multipart boundary
std::string generate_boundary() {
    static const std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string boundary;

    // Generate a 20-character random boundary
    for (int i = 0; i < 20; ++i) {
        boundary += chars[rand() % chars.size()];
    }
    
    return "---------------------------" + boundary;
}

// Get the file size
std::streamsize get_file_size(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("can't open file: " + filename);
    }
    return file.tellg();
}
HttpFileUploader::HttpFileUploader(asio::io_context& io_context,
                    const std::string& server,
                    const std::string& port,
                    const std::string& target,
                    const std::string& filename,
                    const std::string& field_name)
        : resolver_(io_context),
          socket_(io_context),
          connect_timer_(io_context),
          send_timer_(io_context),
          server_(server),
          target_(target),
          filename_(filename),
          field_name_(field_name),
          boundary_(generate_boundary()),
          io_context_(io_context),
          is_canceled_(false),
          is_completed_(false),
          progress_callback_(nullptr),
          success_callback_(nullptr),
          error_callback_(nullptr),
          cancel_callback_(nullptr),
          connect_timeout_(5000),  // Default connect timeout 5 seconds
          send_timeout_(60000)     // Default send timeout 30 seconds
        {
        // Open the file
        file_.open(filename, std::ios::binary);
        if (!file_.is_open()) {
            throw std::runtime_error("Unable to open file: " + filename);
        }

        // Get the file size
        file_size_ = get_file_size(filename);
    }