#ifndef slic3r_GUI_HttpFileUploader_hpp_
#define slic3r_GUI_HttpFileUploader_hpp_
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
namespace asio = boost::asio;
using boost::asio::ip::tcp;
namespace posix_time = boost::posix_time;


// HTTP file upload class
class HttpFileUploader {
public:
    using ProgressCallback = std::function<void(double progress, std::streamsize bytes_sent, std::streamsize total_size)>;
    // Define the success callback function type
    // Parameters: server response content, file name
    using SuccessCallback = std::function<void(const std::string& response, const std::string& filename)>;

    // Define the error callback function type
    // Parameters: error message, error code (optional)
    using ErrorCallback = std::function<void(const std::string& error_msg, int error_code)>;
    // Define the cancel callback function type
    using CancelCallback = std::function<void(const std::string& filename)>;
    HttpFileUploader(asio::io_context& io_context,
                    const std::string& server,
                    const std::string& port,
                    const std::string& target,
                    const std::string& filename,
                    const std::string& field_name = "file");

    // Start the upload
    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_ || is_completed_) return;

        // Start the connect timeout timer
        start_connect_timer();
        // Resolve the server address
        resolver_.async_resolve(server_, "http",
            boost::bind(&HttpFileUploader::on_resolve, this,
                        asio::placeholders::error,
                        asio::placeholders::results));
    }

    // Set the progress callback function
    void set_progress_callback(ProgressCallback callback) {
        progress_callback_ = std::move(callback);
    }
    // Set the success callback function
    void set_success_callback(SuccessCallback callback) {
        success_callback_ = std::move(callback);
    }

    // Set the error callback function
    void set_error_callback(ErrorCallback callback) {
        error_callback_ = std::move(callback);
    }
    // Set the cancel callback function
    void set_cancel_callback(CancelCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        cancel_callback_ = std::move(callback);
    }
  // Cancel the upload operation
    void cancel() {
        asio::post(io_context_, [this]() {
            std::lock_guard<std::mutex> lock(mutex_);
            if (is_canceled_ || is_completed_) return;
            
            // Mark as canceled
            is_canceled_ = true;

            // Close the socket to cancel all asynchronous operations
            boost::system::error_code ec;
            socket_.close(ec);

            // Close the file
            if (file_.is_open()) {
                file_.close();
            }

            // Trigger the cancel callback
            if (cancel_callback_) {
                cancel_callback_("");
            } else {
                std::cout << "file upload canceled: " << std::endl;
            }

            // Stop the IO context
            io_context_.stop();
        });
    }

    // Check whether it has been canceled
    bool is_canceled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_canceled_;
    }

    // Check whether it has completed
    bool is_completed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_completed_;
    }
