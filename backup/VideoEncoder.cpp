//
// Created by Lucas on 2023/5/16.
//

#include <iostream>
#include <cstdint>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <glog/logging.h>
#include <libv4l2.h>

#include "VideoEncoder.h"

VideoEncoder::VideoEncoder() {
}


void VideoEncoder::Init() {
    Open();
    SetCapturePlaneFormat();
    SetOutputPlaneFormat();
    PrepareBuffers();
    Start();

}

void VideoEncoder::Open() {
    encoder_fd_ = v4l2_open(ENCODER_DEV, O_RDWR);
    if (encoder_fd_ < 0) {
        LOG(ERROR) << "Failed to open encoder device: " << ENCODER_DEV;
        exit(-1);
    }
    // query capabilities
    struct v4l2_capability caps = {0};
    if (v4l2_ioctl(encoder_fd_, VIDIOC_QUERYCAP, &caps) < 0) {
        LOG(ERROR) << "Failed to query encoder capabilities";
        exit(-1);
    }
    if (!(caps.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)) {
        LOG(ERROR) << "Encoder does not support V4L2_CAP_VIDEO_M2M_MPLANE";
        exit(-1);
    }
}

void VideoEncoder::PrepareBuffers() {
    RequestCapturePlaneBuffers();
    CaptureBuffersSetup();

    RequestOutputPlaneBuffers();
    OutplaneBuffersSetup();
}


void VideoEncoder::SetCapturePlaneFormat() {
    /* It is necessary to set capture plane
    ** format before the output plane format
    ** along with the frame width and height.
    ** The format of the encoded bitstream is set.
    */
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.pixelformat = encode_pixfmt_;
    fmt.fmt.pix_mp.width = width_;
    fmt.fmt.pix_mp.height = height_;
    fmt.fmt.pix_mp.num_planes = 1;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = encoded_stream_size_MB_;

    if (v4l2_ioctl(encoder_fd_, VIDIOC_S_FMT, &fmt) < 0) {
        LOG(ERROR) << "Failed to set capture plane format";
        exit(-1);
    }

    // init
    capplane_num_planes_ = fmt.fmt.pix_mp.num_planes;

    for (uint32_t i = 0; i < capplane_num_planes_; ++i) {
        capplane_planefmts_[i].stride = fmt.fmt.pix_mp.plane_fmt[i].bytesperline;
        capplane_planefmts_[i].sizeimage = fmt.fmt.pix_mp.plane_fmt[i].sizeimage;
    }
}

void VideoEncoder::SetOutputPlaneFormat() {
    struct v4l2_format fmt = {0};

    uint32_t num_bufferplanes;
    Buffer::BufferPlaneFormat planefmts[MAX_PLANES];

    if (raw_pixfmt_ != V4L2_PIX_FMT_YUV420M) {
        LOG(ERROR) << "Raw pixel format is not V4L2_PIX_FMT_YUV420M";
        exit(-1);
    }
    // todo rewrite Buffer class
    Buffer::fill_buffer_plane_format(&num_bufferplanes, planefmts,
                                     width_, height_, raw_pixfmt_);
    outplane_num_planes_ = num_bufferplanes;
    for (uint32_t i = 0; i < num_bufferplanes; ++i) {
        outplane_planefmts_[i] = planefmts[i];
    }

    fmt.type = outplane_buf_type_;
    fmt.fmt.pix_mp.pixelformat = raw_pixfmt_;
    fmt.fmt.pix_mp.width = width_;
    fmt.fmt.pix_mp.height = height_;
    fmt.fmt.pix_mp.num_planes = num_bufferplanes;
    if (v4l2_ioctl(encoder_fd_, VIDIOC_S_FMT, &fmt) < 0) {
        LOG(ERROR) << "Failed to set output plane format";
        exit(-1);
    }

    // init
    outplane_num_planes_ = fmt.fmt.pix_mp.num_planes;
    for (uint32_t i = 0; i < outplane_num_planes_; ++i) {
        outplane_planefmts_[i].stride = fmt.fmt.pix_mp.plane_fmt[i].bytesperline;
        outplane_planefmts_[i].sizeimage = fmt.fmt.pix_mp.plane_fmt[i].sizeimage;
    }
}

void VideoEncoder::RequestOutputPlaneBuffers() {
    struct v4l2_requestbuffers reqbuf = {0};
    // todo check if it is necessary to set count to 10 as in the example
    reqbuf.count = requestbuffers_count_;
    reqbuf.type = outplane_buf_type_;
    reqbuf.memory = outplane_mem_type_;

    if (v4l2_ioctl(encoder_fd_, VIDIOC_REQBUFS, &reqbuf) < 0) {
        LOG(ERROR) << "Failed to request output plane buffers";
        exit(-1);
    }
    // use count to create new buffers
    if (reqbuf.count) {
        outplane_num_buffers_ = reqbuf.count;
        outplane_buffers_.resize(reqbuf.count);
        for (uint32_t i = 0; i < reqbuf.count; ++i) {
            outplane_buffers_[i] = Buffer(outplane_buf_type_, outplane_mem_type_, outplane_num_planes_,
                                          outplane_planefmts_, i);
        }
    }

}

