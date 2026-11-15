#include "WebRTCDecoder.h"
#include <cstring>
#include <thread>
#include <yangstream/YangSynBuffer.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <jpeglib.h>
WebRTCDecoder* WebRTCDecoder::g_pSingleton = new (std::nothrow) WebRTCDecoder();
WebRTCDecoder::WebRTCDecoder()
{
    yang_setCLogLevel(1);
    m_context = new YangContext();
    m_context->init();

    m_context->synMgr.session->playBuffer = (YangSynBuffer*)yang_calloc(sizeof(YangSynBuffer), 1);//new YangSynBuffer();
    yang_create_synBuffer(m_context->synMgr.session->playBuffer);

    m_context->avinfo.sys.mediaServer = Yang_Server_P2p;//Yang_Server_Srs/Yang_Server_Zlm
    m_context->avinfo.rtc.rtcSocketProtocol = Yang_Socket_Protocol_Udp;//

    m_context->avinfo.rtc.rtcLocalPort = 10000 + yang_random() % 15000;
    memset(m_context->avinfo.rtc.localIp, 0, sizeof(m_context->avinfo.rtc.localIp));
    yang_getLocalInfo(m_context->avinfo.sys.familyType, m_context->avinfo.rtc.localIp);
    m_context->avinfo.rtc.enableDatachannel = yangfalse;
    m_context->avinfo.rtc.iceCandidateType = YangIceHost;
    m_context->avinfo.rtc.turnSocketProtocol = Yang_Socket_Protocol_Udp;

    m_context->avinfo.rtc.enableAudioBuffer = yangtrue; //use audio buffer
    m_context->avinfo.audio.enableAudioFec = yangfalse; //srs not use audio fec
    m_player = YangPlayerHandle::createPlayerHandle(m_context, this);
}
WebRTCDecoder::~WebRTCDecoder()
{
    //stopplay();
}
WebRTCDecoder* WebRTCDecoder::GetInstance()
{
    return g_pSingleton;
}
void WebRTCDecoder::stopPlay()
{
    m_isStop = true;
    std::future_status status = m_playFutrue.wait_for(std::chrono::seconds(2));
    if (status == std::future_status::ready)
	{
		//cout << "thread finished executing" << endl;
	}
    if (m_player) m_player->stopPlay();

}

void WebRTCDecoder::startPlay(const std::string& strUrl)
{
    std::lock_guard<std::mutex> guard(this->frame_mutex_); 
    int waitCount = 100;
    while(m_status == CONNECTTING)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        waitCount--;
        if(waitCount<=0)
        {
            break;
        }
        }
    
    if(m_status == CONNECTED&& strUrl==m_url)
        return;
    std::cout << "connected!"<<m_status<<":"<<strUrl<<":"<<m_url<<"\r\n";
    
    if(m_status == CONNECTED)
    {
        this->stopPlay();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }
    m_status = CONNECTTING;
    m_url = strUrl;
    m_isStop = false;
    //m_recevie_frame_callback = recevie_frame;
    //QString url = "http://172.23.208.238:8000/call/demo";
    m_context->synMgr.session->playBuffer->resetVideoClock(m_context->synMgr.session->playBuffer->session);
    int32_t err = m_player->playRtc(0, const_cast<char*>(m_url.c_str()));
    std::cout << "connected!"<<err<<"\r\n";
    if (!err)
    {
        m_playFutrue = std::async(std::launch::async, [this](){
            while(!this->m_isStop)
            {
                this->receiveFrame();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }

        });
    }
}


int WebRTCDecoder::width() 
{
    return m_width;
}
int WebRTCDecoder::height(){
    return m_height;
    }
