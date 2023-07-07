## Abstract design

The overall abstract class is to represent video device, such as:
1. Camera device
2. Video Convertor device
3. Video Encoder Device
4. Video Decoder Device

## Buffer structure design

Among them, a buffer struct is passing through. I call this buffer struct NvBuffer
This is not the final name, and NvBuffer is widely used in many of Nvidia SDKs. 
This so-called buffer struct basic include those following things:
1. a file-holder: fd, this is used to point the hardware memory (DMA)
2. a user-space memory pointers (not a pointer but pointers to support multi-plane format)
3. a kernel-space memory pointers
4. information of the buffer, the size, order, pitch, color-depth, and so on.
5. information of the color-format
6. Timestamp(s)

the Buffer class in buffer.h is a good example, but the definitions is not straight-forward. A good 
way to refactor is to separate the definitions. 

1.  buffer plane format class
2.  buffer plane parameter class
3.  buffer class 

Notify that the buffer class is built based on v4l2-buffer structure. I will need to read more 
about v4l2-buffer.

## Pipeline
An easy-to-use video process pipeline is the ultimate goal of this project. The code will be ease to 
read, string, a good example of modern C++ style and fits SOLID preconception.

## Break-up the question
1. Video device abstruct class design
2. Buffer class design
3. Implement Camera device
4. Implement Video convertor device
5. Implement Video encoder device
6. Implement Video decoder device
7. Support more functions