// Query status of output plane buffers and export them for userspace mapping
void VideoEncoder::OutplaneBuffersSetup() {
    LOG(INFO) << "Setting up output plane buffers";
    for (uint32_t i = 0; i < outplane_num_buffers_; ++i) {
        struct v4l2_buffer outplane_v4l2_buf = {0};
        struct v4l2_plane outputplanes[MAX_PLANES] = {0};
        struct v4l2_exportbuffer outplane_expbuf = {0};

        outplane_v4l2_buf.index = i;
        outplane_v4l2_buf.type = outplane_buf_type_;
        outplane_v4l2_buf.memory = outplane_mem_type_;
        outplane_v4l2_buf.m.planes = outputplanes;
        outplane_v4l2_buf.length = outplane_num_planes_;

        if (v4l2_ioctl(encoder_fd_, VIDIOC_QUERYBUF, &outplane_v4l2_buf) < 0) {
            LOG(ERROR) << "Failed to query output plane buffers";
            exit(-1);
        }

        for (uint32_t j = 0; j < outplane_v4l2_buf.length; ++j) {
            outplane_buffers_[i].planes[j].length =
                    outplane_v4l2_buf.m.planes[j].length;
            outplane_buffers_[i].planes[j].mem_offset =
                    outplane_v4l2_buf.m.planes[j].m.mem_offset;
        }

        outplane_expbuf.type = outplane_buf_type_;
        outplane_expbuf.index = i;

        for (uint32_t j = 0; j < outplane_num_planes_; ++j) {
            outplane_expbuf.plane = j;
            if (v4l2_ioctl(encoder_fd_, VIDIOC_EXPBUF, &outplane_expbuf) < 0) {
                LOG(ERROR) << "Failed to export output plane buffers";
                exit(-1);
            }
            outplane_buffers_[i].planes[j].fd = outplane_expbuf.fd;
        }

        if (outplane_buffers_[i].map() != 0) {
            LOG(ERROR) << "Failed to map output plane buffers";
            exit(-1);
        }

    }
}

void VideoEncoder::RequestCapturePlaneBuffers() {
    struct v4l2_requestbuffers reqbuf = {0};
    reqbuf.count = requestbuffers_count_;
    reqbuf.type = capplane_buf_type_;
    reqbuf.memory = capplane_mem_type_;

    if (v4l2_ioctl(encoder_fd_, VIDIOC_REQBUFS, &reqbuf) < 0) {
        LOG(ERROR) << "Failed to request capture plane buffers";
        exit(-1);
    }
    // use count to create new buffers
    if (reqbuf.count) {
        capplane_num_buffers_ = reqbuf.count;
        capplane_buffers_.resize(reqbuf.count);
        for (uint32_t i = 0; i < reqbuf.count; ++i) {
            capplane_buffers_[i] = Buffer(capplane_buf_type_, capplane_mem_type_, capplane_num_planes_,
                                          capplane_planefmts_, i);
        }
    }
}

// Query status of output plane buffers and export them for userspace mapping
void VideoEncoder::CaptureBuffersSetup() {
    for (uint32_t i = 0; i < capplane_num_buffers_; ++i) {
        struct v4l2_buffer capplane_v4l2_buf = {0};
        struct v4l2_plane captureplanes[MAX_PLANES] = {0};
        struct v4l2_exportbuffer capplane_expbuf = {0};

        capplane_v4l2_buf.index = i;
        capplane_v4l2_buf.type = capplane_buf_type_;
        capplane_v4l2_buf.memory = capplane_mem_type_;
        capplane_v4l2_buf.m.planes = captureplanes;
        capplane_v4l2_buf.length = capplane_num_planes_;

        if (v4l2_ioctl(encoder_fd_, VIDIOC_QUERYBUF, &capplane_v4l2_buf) < 0) {
            LOG(ERROR) << "Failed to query capture plane buffers";
            exit(-1);
        }

        for (uint32_t j = 0; j < capplane_v4l2_buf.length; ++j) {
            capplane_buffers_[i].planes[j].length =
                    capplane_v4l2_buf.m.planes[j].length;
            capplane_buffers_[i].planes[j].mem_offset =
                    capplane_v4l2_buf.m.planes[j].m.mem_offset;
        }

        capplane_expbuf.type = capplane_buf_type_;
        capplane_expbuf.index = i;

        for (uint32_t j = 0; j < capplane_num_planes_; ++j) {
            capplane_expbuf.plane = j;
            if (v4l2_ioctl(encoder_fd_, VIDIOC_EXPBUF, &capplane_expbuf) < 0) {
                LOG(ERROR) << "Failed to export capture plane buffers";
                exit(-1);
            }
            capplane_buffers_[i].planes[j].fd = capplane_expbuf.fd;
        }

        if (capplane_buffers_[i].map() != 0) {
            LOG(ERROR) << "Failed to map capture plane buffers";
            exit(-1);
        }

    }
}