void YUV420P_to_RGB24(const unsigned char* yuv, unsigned char* rgb, int width, int height) {
    const unsigned char* y_plane = yuv;
    const unsigned char* u_plane = yuv + width * height;
    const unsigned char* v_plane = yuv + width * height + (width / 2) * (height / 2);
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int yy = y_plane[y * width + x];
            int uu = u_plane[(y / 2) * (width / 2) + (x / 2)];
            int vv = v_plane[(y / 2) * (width / 2) + (x / 2)];
            
            // YUV to RGB conversion formulas
            int r = yy + 1.402 * (vv - 128);
            int g = yy - 0.34414 * (uu - 128) - 0.71414 * (vv - 128);
            int b = yy + 1.772 * (uu - 128);
            
            // Clamp values to [0, 255]
            r = std::max(0, std::min(255, r));
            g = std::max(0, std::min(255, g));
            b = std::max(0, std::min(255, b));
            
            rgb[(y * width + x) * 3 + 0] = r;
            rgb[(y * width + x) * 3 + 1] = g;
            rgb[(y * width + x) * 3 + 2] = b;
        }
    }
}
    /**
 * Compress RGB data into JPEG and store it in memory
 *
 * @param rgb_data Input RGB data (in the format R,G,B,R,G,B,...)
 * @param width Image width
 * @param height Image height
 * @param quality JPEG quality (1-100)
 * @param[out] out_buffer Output JPEG data
 * @param[out] out_size Output JPEG data size
 *
 * @return Returns true on success, false on failure
 */
bool rgb_to_jpeg(const unsigned char* rgb_data, int width, int height, int quality,
    std::vector<unsigned char>& out_buffer) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    // Initialize the JPEG compression object
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    // Set the memory destination
    unsigned char* buffer = nullptr;
    unsigned long buffer_size = 0;
    jpeg_mem_dest(&cinfo, &buffer, &buffer_size);

    // Set the image parameters
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;  // RGB has 3 components
    cinfo.in_color_space = JCS_RGB;

    // Set the default parameters
    jpeg_set_defaults(&cinfo);

    // Set the quality
    jpeg_set_quality(&cinfo, quality, TRUE);

    // Start compression
    jpeg_start_compress(&cinfo, TRUE);

    // Write data row by row
    JSAMPROW row_pointer[1];
    int row_stride = width * 3;  // RGB has width*3 bytes per row

    while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer[0] = (JSAMPROW)&rgb_data[cinfo.next_scanline * row_stride];
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
}

// Finish compression
jpeg_finish_compress(&cinfo);

// Copy the data into the output buffer
out_buffer.assign(buffer, buffer + buffer_size);

// Clean up
jpeg_destroy_compress(&cinfo);
free(buffer);

return true;
}
std::vector<unsigned char>& WebRTCDecoder::getFrameData()
{
    
    if(m_status!=CONNECTED)
            return m_frame_data;
    std::lock_guard<std::mutex> guard(this->frame_mutex_); 
    return m_frame_data;
}
void WebRTCDecoder::receiveFrame(){
     //uint8_t* t_vb = m_context->synMgr.session->playBuffer->getVideoRef(m_context->synMgr.session->playBuffer->session, &m_frame);
    YangVideoBuffer*  vb = m_player->getVideoBuffer();
    if(vb == nullptr)
        return;
    uint8_t* t_vb = vb->getVideoRef(&m_frame);
    //std::cout << "receive"<< "\r\n";
    if (t_vb)
    {
        this->frame_mutex_.lock();
        std::vector<unsigned char> frame_data;
        m_width = vb->m_width;//sync_buffer->width(sync_buffer->session);
        m_height = vb->m_height;//sync_buffer->height(sync_buffer->session);
        
        if(m_width<=0)
        {
            return;
        }
        std::vector<unsigned char> rgbData(m_width * m_height * 3); 
        std::vector<unsigned char> yuvData(m_width * m_height*3/2); 
        std::copy(t_vb,t_vb+yuvData.size(),yuvData.begin());
        //memncpy((void *)yuvData.data(),(void *)t_vb,yuvData.size());
        YUV420P_to_RGB24(yuvData.data(), rgbData.data(), m_width, m_height);
        int quality = 90;  // JPEG quality
    
        if (rgb_to_jpeg(rgbData.data(), m_width, m_height, quality, frame_data)) {
            
            m_frame_data = frame_data;
            
        }
        this->frame_mutex_.unlock();
        
    }
    
}
void WebRTCDecoder::success()
{
    m_status = CONNECTED;
    
}
void WebRTCDecoder::failure(int32_t errcode)
{
    m_status = STOPPED;
    //emit RtcConnectFailure(errcode);
}
