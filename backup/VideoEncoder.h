//
// Created by Lucas on 2023/5/16.
//

#ifndef CAMERACOLLECTION_VIDEOENCODER_H
#define CAMERACOLLECTION_VIDEOENCODER_H

/*
1. 初始化上下文 ctx,设置像素格式,内存类型等。
2. 打开输入文件和输出文件。
3. 打开视频编码设备,查询其功能和格式。
4. 设置捕获平面(capture plane)和输出平面(output plane)的格式。
5. 在输出平面请求缓冲区,查询它们并映射到内存。
6. 在捕获平面请求缓冲区,查询它们并映射到内存。
7. 订阅 EOS 事件,用于捕获输出平面排空的缓冲区。
8. 启动输出平面和捕获平面的流。
9. 创建一个线程从捕获平面取出缓冲区。
10. 将所有空的缓冲区入队到捕获平面。
11. 从输入文件读取视频帧,并将填充的缓冲区入队到输出平面。直到文件读取完成。
12. 在输出平面进行出队入队的循环,进行视频编码。
13. 等到捕获平面所有的缓冲区出队后退出。
14. 清理并退出。
*/
#define ENCODER_DEV "/dev/nvhost-msenc"
#define MAX_PLANES 3

#include "Buffer.h"
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>
#include <thread>

class VideoEncoder {
public:
    VideoEncoder();

    void Init();

    void SetCapturePlaneFormat();

    void SetOutputPlaneFormat();

    void PrepareBuffers();

    void RequestCapturePlaneBuffers();

    void CaptureBuffersSetup();

    void RequestOutputPlaneBuffers();

    void OutplaneBuffersSetup();

    void EnqueueEmptyBufferInfo();

    void DequeueEmptyBufferInfo();

    void EnqueueBufferInfo(Buffer &buffer);

    Buffer DequeueBufferInfo();

    void Encode(const Buffer &buffer);

    Buffer CapturePlaneDequeue();

    void Open();

    void Close();

    void Start();

    void Stop();


    int q_buffer(struct v4l2_buffer &v4l2_buf, Buffer *buffer);

    int dq_buffer(struct v4l2_buffer &v4l2_buf, Buffer **buffer);

    void OutputPlaneThread();

private:
    struct v4l2_capability encoder_caps_;
    struct v4l2_buffer outplane_v4l2_buf_;
    struct v4l2_plane outputplanes_[MAX_PLANES];
    struct v4l2_exportbuffer outplane_expbuf_;
    struct v4l2_buffer capplane_v4l2_buf_;
    struct v4l2_plane captureplanes_[MAX_PLANES];
    struct v4l2_exportbuffer capplane_expbuf_;

    uint32_t encode_pixfmt_{V4L2_PIX_FMT_H264};
//    uint32_t raw_pixfmt_{V4L2_PIX_FMT_YUV420M};
    uint32_t raw_pixfmt_{V4L2_PIX_FMT_ARGB32};
    uint32_t width_{1920};
    uint32_t height_{1080};
    uint32_t capplane_num_planes_;
    uint32_t outplane_num_planes_;
    uint32_t capplane_num_buffers_;
    uint32_t outplane_num_buffers_;

    uint32_t num_queued_outplane_buffers_{0};
    uint32_t num_queued_capplane_buffers_{0};

    enum v4l2_memory outplane_mem_type_{V4L2_MEMORY_MMAP};
    enum v4l2_memory capplane_mem_type_{V4L2_MEMORY_MMAP};
    enum v4l2_buf_type outplane_buf_type_{V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};
    enum v4l2_buf_type capplane_buf_type_{V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};

    std::vector <Buffer> capplane_buffers_;
    std::vector <Buffer> outplane_buffers_;

    Buffer::BufferPlaneFormat outplane_planefmts_[MAX_PLANES];
    Buffer::BufferPlaneFormat capplane_planefmts_[MAX_PLANES];

    int encoder_fd_{-1};
    uint32_t encoded_stream_size_MB_{2 * 1024 * 1024}; // 2MB
    uint32_t sizeimage_ = encoded_stream_size_MB_;
    uint32_t requestbuffers_count_{6};

    bool outplane_streaming_on_;
    bool capplane_streaming_on_;

    std::mutex mutex_;
    std::condition_variable cond_;

    std::thread dequeue_thread_;

    bool is_running_{false};
};


#endif //CAMERACOLLECTION_VIDEOENCODER_H
