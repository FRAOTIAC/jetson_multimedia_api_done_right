//
// Created by Lucas on 2023/6/2.
//

#ifndef JETSON_MULTIMEDIA_API_DONE_RIGHT_VIDEO_DEVICE_H
#define JETSON_MULTIMEDIA_API_DONE_RIGHT_VIDEO_DEVICE_H

/* abstract class of a video device
 * a video device can be a camera, a converter , a encoder, a decoder, a display, a file, etc.
 * */
class VideoDevice {
public:
    VideoDevice() = default;

    virtual ~VideoDevice() = default;

    virtual void Init() = 0;

    virtual void Open() = 0;

    virtual void Close() = 0;

    virtual void Start() = 0;

    virtual void Stop() = 0;

    virtual void SetCapturePlaneFormat() = 0;

    virtual void SetOutputPlaneFormat() = 0;

    virtual void PrepareBuffers() {

        SetCapturePlaneFormat();
        SetOutputPlaneFormat();

        RequestCapturePlaneBuffers();
        CaptureBuffersSetup();

        RequestOutputPlaneBuffers();
        OutputPlaneBuffersSetup();

    }

    virtual void RequestCapturePlaneBuffers() = 0;

    virtual void CaptureBuffersSetup() = 0;

    virtual void RequestOutputPlaneBuffers() = 0;

    virtual void OutputPlaneBuffersSetup() = 0;
};


#endif //JETSON_MULTIMEDIA_API_DONE_RIGHT_VIDEO_DEVICE_H