void VideoEncoder::Start() {
    // Stream on capture plane
    enum v4l2_buf_type type = capplane_buf_type_;
    if (v4l2_ioctl(encoder_fd_, VIDIOC_STREAMON, &type) < 0) {
        LOG(ERROR) << "Failed to stream on capture plane";
        exit(-1);
    }
    outplane_streaming_on_ = true;
    // Stream on output plane
    type = outplane_buf_type_;
    if (v4l2_ioctl(encoder_fd_, VIDIOC_STREAMON, &type) < 0) {
        LOG(ERROR) << "Failed to stream on output plane";
        exit(-1);
    }
    capplane_streaming_on_ = true;
}

void VideoEncoder::Stop() {
// Stream off capture plane
    enum v4l2_buf_type type = capplane_buf_type_;
    if (v4l2_ioctl(encoder_fd_, VIDIOC_STREAMOFF, &type) < 0) {
        LOG(ERROR) << "Failed to stream off capture plane";
        exit(-1);
    }
    capplane_streaming_on_ = false;
    // Stream off output plane
    type = outplane_buf_type_;
    if (v4l2_ioctl(encoder_fd_, VIDIOC_STREAMOFF, &type) < 0) {
        LOG(ERROR) << "Failed to stream off output plane";
        exit(-1);
    }
    outplane_streaming_on_ = false;

    while (is_running_) {
        OutplaneBuffersSetup();
    }
}

int
VideoEncoder::q_buffer(struct v4l2_buffer &v4l2_buf, Buffer *buffer) {
    LOG(INFO) << "VideoEncoder::q_buffer";
    int ret_val;
    uint32_t j;

    std::unique_lock <std::mutex> lock(mutex_);
    enum v4l2_memory memory_type = V4L2_MEMORY_MMAP;
    switch (memory_type) {
        case V4L2_MEMORY_MMAP:
            LOG(INFO) << "buffer->planes[j].bytesused = "
                      << buffer->planes[j].bytesused;
            for (j = 0; j < buffer->n_planes; ++j) {
                v4l2_buf.m.planes[j].bytesused =
                        buffer->planes[j].bytesused;
            }
            break;
        case V4L2_MEMORY_DMABUF:
            LOG(INFO) << "V4L2_MEMORY_DMABUF";
            break;
        default:
            LOG(ERROR) << "Invalid memory type";
            return -1;
    }

    if (v4l2_ioctl(encoder_fd_, VIDIOC_QBUF, &v4l2_buf) < 0) {
        LOG(ERROR) << "Failed to queue buffer";
        return -1;
    }
    return 0;
}

void VideoEncoder::EnqueueEmptyBufferInfo() {
    // First enqueue all the empty buffers on capture plane.

    for (uint32_t i = 0; i < capplane_num_buffers_; ++i) {
        struct v4l2_buffer queue_cap_v4l2_buf = {0};
        struct v4l2_plane queue_cap_planes[MAX_PLANES] = {0};
        Buffer *buffer;

        buffer = &capplane_buffers_[i];

        queue_cap_v4l2_buf.index = i;
        queue_cap_v4l2_buf.m.planes = queue_cap_planes;
        queue_cap_v4l2_buf.type = capplane_buf_type_;
        queue_cap_v4l2_buf.memory = capplane_mem_type_;
        queue_cap_v4l2_buf.length = capplane_num_planes_;

        if (q_buffer(queue_cap_v4l2_buf, buffer) < 0) {
            LOG(ERROR) << "Error while queueing buffer on capture plane";
            exit(-1);
        }
    }
}

void VideoEncoder::DequeueEmptyBufferInfo() {
    // Dequeue the empty buffer on output plane.
    struct v4l2_buffer v4l2_buf = {0};
    struct v4l2_plane planes[MAX_PLANES] = {0};
    Buffer *buffer = new Buffer(outplane_buf_type_, outplane_mem_type_, 0);
    v4l2_buf.m.planes = planes;
    v4l2_buf.type = outplane_buf_type_;
    v4l2_buf.memory = outplane_mem_type_;

    if (dq_buffer(v4l2_buf, &buffer) < 0) {
        LOG(ERROR) << "Error while dequeueing buffer on output plane";
        exit(-1);
    }
}

