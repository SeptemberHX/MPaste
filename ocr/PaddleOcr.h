// input: Depends on ONNX Runtime's C++ API (headers only; the library is loaded at run time).
// output: Reads the text in a picture with PaddleOCR's detection and recognition models.
// pos: The engine of mpaste-ocr, the helper MPaste starts for each recognition on Linux.
// update: If I change, update this header block and my folder README.md.
//
// The pipeline follows RapidOcrOnnx (https://github.com/RapidAI/RapidOcrOnnx,
// Apache-2.0): DB text detection, boxes cut out straight, CRNN recognition
// with CTC decoding. Simplified for MPaste: no angle model (copied pictures
// are upright), the dictionary read from the recognition model itself, a long
// screenshot read in slices, the lines put back in reading order, and the few
// image steps it needs written here instead of taken from OpenCV, so the
// helper links nothing but Qt and the C library.
#ifndef MPASTE_PADDLEOCR_H
#define MPASTE_PADDLEOCR_H

#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace PaddleOcrImage {
// 8-bit BGR, rows packed.
struct Bgr {
    int width = 0;
    int height = 0;
    const std::uint8_t *data = nullptr;
    std::size_t stride = 0;
};

struct Point {
    float x = 0, y = 0;
};
}  // namespace PaddleOcrImage

class PaddleOcr {
public:
    PaddleOcr();
    ~PaddleOcr();

    // Loads ONNX Runtime (libonnxruntime.so.1). False, with why, when it is
    // not installed.
    static bool loadRuntime(std::string *error);

    // detModel and recModel are .onnx files; the recognition model carries
    // its dictionary as the metadata entry "character". Empty string if
    // loaded, else what went wrong. loadRuntime() first.
    std::string load(const std::string &detModel, const std::string &recModel, int threads);

    // The text in a picture, one line per line of text.
    std::string read(const PaddleOcrImage::Bgr &image);

private:
    using Quad = std::vector<PaddleOcrImage::Point>;   // clockwise from the top left
    struct Line {
        float x = 0, y = 0, w = 0, h = 0;   // bounds in the picture
        Quad corners;
        std::string text;
    };

    std::vector<Quad> detect(const PaddleOcrImage::Bgr &image, int top, int height);
    std::string recognize(const PaddleOcrImage::Bgr &image, const Quad &corners);

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::SessionOptions> options_;
    std::unique_ptr<Ort::Session> det_;
    std::unique_ptr<Ort::Session> rec_;
    std::string detInput_, detOutput_, recInput_, recOutput_;
    std::vector<std::string> keys_;   // 0 is the CTC blank
};

#endif // MPASTE_PADDLEOCR_H