private:
    void start_connect_timer() {
        connect_timer_.expires_from_now(posix_time::milliseconds(connect_timeout_));
        connect_timer_.async_wait(boost::bind(&HttpFileUploader::on_connect_timeout, this,
            asio::placeholders::error));
    }
    
    // Start the send timeout timer
    void start_send_timer() {
        send_timer_.expires_from_now(posix_time::milliseconds(send_timeout_));
        send_timer_.async_wait(boost::bind(&HttpFileUploader::on_send_timeout, this,
            asio::placeholders::error));
    }

    // Cancel all timers
    void cancel_timers() {
        boost::system::error_code ec;
        connect_timer_.cancel(ec);
        send_timer_.cancel(ec);
    }
     // Connect timeout callback
    void on_connect_timeout(const boost::system::error_code& err) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (err || is_canceled_ || is_completed_ || socket_.is_open()) {
            return;
        }

        // Connection timed out
        handle_error("connect timeout(" + std::to_string(connect_timeout_) + "ms)", 1001);
    }

    // Send timeout callback
    void on_send_timeout(const boost::system::error_code& err) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (err || is_canceled_ || is_completed_) {
            return;
        }

        // Send timed out
        handle_error("send timeout(" + std::to_string(send_timeout_) + "ms)", 1002);
    }
    // Address resolution complete callback
    void on_resolve(const boost::system::error_code& err,
                   const tcp::resolver::results_type& endpoints) {
        if (err) {
            cancel_timers();
            std::string error_msg = "resolve error: " + err.message();
            handle_error(error_msg, 5);
            return;
        }

        // Connect to the server
        asio::async_connect(socket_, endpoints,
            boost::bind(&HttpFileUploader::on_connect, this,
                        asio::placeholders::error));
    }

    // Connection complete callback
    void on_connect(const boost::system::error_code& err) {
        if (is_canceled_) return;

        // Cancel the connect timer and start the send timer
        connect_timer_.cancel();
        start_send_timer();
        if (err) {
            std::string error_msg = "connect error: " + err.message();
            handle_error(error_msg, 7);
            return;
        }

        // Build the HTTP request header
        build_request_header();

        // Send the HTTP request header
        asio::async_write(socket_, request_header_,
            boost::bind(&HttpFileUploader::on_header_written, this,
                        asio::placeholders::error,
                        asio::placeholders::bytes_transferred));
    }

    // Build the HTTP request header
    void build_request_header() {
        // Compute the total content length
        std::string filename = filename_.substr(filename_.find_last_of("/\\") + 1);

        // Compute the size of the multipart header and footer
        std::stringstream header_part;
        header_part << "--" << boundary_ << "\r\n";
        header_part << "Content-Disposition: form-data; name=\"" << field_name_ << "\"; filename=\"" << filename << "\"\r\n";
        header_part << "Content-Type: application/octet-stream\r\n\r\n";
        
        std::string footer_part = "\r\n--" + boundary_ + "--\r\n";
        
        // Total content length
        // Total content length (used for progress calculation)
        total_content_length_ = header_part.str().size() + file_size_ + footer_part.size();

        // Build the HTTP request
        std::ostream request_stream(&request_header_);
        request_stream << "POST " << target_ << " HTTP/1.1\r\n";
        request_stream << "Host: " << server_ << "\r\n";
        request_stream << "User-Agent: Boost.Asio File Uploader\r\n";
        request_stream << "Content-Type: multipart/form-data; boundary=" << boundary_ << "\r\n";
        request_stream << "Content-Length: " << total_content_length_ << "\r\n";
        request_stream << "Connection: close\r\n\r\n";
        request_stream << header_part.str();
        
        // Save the footer for sending later
        footer_ = footer_part;
        header_part_ = header_part.str();
    }

    // Request header send complete callback
    void on_header_written(const boost::system::error_code& err,
                          std::size_t bytes_transferred) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_) return;
        if (err) {
            std::string error_msg = "send http header error: " + err.message();
            handle_error(error_msg, 19);
            return;
        }

        // Send the file content
        send_file_chunk();
    }

    // Send a file chunk
    void send_file_chunk() {
        if (is_canceled_) return;
        // Read a file chunk
        file_.read(buffer_, max_chunk_size);
        std::streamsize bytes_read = file_.gcount();

        if (bytes_read > 0) {
            // Send the current chunk
            asio::async_write(socket_, asio::buffer(buffer_, bytes_read),
                boost::bind(&HttpFileUploader::on_file_chunk_sent, this,
                            asio::placeholders::error,
                            asio::placeholders::bytes_transferred,
                            bytes_read));
        } else {
            // File send complete, send the footer
            send_footer();
        }
    }

    // File chunk send complete callback
    void on_file_chunk_sent(const boost::system::error_code& err,
                           std::size_t bytes_transferred,
                           std::streamsize bytes_read) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_) return;
        if (err) {
            std::string error_msg = "send file error: " + err.message();
            handle_error(error_msg, 20);
            return;
        }

       // Update the number of bytes sent
        total_sent_ += bytes_transferred;

        // Trigger the progress callback
        trigger_progress_callback();

        // Continue sending the next chunk
        send_file_chunk();
    }

    // Send the multipart footer
    void send_footer() {
        if (is_canceled_) return;
        asio::async_write(socket_, asio::buffer(footer_),
            boost::bind(&HttpFileUploader::on_footer_sent, this,
                        asio::placeholders::error,
                        asio::placeholders::bytes_transferred));
    }

    // Footer send complete callback
    void on_footer_sent(const boost::system::error_code& err,
                       std::size_t bytes_transferred) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_) return;
        if (err) {
            std::string error_msg = "send footer error: " + err.message();
            handle_error(error_msg, 23);
            return;
        }
        // Update the number of bytes sent (including the footer)
        total_sent_ += bytes_transferred;

        // Trigger the final progress callback (100%)
        trigger_progress_callback();


        // Read the server response
        asio::async_read_until(socket_, response_, "\r\n\r\n",
            boost::bind(&HttpFileUploader::on_response_header_received, this,
                        asio::placeholders::error,
                        asio::placeholders::bytes_transferred));
    }
      // Reset the send timer (called when there is a progress update)
    void reset_send_timer() {
        // Cancel the current timer and restart it
        boost::system::error_code ec;
        send_timer_.cancel(ec);
        start_send_timer();
    }
     // Trigger the progress callback
    void trigger_progress_callback() {
        reset_send_timer();
        if (progress_callback_ && total_content_length_ > 0) {
            double progress = static_cast<double>(total_sent_) / total_content_length_ * 100.0;
            progress_callback_(progress, total_sent_, total_content_length_);
        }
    }

    // Response header received callback
    void on_response_header_received(const boost::system::error_code& err,
                                    std::size_t bytes_transferred) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_) return;
        if (err) {
            std::string error_msg = "receive reponse: " + err.message();
            // Trigger the error callback
            handle_error(error_msg, err.value());
            return;
        }

        // Parse the response header
        std::istream response_stream(&response_);
        std::string http_version;
        response_stream >> http_version;
        unsigned int status_code;
        response_stream >> status_code;
        std::string status_message;
        std::getline(response_stream, status_message);

        // Read the rest of the response header
        std::string header;
        while (std::getline(response_stream, header) && header != "\r") {}

        // Read the response body
        std::string response_body;
        if (response_.size() > 0) {
            std::stringstream ss;
            ss << &response_;
            response_body = ss.str();
        }

        // Check whether there is more response body data
        if (status_code == 200) {
            // Read the complete response body
            asio::async_read(socket_, response_,
                asio::transfer_all(),
                boost::bind(&HttpFileUploader::on_response_body_complete, this,
                            asio::placeholders::error,
                            asio::placeholders::bytes_transferred,
                            status_code,
                            response_body));
        } else {
            // Non-200 status code, handle the error
            std::string error_msg = "Upload failed: " + std::to_string(status_code) + "msg: " + status_message;
            // Trigger the error callback
            handle_error(error_msg, status_code);

        }
    }

    // Response body fully received callback
    void on_response_body_complete(const boost::system::error_code& err,
                                  std::size_t bytes_transferred,
                                  unsigned int status_code,
                                  std::string response_body) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_) return;
        
        // Cancel all timers
        cancel_timers();
        // Merge the response body already read
        if (bytes_transferred > 0) {
            std::stringstream ss;
            ss << &response_;
            response_body += ss.str();
        }

        if (status_code == 200) {
            std::cout << "Upload sucess!" << std::endl;
            is_completed_ = true;
            // Trigger the success callback
            if (success_callback_) {
                success_callback_(response_body, "");
            }
        } else {
            std::string error_msg = "Upload failed: " + std::to_string(status_code);
            // Trigger the error callback
            handle_error(error_msg, status_code);

        }

        io_context_.stop();
    }
    // Handle errors
    void handle_error(const std::string& error_msg, int error_code) {
        if (is_canceled_) return;

        std::cerr << error_msg << std::endl;
        is_completed_ = true;

        // Cancel all timers
        cancel_timers();

        // Trigger the error callback
        if (error_callback_) {
            error_callback_(error_msg, error_code);
        }

        // Close the socket
        boost::system::error_code ec;
        socket_.close(ec);

        // Close the file
        if (file_.is_open()) {
            file_.close();
        }

        io_context_.stop();
    }

    tcp::resolver resolver_;
    tcp::socket socket_;
    asio::deadline_timer connect_timer_;  // Connect timeout timer
    asio::deadline_timer send_timer_;     // Send timeout timer
    asio::streambuf request_header_;
    asio::streambuf response_;
    std::ifstream file_;
    std::string server_;
    std::string target_;
    std::string filename_;
    std::string field_name_;
    std::string boundary_;
    std::string header_part_;
    std::string footer_;
    std::streamsize file_size_ = 0;
    std::streamsize total_sent_ = 0;
    asio::io_context& io_context_;  // Reference to the IO context, used to control exit
    ProgressCallback progress_callback_;
    SuccessCallback success_callback_;    // Success callback function
    ErrorCallback error_callback_;        // Error callback function
    CancelCallback cancel_callback_;      // Cancel callback function
    std::streamsize total_content_length_ = 0;
    mutable std::mutex mutex_;            // Thread-safe mutex
    bool is_canceled_;                    // Canceled state flag
    bool is_completed_;
    int connect_timeout_;                 // Connect timeout (milliseconds)
    int send_timeout_;                    // Send timeout (milliseconds)
    // Chunk size set to 8KB
    enum { max_chunk_size = 65536 };
    char buffer_[max_chunk_size];
};
#endif