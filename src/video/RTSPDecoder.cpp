#include "RTSPDecoder.h"
#include <iostream>

RTSPDecoder* RTSPDecoder::GetInstance() {
    static RTSPDecoder instance;
    return &instance;
}
RTSPDecoder::RTSPDecoder() {
    // Constructor implementation
}
RTSPDecoder::~RTSPDecoder() {
    // Destructor implementation
}
void RTSPDecoder::startPlay(const std::string& strUrl) {
    if(strUrl == m_url && !m_isStop) {
        std::cout << "Already playing this URL: " << strUrl << std::endl;
        return;
    }
    stopPlay();
    // Start playing the RTSP stream
    m_url = strUrl;
    m_isStop = false;
    m_frame_data.clear();
    m_playFutrue = std::async(std::launch::async, [this](){
                   avformat_network_init();
                // Open the RTSP stream
                AVFormatContext *pFormatCtx = NULL;
                if (avformat_open_input(&pFormatCtx, m_url.c_str(), NULL, NULL) != 0) {
                    std::cerr << "Couldn't open input stream." << std::endl;
                    m_isStop = true;
                    return -1;
                }
                // Find stream information
                if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
                    std::cerr << "Couldn't find stream information." << std::endl;
                    avformat_close_input(&pFormatCtx);
                    m_isStop = true;
                    return -1;
                }
                // Find the index of the video stream
                int videoStream = -1;
                for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
                    if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                        videoStream = i;
                        break;
                    }
                }
                if (videoStream == -1) {
                    std::cerr << "Didn't find a video stream." << std::endl;
                    avformat_close_input(&pFormatCtx);
                    m_isStop = true;
                    return -1;
                }
                // Get the codec parameters of the video stream
                AVCodecParameters *pCodecPar = pFormatCtx->streams[videoStream]->codecpar;
                // Find the video decoder
                const AVCodec *pCodec = avcodec_find_decoder(pCodecPar->codec_id);
                if (pCodec == NULL) {
                    std::cerr << "Codec not found." << std::endl;
                    avformat_close_input(&pFormatCtx);
                    m_isStop = true;
                    return -1;
                }
                // Allocate the decoder context
                AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
                if (avcodec_parameters_to_context(pCodecCtx, pCodecPar) < 0) {
                    std::cerr << "Could not copy codec parameters to codec context" << std::endl;
                    avformat_close_input(&pFormatCtx);
                    m_isStop = true;
                    return -1;
                }
                // Open the decoder
                if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
                    std::cerr << "Could not open codec." << std::endl;
                    avcodec_free_context(&pCodecCtx);
                    avformat_close_input(&pFormatCtx);
                    m_isStop = true;
                    return -1;
                }
                // Allocate an AVFrame
                AVFrame *pFrame = av_frame_alloc();
                if (pFrame == NULL) {
                    std::cerr << "Could not allocate video frame" << std::endl;
                    avcodec_free_context(&pCodecCtx);
                    avformat_close_input(&pFormatCtx);
                    m_isStop = true;
                    return -1;
                }
                // Allocate an AVPacket
                AVPacket *pPacket = av_packet_alloc();
                if (pPacket == NULL) {
                    std::cerr << "Could not allocate packet" << std::endl;
                    av_frame_free(&pFrame);
                    avcodec_free_context(&pCodecCtx);
                    avformat_close_input(&pFormatCtx);
                    m_isStop = true;
                    return -1;
                }
                std::vector<std::vector<uint8_t>> jpegArrays;
                // Read frames and decode them
                while (!m_isStop) {
                    int ret = av_read_frame(pFormatCtx, pPacket);
                    if (ret < 0) {
                        if (ret == AVERROR_EOF) {
                            // Stream ended normally, exit
                            break;
                        } else if (ret == AVERROR(EAGAIN) || ret == AVERROR(EINTR)) {
                            // Temporary error (no data / interrupted), retry
                            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // retry
                            continue;
                        } else {
                            // Network error (e.g. packet loss, connection dropped), log the error and retry
                            av_packet_unref(pPacket);
                            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // retry
                            continue;
                        }
                    }

                    // Process video stream packets
                    if (pPacket->stream_index == videoStream) {
                        // Send the packet to the decoder, flushing it on failure
                        if (avcodec_send_packet(pCodecCtx, pPacket) < 0) {
                            std::cerr << "Failed to send packet to decoder, flushing decoder..." << std::endl;
                            avcodec_flush_buffers(pCodecCtx); // Flush the decoder to avoid error accumulation
                        }

                        // Receive decoded frames (loop to ensure all output is received)
                        int frame_ret;
                        while ((frame_ret = avcodec_receive_frame(pCodecCtx, pFrame)) == 0) {
                            img2jpeg(pFrame); // Convert to JPEG
                        }
                    }
                    av_packet_unref(pPacket);
                }

                // Release resources
                av_frame_free(&pFrame);
                av_packet_free(&pPacket);
                avcodec_free_context(&pCodecCtx);
                avformat_close_input(&pFormatCtx);
                avformat_network_deinit();
                m_isStop = true;
                return 0;

        });
    // Initialize the player and start playback
    // This is a placeholder; actual implementation will depend on your RTSP library
}
void RTSPDecoder::img2jpeg(AVFrame *pFrame) {
        const AVCodec *pCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        if (!pCodec) {
            std::cerr << "Could not find encoder" << std::endl;
            return ;
        }
        AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
        if (!pCodecCtx) {
            std::cerr << "Could not allocate codec context" << std::endl;
            return ;
        }
        pCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
        pCodecCtx->width = pFrame->width;
        pCodecCtx->height = pFrame->height;
        pCodecCtx->time_base = {1, 25};
        if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            std::cerr << "Could not open codec" << std::endl;
            avcodec_free_context(&pCodecCtx);
            return ;
        }
        AVPacket *pPacket = av_packet_alloc();
        if (!pPacket) {
            std::cerr << "Could not allocate packet" << std::endl;
            avcodec_free_context(&pCodecCtx);
            return ;
        }
        if (avcodec_send_frame(pCodecCtx, pFrame) < 0) {
            std::cerr << "Error sending frame to encoder" << std::endl;
            av_packet_free(&pPacket);
            avcodec_free_context(&pCodecCtx);
            return ;
        }

       while (avcodec_receive_packet(pCodecCtx, pPacket) == 0) 
       {
            std::lock_guard<std::mutex> guard(frame_mutex_);
            // Drop the oldest frame when the queue is full
            if (frame_queue.size() >= MAX_QUEUE_SIZE) {
                frame_queue.pop();
            }
            // Store the new frame
            frame_queue.emplace(pPacket->data, pPacket->data + pPacket->size);
            av_packet_unref(pPacket);
       }

        av_packet_free(&pPacket);
        avcodec_free_context(&pCodecCtx);
        return ;
}

void RTSPDecoder::getFrameData(std::vector<unsigned char>& framedata)
{
    std::lock_guard<std::mutex> guard(frame_mutex_);
    framedata.clear();
    static std::vector<uint8_t> last_valid_frame;
    if (!frame_queue.empty()) {
        framedata = frame_queue.front();
        frame_queue.pop();
    } else {
        // When the queue is empty, return the backup of the previous frame
        if (!last_valid_frame.empty()) {
            framedata = last_valid_frame;
        }
    }
    // Back up the current frame, used in case of network interruption
    if (!framedata.empty()) {
        last_valid_frame = framedata;
    }
}