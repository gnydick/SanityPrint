#ifndef PLAYER_FFMPEG_RTSP_DECODER_H_
#define PLAYER_FFMPEG_RTSP_DECODER_H_
#include <string>
#include <vector>
#include <future>
#include <functional>
#include <queue>
#include <thread>
extern "C"
{
#define __STDC_CONSTANT_MACROS

#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include<libavcodec/avcodec.h>
#include<libswresample/swresample.h>
}
class RTSPDecoder  {
    public:
        static RTSPDecoder* GetInstance();
        void startPlay(const std::string& strUrl);
        void stopPlay() {
            m_isStop = true;
            if(!m_playFutrue.valid())
            {
                return;
            }
            std::future_status status = m_playFutrue.wait_for(std::chrono::seconds(2));
            if (status == std::future_status::ready)
            {
                //cout << "thread finished executing" << endl;
            }	
        }
        void getFrameData(std::vector<unsigned char>& framedata);
    private:
        RTSPDecoder();
        ~RTSPDecoder();
        void img2jpeg(AVFrame *pFrame);
        std::string m_url;
        std::vector<unsigned char> m_frame_data;
        bool m_isStop = true;
        int m_width = 1920;
        int m_height = 1080;
        std::future<int> m_playFutrue;
         std::mutex frame_mutex_;

        std::queue<std::vector<uint8_t>> frame_queue;        // Frame buffer queue
        const size_t                     MAX_QUEUE_SIZE = 3; // Buffer 3 frames (balance memory and smoothness)
        // Add other necessary members and methods for RTSP handling
};
#endif // PLAYER_FFMPEG_RTSP_DECODER_H_