Buffer VideoEncoder::CapturePlaneDequeue() {
    while (is_running_) {}
    // Dequeue all the filled buffers on capture plane.
    for (uint32_t i = 0; i < capplane_num_buffers_; ++i) {
        struct v4l2_buffer dequeue_cap_v4l2_buf = {0};
        struct v4l2_plane dequeue_cap_planes[MAX_PLANES] = {0};
        Buffer *buffer;

        buffer = &capplane_buffers_[i];
        dequeue_cap_v4l2_buf.index = i;
        dequeue_cap_v4l2_buf.m.planes = dequeue_cap_planes;
        dequeue_cap_v4l2_buf.type = capplane_buf_type_;
        dequeue_cap_v4l2_buf.memory = capplane_mem_type_;
        dequeue_cap_v4l2_buf.length = capplane_num_planes_;

        if (dq_buffer(dequeue_cap_v4l2_buf, &buffer) < 0) {
            LOG(ERROR) << "Error while dequeueing buffer on capture plane";
            exit(-1);
        }
    }
}

Buffer VideoEncoder::DequeueBufferInfo() {
    struct v4l2_buffer dequeue_cap_v4l2_buf = {0};
    struct v4l2_plane dequeue_cap_planes[MAX_PLANES] = {0};
    Buffer *buffer = new Buffer(capplane_buf_type_,
                              capplane_mem_type_, 0);

    dequeue_cap_v4l2_buf.m.planes = dequeue_cap_planes;
    dequeue_cap_v4l2_buf.type = capplane_buf_type_;
    dequeue_cap_v4l2_buf.memory = capplane_mem_type_;
    dequeue_cap_v4l2_buf.length = capplane_num_planes_;

    if (dq_buffer(dequeue_cap_v4l2_buf, &buffer) < 0) {
        LOG(ERROR) << "Error while dequeueing buffer on capture plane";
        exit(-1);
    }
    return *buffer;
}

int
VideoEncoder::dq_buffer(struct v4l2_buffer &v4l2_buf, Buffer **buffer) {
    int ret_val = 0;
    bool is_in_error = false;
    auto num_retries = requestbuffers_count_;

    do {
        ret_val = v4l2_ioctl(encoder_fd_, VIDIOC_DQBUF, &v4l2_buf);

        if (ret_val == 0) {
            std::unique_lock <std::mutex> lock(mutex_);
            switch (v4l2_buf.type) {
                case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
                    if (buffer)
                        *buffer = &outplane_buffers_[v4l2_buf.index];
                    for (uint32_t j = 0; j < outplane_buffers_[v4l2_buf.index].n_planes; j++) {
                        outplane_buffers_[v4l2_buf.index].planes[j].bytesused =
                                v4l2_buf.m.planes[j].bytesused;
                    }
                    num_queued_outplane_buffers_--;
                    break;

                case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
                    if (buffer)
                        *buffer = &capplane_buffers_[v4l2_buf.index];
                    for (uint32_t j = 0; j < capplane_buffers_[v4l2_buf.index].n_planes; j++) {
                        capplane_buffers_[v4l2_buf.index].planes[j].bytesused =
                                v4l2_buf.m.planes[j].bytesused;
                    }
                    num_queued_capplane_buffers_--;
                    break;

                default:
                    LOG(ERROR) << "Invaild buffer type";
            }
//            notify and unlock
            cond_.notify_one();
            lock.unlock();

        } else if (errno == EAGAIN) {
            std::unique_lock <std::mutex> lock(mutex_);
            if (v4l2_buf.flags & V4L2_BUF_FLAG_LAST) {
                LOG(ERROR) << "Last buffer returned";
                break;
            }
            lock.unlock();
            if (num_retries-- == 0) {
                // Resource temporarily unavailable.
                LOG(ERROR) << "Resource temporarily unavailable";
                break;
            }
        } else {
            is_in_error = true;
            break;
        }
    } while (ret_val && !is_in_error);

    return ret_val;
}

void VideoEncoder::EnqueueBufferInfo(Buffer &buffer) {
    // Enqueue the filled buffer on output plane.
    struct v4l2_buffer v4l2_buf = {0};
    struct v4l2_plane planes[MAX_PLANES] = {0};

    v4l2_buf.m.planes = planes;
    v4l2_buf.type = outplane_buf_type_;
    v4l2_buf.memory = outplane_mem_type_;
    v4l2_buf.index = buffer.index;

    if (q_buffer(v4l2_buf, &buffer) < 0) {
        LOG(ERROR) << "Error while enqueueing buffer on output plane";
        exit(-1);
    }


}

void VideoEncoder::Encode(const Buffer &buffer) {
    // capture
    EnqueueEmptyBufferInfo();
    // output
    EnqueueBufferInfo(const_cast<Buffer &>(buffer));
    // capture
    auto res = DequeueBufferInfo();
    // output
    DequeueEmptyBufferInfo();
}